#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vpxhost {
class PluginBus;
}

namespace colorizing::vpx {

// The VPX plugins the editor hosts (libvpxhost's plugin bus): DMDUtil for DMD output, later
// more. One bus per process, created on first use on the UI thread and kept until the process
// ends; everything here must be called on the UI thread.
//
// The plugins come from a folder, one folder per plugin as VPX ships them; see
// pluginDirectory(). Without plugin hosting in the build (COLORIZINGDMD_LIBVPXHOST_DIR and
// COLORIZINGDMD_VPINBALL_DIR) isBuiltIn() is false and nothing loads.
class Host {
public:
    using Settings = std::vector<std::pair<std::string, std::string>>;
    // level: 0x00 debug, 0x10 info, 0x20 warning, 0x40 error (the plugin SDK's LPI_LVL_*).
    using LogHandler =
        std::function<void(unsigned int level, const std::string& source, const std::string& message)>;
    static constexpr unsigned int kLogError = 0x40;

    [[nodiscard]] static bool isBuiltIn();
    [[nodiscard]] static Host& instance();

    // Where the plugins are looked for: COLORIZINGDMD_VPX_PLUGIN_DIR if set; otherwise
    // "plugins" beside the executable, "PlugIns/vpx" in a macOS app bundle, or the build's own
    // folder of plugins.
    [[nodiscard]] static std::string pluginDirectory();

    // Whether the plugin folder has the plugin. Opens the bus on first use.
    [[nodiscard]] bool hasPlugin(const std::string& id);
    // The ids of the plugins in the plugin folder.
    [[nodiscard]] std::vector<std::string> pluginIds();
    // The plugins in the plugin folder, as their plugin.cfg describes them.
    struct PluginDescription {
        std::string id;
        std::string name;
        std::string description;
        std::string version;
    };
    [[nodiscard]] std::vector<PluginDescription> plugins();
    // Loads the plugin with the settings (unloading it first if it is loaded: a plugin reads
    // its settings when it loads). Settings not given take the plugin's defaults.
    bool load(const std::string& id, const Settings& settings, std::string* error);
    void unload(const std::string& id);
    [[nodiscard]] bool isLoaded(const std::string& id) const;
    // Runs what the plugins queued for the UI thread.
    void process();

    // Receives every plugin's log lines and the bus's own messages (source empty).
    void setLogHandler(LogHandler handler);

    // The last kMaxLogLines log lines of the bus and every plugin, oldest first, kept whether
    // or not a handler is set (Help -> Plugin Log). Plugins log from their own threads too;
    // these may be called from any thread.
    struct LogLine {
        // Milliseconds since the epoch.
        int64_t timeMs = 0;
        unsigned int level = 0;
        // The plugin's name; empty for the bus itself.
        std::string source;
        std::string message;
    };
    static constexpr std::size_t kMaxLogLines = 5000;
    [[nodiscard]] std::vector<LogLine> logLines() const;
    // Changes with every line logged (also those dropped since) and with clearLog().
    [[nodiscard]] uint64_t logCount() const;
    void clearLog();
    // Adds a line of the editor's own to the log (source "Editor"): what the editor asked a
    // plugin to do, which the plugin itself may not report.
    static void log(unsigned int level, const std::string& message);
    static constexpr unsigned int kLogInfo = 0x10;

    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

private:
    Host();
    bool open(std::string* error);
    // The open bus; nullptr before open() succeeded or without plugin hosting.
    [[nodiscard]] vpxhost::PluginBus* bus();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    friend class DisplaySource;
    friend class PupHost;
};

// A picture the editor shows, offered on the bus as a display (an RGB565 DMD), for plugins
// that show displays: DMDUtil sends it to DMDs. The display exists while a picture is set.
class DisplaySource {
public:
    DisplaySource();
    ~DisplaySource();
    DisplaySource(const DisplaySource&) = delete;
    DisplaySource& operator=(const DisplaySource&) = delete;

    // At most 256x64. A picture of another size than the last replaces the display.
    void setPicture(int width, int height, const uint16_t* pixels);
    void clear();
    // How many pictures were set since the display was created.
    [[nodiscard]] unsigned int frameId() const;
    // How often a plugin fetched the picture.
    [[nodiscard]] unsigned int reads() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace colorizing::vpx
