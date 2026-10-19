#pragma once

#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::app {

// Where pictures go: displays connected to this computer, which libdmdutil finds itself, or a
// dmdserver (libdmdutil's server process) that drives the displays. Applied as settings of
// VPX's DMDUtil plugin.
struct DmdOutputSettings {
    enum class Target {
        LocalDisplays,
        DmdServer,
    };
    Target target = Target::LocalDisplays;
    bool zedmd = true;
    // Empty: libdmdutil looks on every serial port.
    QString zedmdDevice;
    bool zedmdWiFi = false;
    QString zedmdWiFiAddress;
    // 0 to 15; -1 keeps the display's own setting.
    int zedmdBrightness = -1;
    // libzedmd's debug messages, into the plugin log (Help -> Plugin Log).
    bool zedmdDebug = false;
    bool pixelcade = true;
    QString pixelcadeDevice;
    bool pin2dmd = true;
    QString serverAddress = QStringLiteral("localhost");
    int serverPort = 6789;

    // Which plane of an ordinary project (128x32 native plane, 256x64 extra plane) goes to the
    // display. A project with one plane always sends that one.
    enum class Picture {
        // The plane the canvas shows (in the Runtime view, the rows it shows).
        Canvas,
        Native,
        // A frame without extra data sends its native plane, which libdmdutil scales up; in
        // the Runtime view libserum's 64-row picture, which it upscales as on a machine.
        Extra,
    };
    Picture picture = Picture::Canvas;
};

[[nodiscard]] DmdOutputSettings loadDmdOutputSettings();
void saveDmdOutputSettings(const DmdOutputSettings& settings);

// Sends pictures to DMDs through VPX's DMDUtil plugin (vpx::Host), which hands them to
// libdmdutil. libdmdutil looks for displays in the background and scales a picture to a
// display of another size. Without plugin hosting in the build, or without the DMDUtil plugin
// in the plugin folder, isAvailable() is false and the rest does nothing.
class DmdOutput {
public:
    DmdOutput();
    ~DmdOutput();
    DmdOutput(const DmdOutput&) = delete;
    DmdOutput& operator=(const DmdOutput&) = delete;

    [[nodiscard]] static bool isAvailable();

    // Loads the plugin with the settings; an output that runs is started again with them
    // (the plugin and libdmdutil read their settings when they start). Returns false, with
    // the reason in lastMessage(), when the plugin does not load.
    bool start(const DmdOutputSettings& settings);
    void stop();
    [[nodiscard]] bool isRunning() const;
    // The last thing the DMDUtil plugin or libdmdutil logged (which displays it found, why one
    // failed), or why the output did not start.
    [[nodiscard]] QString lastMessage() const;
    // What libdmdutil logged for each display it found since the start ("ZeDMD 6.1.2 found:
    // ..."), oldest first. A PIN2DMD is not logged when found, so it is never listed.
    [[nodiscard]] std::vector<QString> foundDisplays() const;

    // Offers the picture to the plugin unless it is the one offered last.
    void show(const document::Rgb565Frame& picture);
    [[nodiscard]] std::size_t picturesSent() const;
    // How often the plugin fetched the picture (it does so at 60 Hz while it shows one).
    [[nodiscard]] unsigned int pictureReads() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace colorizing::app
