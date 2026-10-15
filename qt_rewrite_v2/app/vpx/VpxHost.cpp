#include "VpxHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>

#ifdef COLORIZINGDMD_HAS_VPX_PLUGINS
#include "plugins/ControllerPlugin.h"
#include "vpx/EditorApiHost.h"
#include "vpxhost/PluginBus.h"
#endif

namespace colorizing::vpx {

namespace {

// libdmdutil's update buffers, and so the DMDUtil plugin, take at most this many pixels.
constexpr int kMaxPixels = 256 * 64;

// The log history: one per process, as the bus. Plugin threads add to it.
struct LogHistory {
    mutable std::mutex mutex;
    std::deque<Host::LogLine> lines;
    uint64_t count = 0;

    void add(unsigned int level, const std::string& source, const std::string& message)
    {
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        const std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(Host::LogLine{now, level, source, message});
        while (lines.size() > Host::kMaxLogLines) {
            lines.pop_front();
        }
        ++count;
    }
};

LogHistory& logHistory()
{
    static LogHistory history;
    return history;
}

}  // namespace

std::vector<Host::LogLine> Host::logLines() const
{
    const LogHistory& history = logHistory();
    const std::lock_guard<std::mutex> lock(history.mutex);
    return std::vector<LogLine>(history.lines.begin(), history.lines.end());
}

uint64_t Host::logCount() const
{
    const LogHistory& history = logHistory();
    const std::lock_guard<std::mutex> lock(history.mutex);
    return history.count;
}

void Host::log(unsigned int level, const std::string& message)
{
    logHistory().add(level, "Editor", message);
}

void Host::clearLog()
{
    LogHistory& history = logHistory();
    const std::lock_guard<std::mutex> lock(history.mutex);
    history.lines.clear();
    ++history.count;
}

std::string Host::pluginDirectory()
{
    const QByteArray configured = qgetenv("COLORIZINGDMD_VPX_PLUGIN_DIR");
    if (!configured.isEmpty()) {
        return configured.toStdString();
    }
    QStringList candidates;
    if (QCoreApplication::instance()) {
        const QDir executableDir(QCoreApplication::applicationDirPath());
        candidates << executableDir.absoluteFilePath(QStringLiteral("plugins"))
                   << executableDir.absoluteFilePath(QStringLiteral("../PlugIns/vpx"));
    }
#ifdef COLORIZINGDMD_BUILD_VPX_PLUGIN_DIR
    candidates << QStringLiteral(COLORIZINGDMD_BUILD_VPX_PLUGIN_DIR);
#endif
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir()) {
            return QDir::cleanPath(candidate).toStdString();
        }
    }
    return candidates.isEmpty() ? std::string() : QDir::cleanPath(candidates.front()).toStdString();
}

#ifdef COLORIZINGDMD_HAS_VPX_PLUGINS

struct Host::Impl {
    LogHandler log;
    std::unique_ptr<vpxhost::PluginBus> bus;
    std::string openError;
};

bool Host::isBuiltIn()
{
    return true;
}

Host& Host::instance()
{
    static Host host;
    return host;
}

Host::Host()
    : m_impl(std::make_unique<Impl>())
{
}

Host::~Host()
{
    if (m_impl->bus) {
        // While the host's endpoint is still there; unloading plugins still call the API's
        // functions, which need no subscription.
        uninstallEditorApi();
        m_impl->bus->Shutdown();
    }
}

bool Host::open(std::string* error)
{
    if (m_impl->bus) {
        return true;
    }
    if (!m_impl->openError.empty()) {
        if (error) {
            *error = m_impl->openError;
        }
        return false;
    }
    auto bus = std::make_unique<vpxhost::PluginBus>(
        vpxhost::HostInfo{"ColorizingDMD", "ColorizingDMD", "Serum colorization editor"},
        [this](unsigned int level, const std::string& source, const std::string& message) {
            logHistory().add(level, source, message);
            if (m_impl->log) {
                m_impl->log(level, source, message);
            }
        });
    std::string message;
    if (!bus->Initialize(pluginDirectory(), &message)) {
        m_impl->openError = message;
        if (error) {
            *error = message;
        }
        return false;
    }
    m_impl->bus = std::move(bus);
    installEditorApi(&m_impl->bus->Api(), m_impl->bus->HostEndpointId());
    return true;
}

bool Host::hasPlugin(const std::string& id)
{
    if (!open(nullptr)) {
        return false;
    }
    const std::vector<vpxhost::PluginInfo> plugins = m_impl->bus->Plugins();
    return std::any_of(plugins.begin(), plugins.end(),
                       [&id](const vpxhost::PluginInfo& plugin) { return plugin.id == id; });
}

std::vector<std::string> Host::pluginIds()
{
    std::vector<std::string> ids;
    if (open(nullptr)) {
        for (const vpxhost::PluginInfo& plugin : m_impl->bus->Plugins()) {
            ids.push_back(plugin.id);
        }
    }
    return ids;
}

std::vector<Host::PluginDescription> Host::plugins()
{
    std::vector<PluginDescription> result;
    if (open(nullptr)) {
        for (const vpxhost::PluginInfo& plugin : m_impl->bus->Plugins()) {
            result.push_back({plugin.id, plugin.name, plugin.description, plugin.version});
        }
    }
    return result;
}

bool Host::load(const std::string& id, const Settings& settings, std::string* error)
{
    if (!open(error)) {
        return false;
    }
    m_impl->bus->UnloadPluginById(id);
    for (const auto& [setting, value] : settings) {
        m_impl->bus->SetSettingOverride(id, setting, value);
    }
    if (!m_impl->bus->LoadPluginById(id)) {
        if (error) {
            *error = "the " + id + " plugin is not in " + pluginDirectory() + " or did not load";
        }
        return false;
    }
    return true;
}

void Host::unload(const std::string& id)
{
    if (m_impl->bus) {
        m_impl->bus->UnloadPluginById(id);
    }
}

bool Host::isLoaded(const std::string& id) const
{
    return m_impl->bus && m_impl->bus->IsPluginLoaded(id);
}

void Host::process()
{
    if (m_impl->bus) {
        m_impl->bus->Process();
    }
}

void Host::setLogHandler(LogHandler handler)
{
    m_impl->log = std::move(handler);
}

using PinballPlugin::Controller::CtrlItemProvider;

struct DisplaySource::Impl {
    // Pictures are written into the next of three buffers, so a plugin thread that just got
    // the current one can copy it while the next picture arrives. The buffers never grow
    // beyond their reserve, so a pointer handed out stays valid.
    std::mutex mutex;
    std::array<std::vector<uint16_t>, 3> buffers;
    std::size_t current = 0;
    unsigned int frameId = 0;
    std::atomic<unsigned int> reads{0};
    int width = 0;
    int height = 0;
    std::unique_ptr<CtrlItemProvider<DisplaySrcId>> provider;

    Impl()
    {
        for (std::vector<uint16_t>& buffer : buffers) {
            buffer.reserve(kMaxPixels);
        }
    }

    static DisplayFrame MSGPIAPI renderFrame(void* context)
    {
        Impl* self = static_cast<Impl*>(context);
        ++self->reads;
        const std::lock_guard<std::mutex> lock(self->mutex);
        return DisplayFrame{self->frameId, self->buffers[self->current].data()};
    }
};

DisplaySource::DisplaySource()
    : m_impl(std::make_unique<Impl>())
{
}

DisplaySource::~DisplaySource()
{
    clear();
}

void DisplaySource::setPicture(int width, int height, const uint16_t* pixels)
{
    Impl& impl = *m_impl;
    if (width <= 0 || height <= 0 || width * height > kMaxPixels || !pixels) {
        return;
    }
    Host& host = Host::instance();
    if (!host.open(nullptr)) {
        return;
    }
    const bool resized = width != impl.width || height != impl.height;
    if (resized) {
        // Withdrawn first, so no plugin reads a buffer of the old size as the new one.
        clear();
    }
    {
        const std::lock_guard<std::mutex> lock(impl.mutex);
        const std::size_t next = (impl.current + 1) % impl.buffers.size();
        impl.buffers[next].assign(pixels, pixels + static_cast<std::size_t>(width) * height);
        impl.current = next;
        ++impl.frameId;
    }
    if (resized || !impl.provider) {
        const vpxhost::PluginBus& bus = *host.m_impl->bus;
        if (!impl.provider) {
            impl.provider = std::make_unique<CtrlItemProvider<DisplaySrcId>>(
                &bus.Api(), bus.HostEndpointId(), CTLPI_DISPLAY_GET_SRC_MSG,
                CTLPI_DISPLAY_ON_SRC_CHG_MSG);
        }
        DisplaySrcId display{};
        display.id.endpointId = bus.HostEndpointId();
        display.id.resId = 0;
        display.width = static_cast<unsigned int>(width);
        display.height = static_cast<unsigned int>(height);
        display.hardware = CTLPI_DISPLAY_HARDWARE_RGB_LED;
        display.callContext = &impl;
        display.frameFormat = CTLPI_DISPLAY_FORMAT_SRGB565;
        display.GetRenderFrame = &Impl::renderFrame;
        impl.provider->SetItem(display);
        impl.width = width;
        impl.height = height;
    }
}

void DisplaySource::clear()
{
    Impl& impl = *m_impl;
    if (impl.provider) {
        impl.provider->ClearItems();
    }
    impl.width = 0;
    impl.height = 0;
}

unsigned int DisplaySource::frameId() const
{
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->frameId;
}

unsigned int DisplaySource::reads() const
{
    return m_impl->reads.load();
}

#else  // no plugin hosting in this build

struct Host::Impl {
};

bool Host::isBuiltIn()
{
    return false;
}

Host& Host::instance()
{
    static Host host;
    return host;
}

Host::Host()
    : m_impl(std::make_unique<Impl>())
{
}

Host::~Host() = default;

bool Host::open(std::string* error)
{
    if (error) {
        *error = "this build has no VPX plugin hosting";
    }
    return false;
}

bool Host::hasPlugin(const std::string&)
{
    return false;
}

std::vector<std::string> Host::pluginIds()
{
    return {};
}

std::vector<Host::PluginDescription> Host::plugins()
{
    return {};
}

bool Host::load(const std::string&, const Settings&, std::string* error)
{
    return open(error);
}

void Host::unload(const std::string&) {}

bool Host::isLoaded(const std::string&) const
{
    return false;
}

void Host::process() {}

void Host::setLogHandler(LogHandler) {}

struct DisplaySource::Impl {
    unsigned int frameId = 0;
};

DisplaySource::DisplaySource()
    : m_impl(std::make_unique<Impl>())
{
}

DisplaySource::~DisplaySource() = default;

void DisplaySource::setPicture(int, int, const uint16_t*)
{
    ++m_impl->frameId;
}

void DisplaySource::clear() {}

unsigned int DisplaySource::frameId() const
{
    return m_impl->frameId;
}

unsigned int DisplaySource::reads() const
{
    return 0;
}

#endif

}  // namespace colorizing::vpx
