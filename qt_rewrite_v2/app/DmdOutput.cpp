#include "DmdOutput.h"

#include <QSettings>

#include <algorithm>
#include <string>
#include <utility>

#ifdef COLORIZINGDMD_HAS_DMDUTIL
#include "DMDUtil/Config.h"
#include "DMDUtil/DMD.h"
#endif

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

struct DmdOutput::Impl {
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    std::unique_ptr<DMDUtil::DMD> dmd;
#endif
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
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    return true;
#else
    return false;
#endif
}

void DmdOutput::start(const DmdOutputSettings& settings)
{
    stop();
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    // libdmdutil copies the strings; an empty one means "look for it".
    const std::string zedmdDevice = settings.zedmdDevice.trimmed().toStdString();
    const std::string zedmdWiFiAddress = settings.zedmdWiFiAddress.trimmed().toStdString();
    const std::string pixelcadeDevice = settings.pixelcadeDevice.trimmed().toStdString();
    const std::string serverAddress = settings.serverAddress.trimmed().toStdString();
    DMDUtil::Config* config = DMDUtil::Config::GetInstance();
    const bool server = settings.target == DmdOutputSettings::Target::DmdServer;
    config->SetDMDServer(server);
    config->SetLocalDisplaysActive(!server);
    config->SetDMDServerAddr(serverAddress.c_str());
    config->SetDMDServerPort(settings.serverPort);
    config->SetZeDMD(!server && settings.zedmd);
    config->SetZeDMDDevice(zedmdDevice.c_str());
    config->SetZeDMDWiFiEnabled(!server && settings.zedmdWiFi);
    config->SetZeDMDWiFiAddr(zedmdWiFiAddress.c_str());
    config->SetZeDMDBrightness(settings.zedmdBrightness);
    config->SetPixelcade(!server && settings.pixelcade);
    config->SetPixelcadeDevice(pixelcadeDevice.c_str());
    config->SetPIN2DMD(!server && settings.pin2dmd);
    // The editor sends finished pictures: nothing for libdmdutil to colorize or dump.
    config->SetAltColor(false);
    m_impl->dmd = std::make_unique<DMDUtil::DMD>();
    m_impl->dmd->FindDisplays();
#else
    (void)settings;
#endif
}

void DmdOutput::stop()
{
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    m_impl->dmd.reset();
#endif
    m_impl->lastPixels.clear();
    m_impl->lastWidth = 0;
    m_impl->lastHeight = 0;
}

bool DmdOutput::isRunning() const
{
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    return m_impl->dmd != nullptr;
#else
    return false;
#endif
}

bool DmdOutput::isFinding() const
{
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    return m_impl->dmd && DMDUtil::DMD::IsFinding();
#else
    return false;
#endif
}

bool DmdOutput::hasDisplay() const
{
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    return m_impl->dmd && m_impl->dmd->HasDisplay();
#else
    return false;
#endif
}

bool DmdOutput::hasHdDisplay() const
{
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    return m_impl->dmd && m_impl->dmd->HasHDDisplay();
#else
    return false;
#endif
}

void DmdOutput::show(const document::Rgb565Frame& picture)
{
    Impl& impl = *m_impl;
    if (!isRunning() || !picture.isValid()) {
        return;
    }
    if (impl.lastWidth == picture.size.width && impl.lastHeight == picture.size.height &&
        std::equal(impl.lastPixels.begin(), impl.lastPixels.end(), picture.pixels.begin(),
                   picture.pixels.end())) {
        return;
    }
    impl.lastPixels.assign(picture.pixels.begin(), picture.pixels.end());
    impl.lastWidth = picture.size.width;
    impl.lastHeight = picture.size.height;
#ifdef COLORIZINGDMD_HAS_DMDUTIL
    impl.dmd->UpdateRGB16Data(impl.lastPixels.data(),
                              static_cast<uint16_t>(impl.lastWidth),
                              static_cast<uint16_t>(impl.lastHeight));
#endif
    ++impl.sent;
}

std::size_t DmdOutput::picturesSent() const
{
    return m_impl->sent;
}

}  // namespace colorizing::app
