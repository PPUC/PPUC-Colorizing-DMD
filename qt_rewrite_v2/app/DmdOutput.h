#pragma once

#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::app {

// Where pictures go: displays connected to this computer, which libdmdutil finds itself, or a
// dmdserver (libdmdutil's server process) that drives the displays.
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
    bool pixelcade = true;
    QString pixelcadeDevice;
    bool pin2dmd = true;
    QString serverAddress = QStringLiteral("localhost");
    int serverPort = 6789;
};

[[nodiscard]] DmdOutputSettings loadDmdOutputSettings();
void saveDmdOutputSettings(const DmdOutputSettings& settings);

// Sends pictures to DMDs through libdmdutil. libdmdutil looks for displays in the background
// and scales a picture to a display of another size. Without libdmdutil in the build
// (COLORIZINGDMD_DMDUTIL_PREFIX) isAvailable() is false and the rest does nothing.
class DmdOutput {
public:
    DmdOutput();
    ~DmdOutput();
    DmdOutput(const DmdOutput&) = delete;
    DmdOutput& operator=(const DmdOutput&) = delete;

    [[nodiscard]] static bool isAvailable();

    // Starts looking for displays with the settings; stops an output that runs. libdmdutil has
    // one configuration per process.
    void start(const DmdOutputSettings& settings);
    void stop();
    [[nodiscard]] bool isRunning() const;
    // Still looking for displays.
    [[nodiscard]] bool isFinding() const;
    [[nodiscard]] bool hasDisplay() const;
    // A display with 64 rows (256x64 or 192x64) was found.
    [[nodiscard]] bool hasHdDisplay() const;

    // Sends the picture unless it is the one sent last.
    void show(const document::Rgb565Frame& picture);
    [[nodiscard]] std::size_t picturesSent() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace colorizing::app
