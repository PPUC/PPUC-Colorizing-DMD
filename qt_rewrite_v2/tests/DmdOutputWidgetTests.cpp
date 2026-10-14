// DMD output through VPX's DMDUtil plugin, without hardware: every display switched off, so
// libdmdutil finds none, and the plugin takes the pictures anyway.

#include <cstdlib>
#include <iostream>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QTemporaryDir>

#include "DmdOutput.h"
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
    stored.pin2dmd = false;
    stored.picture = app::DmdOutputSettings::Picture::Extra;
    app::saveDmdOutputSettings(stored);
    const app::DmdOutputSettings loaded = app::loadDmdOutputSettings();
    expect(loaded.target == stored.target && loaded.serverAddress == stored.serverAddress &&
               loaded.serverPort == 6790 && loaded.zedmdBrightness == 7 && !loaded.pin2dmd &&
               loaded.picture == app::DmdOutputSettings::Picture::Extra,
           "settings are kept");
    QSettings().remove(QStringLiteral("dmdOutput"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
