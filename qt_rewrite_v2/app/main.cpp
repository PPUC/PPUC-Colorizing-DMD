#include <QApplication>
#include <QElapsedTimer>
#include <QStringList>
#include <QThread>

#include <cstdio>
#include <string>

#include "Appearance.h"
#include "DmdOutput.h"
#include "MainWindow.h"
#include "Translations.h"
#include "vpx/VpxHost.h"

namespace {

// --check-plugins, for the packaging scripts: loads and unloads every VPX plugin the package
// has, from the folder the app uses, and fails when there is none or one does not load.
int checkPlugins()
{
    colorizing::vpx::Host& host = colorizing::vpx::Host::instance();
    const std::vector<std::string> ids = host.pluginIds();
    std::printf("VPX plugins in %s\n", colorizing::vpx::Host::pluginDirectory().c_str());
    bool ok = !ids.empty();
    for (const std::string& id : ids) {
        std::string error;
        const bool loaded = host.load(id, {}, &error);
        std::printf("  %s: %s\n", id.c_str(), loaded ? "loads" : error.c_str());
        host.unload(id);
        ok = ok && loaded;
    }
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// --check-dmd-output, to find out why a display is not found: starts the DMD output with the
// settings of View -> DMD Output Settings, sends a test picture (a 256x64 gradient) for 8 s,
// and prints the plugin log.
int checkDmdOutput()
{
    colorizing::vpx::Host& host = colorizing::vpx::Host::instance();
    colorizing::app::DmdOutput output;
    std::printf("DMD output: %s\n", colorizing::app::DmdOutput::isAvailable()
                                       ? "the DMDUtil plugin is there"
                                       : "no DMDUtil plugin in this build");
    const bool started = output.start(colorizing::app::loadDmdOutputSettings());
    if (started) {
        colorizing::document::Rgb565Frame picture;
        picture.size = {256, 64};
        picture.pixels = colorizing::document::CompactArray<uint16_t>(256 * 64, 0);
        QElapsedTimer timer;
        timer.start();
        uint16_t shift = 0;
        while (timer.elapsed() < 8000) {
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 256; ++x) {
                    picture.pixels[static_cast<std::size_t>(y * 256 + x)] =
                        static_cast<uint16_t>((((x + shift) / 8) & 0x1f) << 11 | (y & 0x3f) << 5);
                }
            }
            ++shift;
            output.show(picture);
            QThread::msleep(33);
        }
        std::printf("Pictures sent: %zu, read by the plugin: %u\n", output.picturesSent(),
                    output.pictureReads());
        const std::vector<QString> found = output.foundDisplays();
        std::printf("Displays found: %s\n", found.empty() ? "none (a PIN2DMD is not reported)" : "");
        for (const QString& line : found) {
            std::printf("  %s\n", line.toStdString().c_str());
        }
        output.stop();
    }
    for (const colorizing::vpx::Host::LogLine& line : host.logLines()) {
        std::printf("[%s] %s\n", line.source.empty() ? "bus" : line.source.c_str(),
                    line.message.c_str());
    }
    std::fflush(stdout);
    return started ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ColorizingDMD"));
    QApplication::setApplicationName(QStringLiteral("ColorizingDMDQtV2"));
    if (QApplication::arguments().contains(QStringLiteral("--check-plugins"))) {
        return checkPlugins();
    }
    if (QApplication::arguments().contains(QStringLiteral("--check-dmd-output"))) {
        return checkDmdOutput();
    }
    colorizing::app::installTranslations();
    colorizing::app::applyTheme(colorizing::app::loadTheme());
    colorizing::app::MainWindow window;
    window.show();
    window.offerUntitledAutosave();
    return app.exec();
}
