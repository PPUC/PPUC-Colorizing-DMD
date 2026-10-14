#include "DmdOutput.h"

#include <QSettings>

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>

#include "vpx/VpxHost.h"

namespace colorizing::app {

namespace {

const QString kGroup = QStringLiteral("dmdOutput/");

}  // namespace

DmdOutputSettings loadDmdOutputSettings()
{
    const QSettings settings;
    DmdOutputSettings result;
    result.target = settings.value(kGroup + QStringLiteral("target")).toString() ==
                            QStringLiteral("server")
                        ? DmdOutputSettings::Target::DmdServer
                        : DmdOutputSettings::Target::LocalDisplays;
    result.zedmd = settings.value(kGroup + QStringLiteral("zedmd"), result.zedmd).toBool();
    result.zedmdDevice = settings.value(kGroup + QStringLiteral("zedmdDevice")).toString();
    result.zedmdWiFi = settings.value(kGroup + QStringLiteral("zedmdWiFi"), false).toBool();
    result.zedmdWiFiAddress =
        settings.value(kGroup + QStringLiteral("zedmdWiFiAddress")).toString();
    result.zedmdBrightness =
        settings.value(kGroup + QStringLiteral("zedmdBrightness"), result.zedmdBrightness).toInt();
    result.pixelcade =
        settings.value(kGroup + QStringLiteral("pixelcade"), result.pixelcade).toBool();
    result.pixelcadeDevice = settings.value(kGroup + QStringLiteral("pixelcadeDevice")).toString();
    result.pin2dmd = settings.value(kGroup + QStringLiteral("pin2dmd"), result.pin2dmd).toBool();
    result.serverAddress =
        settings.value(kGroup + QStringLiteral("serverAddress"), result.serverAddress).toString();
    result.serverPort =
        settings.value(kGroup + QStringLiteral("serverPort"), result.serverPort).toInt();
    return result;
}

void saveDmdOutputSettings(const DmdOutputSettings& value)
{
    QSettings settings;
    settings.setValue(kGroup + QStringLiteral("target"),
                      value.target == DmdOutputSettings::Target::DmdServer
                          ? QStringLiteral("server")
                          : QStringLiteral("local"));
    settings.setValue(kGroup + QStringLiteral("zedmd"), value.zedmd);
    settings.setValue(kGroup + QStringLiteral("zedmdDevice"), value.zedmdDevice);
    settings.setValue(kGroup + QStringLiteral("zedmdWiFi"), value.zedmdWiFi);
    settings.setValue(kGroup + QStringLiteral("zedmdWiFiAddress"), value.zedmdWiFiAddress);
    settings.setValue(kGroup + QStringLiteral("zedmdBrightness"), value.zedmdBrightness);
    settings.setValue(kGroup + QStringLiteral("pixelcade"), value.pixelcade);
    settings.setValue(kGroup + QStringLiteral("pixelcadeDevice"), value.pixelcadeDevice);
    settings.setValue(kGroup + QStringLiteral("pin2dmd"), value.pin2dmd);
    settings.setValue(kGroup + QStringLiteral("serverAddress"), value.serverAddress);
    settings.setValue(kGroup + QStringLiteral("serverPort"), value.serverPort);
}

namespace {

const std::string kPluginId = "DMDUtil";

const char* boolSetting(bool value)
{
    return value ? "1" : "0";
}

}  // namespace

struct DmdOutput::Impl {
    vpx::DisplaySource display;
    bool running = false;
    // Written by the plugin's and libdmdutil's threads as they log.
    mutable std::mutex messageMutex;
    QString message;

    void setMessage(const QString& text)
    {
        const std::lock_guard<std::mutex> lock(messageMutex);
        message = text;
    }
    std::vector<uint16_t> lastPixels;
    int lastWidth = 0;
    int lastHeight = 0;
    std::size_t sent = 0;
};

DmdOutput::DmdOutput()
    : m_impl(std::make_unique<Impl>())
{
}

DmdOutput::~DmdOutput()
{
    stop();
}

bool DmdOutput::isAvailable()
{
    return vpx::Host::isBuiltIn() && vpx::Host::instance().hasPlugin(kPluginId);
}

bool DmdOutput::start(const DmdOutputSettings& settings)
{
    stop();
    Impl& impl = *m_impl;
    const bool server = settings.target == DmdOutputSettings::Target::DmdServer;
    // An empty device or address keeps the plugin's default: libdmdutil looks for it.
    vpx::Host::Settings values = {
        {"ZeDMD", boolSetting(!server && settings.zedmd)},
        {"ZeDMDWiFiEnabled", boolSetting(!server && settings.zedmdWiFi)},
        {"ZeDMDBrightness", std::to_string(settings.zedmdBrightness)},
        {"Pixelcade", boolSetting(!server && settings.pixelcade)},
        {"PIN2DMD", boolSetting(!server && settings.pin2dmd)},
        {"DMDServer", boolSetting(server)},
        {"DMDServerPort", std::to_string(settings.serverPort)},
        // A dmdserver drives its own displays; this computer's are not looked for.
        {"FindDisplays", boolSetting(!server)},
    };
    const auto addText = [&values](const char* setting, const QString& text) {
        if (!text.trimmed().isEmpty()) {
            values.emplace_back(setting, text.trimmed().toStdString());
        }
    };
    addText("ZeDMDDevice", settings.zedmdDevice);
    addText("ZeDMDWiFiAddr", settings.zedmdWiFiAddress);
    addText("PixelcadeDevice", settings.pixelcadeDevice);
    addText("DMDServerAddr", settings.serverAddress);

    vpx::Host& host = vpx::Host::instance();
    host.setLogHandler([this](unsigned int level, const std::string& source,
                              const std::string& message) {
        // The plugin's own lines and libdmdutil's, which it passes on.
        if (source == kPluginId || (source.empty() && level >= vpx::Host::kLogError)) {
            m_impl->setMessage(QString::fromStdString(message));
        }
    });
    impl.setMessage({});
    std::string error;
    if (!host.load(kPluginId, values, &error)) {
        impl.setMessage(QString::fromStdString(error));
        return false;
    }
    impl.running = true;
    return true;
}

void DmdOutput::stop()
{
    Impl& impl = *m_impl;
    impl.display.clear();
    if (impl.running) {
        vpx::Host::instance().unload(kPluginId);
        vpx::Host::instance().setLogHandler({});
    }
    impl.running = false;
    impl.lastPixels.clear();
    impl.lastWidth = 0;
    impl.lastHeight = 0;
}

bool DmdOutput::isRunning() const
{
    return m_impl->running;
}

QString DmdOutput::lastMessage() const
{
    const std::lock_guard<std::mutex> lock(m_impl->messageMutex);
    return m_impl->message;
}

void DmdOutput::show(const document::Rgb565Frame& picture)
{
    Impl& impl = *m_impl;
    if (!impl.running || !picture.isValid()) {
        return;
    }
    vpx::Host::instance().process();
    if (impl.lastWidth == picture.size.width && impl.lastHeight == picture.size.height &&
        std::equal(impl.lastPixels.begin(), impl.lastPixels.end(), picture.pixels.begin(),
                   picture.pixels.end())) {
        return;
    }
    impl.lastPixels.assign(picture.pixels.begin(), picture.pixels.end());
    impl.lastWidth = picture.size.width;
    impl.lastHeight = picture.size.height;
    impl.display.setPicture(impl.lastWidth, impl.lastHeight, impl.lastPixels.data());
    ++impl.sent;
}

std::size_t DmdOutput::picturesSent() const
{
    return m_impl->sent;
}

unsigned int DmdOutput::pictureReads() const
{
    return m_impl->display.reads();
}

}  // namespace colorizing::app
