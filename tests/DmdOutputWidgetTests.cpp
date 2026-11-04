// DMD output through VPX's DMDUtil plugin, without hardware: every display switched off, so
// libdmdutil finds none, and the plugin takes the pictures anyway.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include "DmdOutput.h"
#include "dialogs/DmdOutputSettingsPage.h"
#include "PluginLogWindow.h"
#include "vpx/VpxHost.h"
#include "document/ProjectDocument.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: DMD output: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

void runDmdOutputWidgetTests()
{
    app::DmdOutputSettings settings;
    settings.zedmd = false;
    settings.zedmdWiFi = false;
    settings.pixelcade = false;
    settings.pin2dmd = false;

    app::DmdOutput output;
    expect(!output.isRunning() && output.picturesSent() == 0, "off at first");
    const bool available = app::DmdOutput::isAvailable();
    expect(output.start(settings) == available && output.isRunning() == available,
           "runs where the build has the DMDUtil plugin: " +
               output.lastMessage().toStdString());
    document::Rgb565Frame picture = document::makeRgb565Frame(render::Size{128, 32}, 0x07e0);
    output.show(picture);
    output.show(picture);
    picture.pixels[5] = 0xf800;
    output.show(picture);
    expect(output.picturesSent() == (available ? 2u : 0u),
           "a picture is sent once, a changed one again");
    if (available) {
        // The plugin picks the display up and fetches the picture on its own thread.
        QElapsedTimer timer;
        timer.start();
        while (output.pictureReads() == 0 && timer.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        expect(output.pictureReads() > 0, "the DMDUtil plugin fetches the picture");
        // A picture of another size replaces the display, and the plugin takes that one too.
        output.show(document::makeRgb565Frame(render::Size{256, 64}, 0x001f));
        const unsigned int before = output.pictureReads();
        timer.restart();
        while (output.pictureReads() == before && timer.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        expect(output.pictureReads() > before, "and the 256x64 one");
        // Settings changed while it runs: started again.
        expect(output.start(settings) && output.isRunning(), "starts again");
    }
    output.stop();
    expect(!output.isRunning(), "stopped");

    const QString configuredUserFolder = qEnvironmentVariable("COLORIZINGDMD_USER_PLUGIN_DIR");
    expect(vpx::Host::userPluginDirectory() == QDir::cleanPath(configuredUserFolder).toStdString(),
           "COLORIZINGDMD_USER_PLUGIN_DIR names the folder for the user's plugins");

    // The plugin log keeps what the bus and the plugins logged, also with no handler set: the
    // bus names its plugin folder when it opens, which the output above did.
    if (vpx::Host::isBuiltIn()) {
        const std::vector<vpx::Host::LogLine> lines = vpx::Host::instance().logLines();
        const bool named = std::any_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.source.empty() &&
                   line.message.find("Plugin directory") != std::string::npos;
        });
        expect(named && vpx::Host::instance().logCount() >= lines.size(),
               "the plugin log keeps the bus's lines");
        // The folder for the user's plugins (the test run's own, see main()) is made and
        // scanned after the editor's.
        const std::string userFolder = vpx::Host::userPluginDirectory();
        const bool userScanned = std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
            return line.source.empty() && line.message == "Plugin directory: " + userFolder;
        });
        expect(userScanned && QFileInfo(QString::fromStdString(userFolder)).isDir(),
               "the folder for the user's plugins is made and scanned");
        if (available) {
            const auto editorSaid = [&lines](const std::string& text) {
                return std::any_of(lines.begin(), lines.end(), [&text](const auto& line) {
                    return line.source == "Editor" && line.message.find(text) != std::string::npos;
                });
            };
            expect(editorSaid("DMD output started, looking for no display") &&
                       editorSaid("DMD output stopped"),
                   "the editor logs what the output looks for, and its stop");
        }
        app::PluginLogWindow window;
        window.refresh();
        expect(window.text().contains(QStringLiteral("Plugin directory")),
               "the Plugin Log window shows them");
        vpx::Host::instance().clearLog();
        window.refresh();
        expect(vpx::Host::instance().logLines().empty() &&
                   !window.text().contains(QStringLiteral("Plugin directory")),
               "Clear empties it");
    }

    // The settings survive a round trip through QSettings: an INI file of a test organization
    // in a folder of its own (the native format on Windows, the registry, needs an
    // organization name, and the user's own settings stay untouched).
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("DmdOutputWidgetTests"));
    app::DmdOutputSettings stored;
    stored.target = app::DmdOutputSettings::Target::DmdServer;
    stored.serverAddress = QStringLiteral("pi.local");
    stored.serverPort = 6790;
    stored.zedmdBrightness = 7;
    stored.zedmdDebug = true;
    stored.pin2dmd = false;
    stored.pinDmd3 = true;
    stored.pinDmd3Device = QStringLiteral("/dev/ttyACM1");
    stored.dmdExtStream = true;
    stored.dmdExtStreamUrl = QStringLiteral("ws://pi.local/dmd");
    stored.dmdExtStreamRetry = true;
    stored.picture = app::DmdOutputSettings::Picture::Extra;
    app::saveDmdOutputSettings(stored);
    const app::DmdOutputSettings loaded = app::loadDmdOutputSettings();
    expect(loaded.target == stored.target && loaded.serverAddress == stored.serverAddress &&
               loaded.serverPort == 6790 && loaded.zedmdBrightness == 7 && loaded.zedmdDebug &&
               !loaded.pin2dmd &&
               loaded.pinDmd3 && loaded.pinDmd3Device == stored.pinDmd3Device &&
               loaded.dmdExtStream && loaded.dmdExtStreamUrl == stored.dmdExtStreamUrl &&
               loaded.dmdExtStreamRetry &&
               loaded.picture == app::DmdOutputSettings::Picture::Extra,
           "settings are kept");
    QSettings().remove(QStringLiteral("dmdOutput"));
    const app::DmdOutputSettings defaults = app::loadDmdOutputSettings();
    expect(!defaults.pinDmd3 && !defaults.dmdExtStream,
           "PinDMD3, which probes the serial ports, and the stream are off by default");

    // The settings page shows them and gives them back.
    app::DmdOutputSettingsPage page(stored);
    const app::DmdOutputSettings shown = page.settings();
    expect(shown.pinDmd3 && shown.pinDmd3Device == stored.pinDmd3Device && shown.dmdExtStream &&
               shown.dmdExtStreamUrl == stored.dmdExtStreamUrl && shown.dmdExtStreamRetry,
           "the page keeps the new outputs' settings");
    QSettings().remove(QStringLiteral("dmdOutput"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
