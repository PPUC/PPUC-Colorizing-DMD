// DMD output through libdmdutil, without hardware: every display switched off, so libdmdutil
// finds none and takes the pictures anyway.

#include <cstdlib>
#include <iostream>
#include <string>

#include <QSettings>
#include <QStandardPaths>

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
    output.start(settings);
    expect(output.isRunning() == app::DmdOutput::isAvailable(),
           "runs where the build has libdmdutil");
    document::Rgb565Frame picture = document::makeRgb565Frame(render::Size{128, 32}, 0x07e0);
    output.show(picture);
    output.show(picture);
    picture.pixels[5] = 0xf800;
    output.show(picture);
    expect(output.picturesSent() == (app::DmdOutput::isAvailable() ? 2u : 0u),
           "a picture is sent once, a changed one again");
    expect(!output.hasDisplay(), "no display without hardware");
    output.stop();
    expect(!output.isRunning(), "stopped");

    // The settings survive a round trip through QSettings.
    QStandardPaths::setTestModeEnabled(true);
    app::DmdOutputSettings stored;
    stored.target = app::DmdOutputSettings::Target::DmdServer;
    stored.serverAddress = QStringLiteral("pi.local");
    stored.serverPort = 6790;
    stored.zedmdBrightness = 7;
    stored.pin2dmd = false;
    app::saveDmdOutputSettings(stored);
    const app::DmdOutputSettings loaded = app::loadDmdOutputSettings();
    expect(loaded.target == stored.target && loaded.serverAddress == stored.serverAddress &&
               loaded.serverPort == 6790 && loaded.zedmdBrightness == 7 && !loaded.pin2dmd,
           "settings are kept");
    QSettings().remove(QStringLiteral("dmdOutput"));
    QStandardPaths::setTestModeEnabled(false);
}
