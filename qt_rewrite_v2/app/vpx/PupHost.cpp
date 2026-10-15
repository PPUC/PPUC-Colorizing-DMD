#include "vpx/PupHost.h"

#include "vpx/VpxHost.h"

#ifdef COLORIZINGDMD_HAS_VPX_PLUGINS

#include <QCoreApplication>
#include <QDir>
#include <QPainter>
#include <QStandardPaths>
#include <QTransform>

#include <chrono>
#include <cstring>
#include <vector>

#include "plugins/B2SPluginEventStream.h"
#include "plugins/ControllerPlugin.h"
#include "plugins/VPXPlugin.h"
#include "vpxhost/PluginBus.h"

namespace colorizing::vpx {

using PinballPlugin::Controller::CtrlItemProvider;

namespace {

constexpr const char* kPupId = "PUP";
// The trigger message of the editor's DMD event source, in the "Serum" namespace as PUP
// expects it; carries a pointer to the trigger ID.
constexpr const char* kTriggerMessage = "ColorizingDMDTrigger:1";

// A texture PUP made through the VPX API: the pixels as given (GetTextureInfo hands them out)
// and as a picture to draw.
struct Texture {
    VPXTextureInfo info{};
    std::vector<uint8_t> bytes;
    QImage image;
};

void MSGPIAPI updateTexture(VPXTexture* texture, int width, int height, VPXTextureFormat format,
                            const void* data)
{
    if (!texture || width <= 0 || height <= 0 || !data) {
        return;
    }
    auto* target = static_cast<Texture*>(*texture);
    if (!target) {
        target = new Texture;
        *texture = target;
    }
    std::size_t pixelBytes = 4;
    QImage::Format qtFormat = QImage::Format_RGBA8888;
    switch (format) {
        case VPXTEXFMT_sRGBA8:
            break;
        case VPXTEXFMT_sRGB8:
            pixelBytes = 3;
            qtFormat = QImage::Format_RGB888;
            break;
        case VPXTEXFMT_sRGB565:
            pixelBytes = 2;
            qtFormat = QImage::Format_RGB16;
            break;
        case VPXTEXFMT_BW32F:
            pixelBytes = 4;
            qtFormat = QImage::Format_Grayscale8;
            break;
    }
    const std::size_t size = static_cast<std::size_t>(width) * height * pixelBytes;
    target->bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    target->info = VPXTextureInfo{static_cast<unsigned int>(width), static_cast<unsigned int>(height),
                                  format, target->bytes.data()};
    if (format == VPXTEXFMT_BW32F) {
        QImage gray(width, height, QImage::Format_Grayscale8);
        const auto* values = reinterpret_cast<const float*>(target->bytes.data());
        for (int y = 0; y < height; ++y) {
            uchar* line = gray.scanLine(y);
            for (int x = 0; x < width; ++x) {
                const float value = values[static_cast<std::size_t>(y) * width + x];
                line[x] = static_cast<uchar>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            }
        }
        target->image = gray;
    } else {
        target->image = QImage(target->bytes.data(), width, height,
                               static_cast<qsizetype>(width * pixelBytes), qtFormat)
                            .copy();
    }
}

VPXTextureInfo* MSGPIAPI textureInfo(VPXTexture texture)
{
    return texture ? &static_cast<Texture*>(texture)->info : nullptr;
}

void MSGPIAPI deleteTexture(VPXTexture texture)
{
    delete static_cast<Texture*>(texture);
}

VPXTexture MSGPIAPI createTexture(uint8_t* rawData, int size)
{
    // Encoded pictures (PNG, WebP, ...): what Qt can read.
    QImage image;
    if (!rawData || size <= 0 || !image.loadFromData(rawData, size)) {
        return nullptr;
    }
    image = image.convertToFormat(QImage::Format_RGBA8888);
    VPXTexture texture = nullptr;
    updateTexture(&texture, image.width(), image.height(), VPXTEXFMT_sRGBA8, image.constBits());
    return texture;
}

// The places PUP is told: the editor's folders, and a table "in" the PUP folder, so that its
// search beside the table finds <PUP folder>/pupvideos as well.
std::string g_appPath;
std::string g_prefPath;
std::string g_tablePath;
std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

void MSGPIAPI vpxInfo(VPXInfo* info)
{
    if (info) {
        info->path = g_appPath.c_str();
        info->prefPath = g_prefPath.c_str();
    }
}

void MSGPIAPI tableInfo(VPXTableInfo* info)
{
    if (info) {
        info->path = g_tablePath.empty() ? nullptr : g_tablePath.c_str();
        info->tableWidth = 0.0f;
        info->tableHeight = 0.0f;
    }
}

unsigned int MSGPIAPI pushNotification(const char*, const int)
{
    return 0;
}
void MSGPIAPI updateNotification(const unsigned int, const char*, const int) {}
void MSGPIAPI disableStaticPrerendering(const int) {}
void MSGPIAPI activeViewSetup(VPXViewSetupDef*) {}
void MSGPIAPI inputState(VPXInputState*) {}
void MSGPIAPI runScript(const char*) {}

double MSGPIAPI gameTime()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

VPXPluginAPI g_vpxApi{
    1,
    &vpxInfo,
    &tableInfo,
    &pushNotification,
    &updateNotification,
    &disableStaticPrerendering,
    &activeViewSetup,
    &activeViewSetup,
    &inputState,
    &inputState,
    &gameTime,
    &createTexture,
    &updateTexture,
    &textureInfo,
    &deleteTexture,
    &runScript,
};

void MSGPIAPI onGetVpxApi(const unsigned int, void*, void* msgData)
{
    if (msgData) {
        *static_cast<VPXPluginAPI**>(msgData) = &g_vpxApi;
    }
}

// Drawing for a renderer: the context's rendererData is the painter.
void MSGPIAPI drawImage(VPXRenderContext2D* ctx, VPXTexture texture, const float, const float,
                        const float, const float alpha, const float texX, const float texY,
                        const float texW, const float texH, const float pivotX,
                        const float pivotY, const float rotation, const float srcX,
                        const float srcY, const float srcW, const float srcH)
{
    auto* painter = static_cast<QPainter*>(ctx->rendererData);
    const auto* source = static_cast<const Texture*>(texture);
    if (!painter || !source || source->image.isNull() || ctx->srcWidth <= 0 || ctx->srcHeight <= 0) {
        return;
    }
    // Source surface coordinates to the image.
    const double scaleX = ctx->outWidth / ctx->srcWidth;
    const double scaleY = ctx->outHeight / ctx->srcHeight;
    const QRectF target(srcX * scaleX, srcY * scaleY, srcW * scaleX, srcH * scaleY);
    painter->save();
    painter->setOpacity(std::clamp(static_cast<double>(alpha), 0.0, 1.0));
    if (rotation != 0.0f) {
        const QPointF pivot(target.left() + pivotX * scaleX, target.top() + pivotY * scaleY);
        painter->translate(pivot);
        painter->rotate(rotation);
        painter->translate(-pivot);
    }
    painter->drawImage(target, source->image, QRectF(texX, texY, texW, texH));
    painter->restore();
}

void MSGPIAPI drawDisplay(VPXRenderContext2D*, VPXDisplayRenderStyle, VPXTexture, const float,
                          const float, const float, const float, const float, const float,
                          const float, const float, const float, const float, const float,
                          VPXTexture, const float, const float, const float, const float,
                          const float, const float, const float, const float, const float,
                          const float, const float, const float, const float)
{
}

void MSGPIAPI drawSegDisplay(VPXRenderContext2D*, VPXSegDisplayRenderStyle, VPXSegDisplayHint,
                             VPXTexture, const float, const float, const float, const float,
                             const float, const float, const float, const float, const float,
                             const float, const float, SegElementType, const float*,
                             const float, const float, const float, const float, const float,
                             const float, const float, const float, const float, const float,
                             const float, const float, const float)
{
}

}  // namespace

struct PupHost::Impl {
    const MsgPluginAPI* api = nullptr;
    uint32_t endpoint = 0;
    bool running = false;
    std::string gameId;
    unsigned int getVpxApiId = 0;
    unsigned int getAuxRendererId = 0;
    unsigned int gameEndId = 0;
    unsigned int triggerId = 0;
    std::unique_ptr<CtrlItemProvider<ControllerDef>> controller;
    std::unique_ptr<CtrlItemProvider<DMDEventSrcId>> dmdEvents;
};

PupHost::PupHost()
    : m_impl(std::make_unique<Impl>())
{
}

PupHost::~PupHost()
{
    stop();
}

bool PupHost::isAvailable()
{
    return Host::isBuiltIn() && Host::instance().hasPlugin(kPupId);
}

bool PupHost::start(const std::string& pupFolder, const std::string& game, std::string* error)
{
    stop();
    Host& host = Host::instance();
    if (!host.open(error)) {
        return false;
    }
    Impl& impl = *m_impl;
    const vpxhost::PluginBus& bus = *host.bus();
    impl.api = &bus.Api();
    impl.endpoint = bus.HostEndpointId();
    g_appPath = QCoreApplication::applicationDirPath().toStdString();
    g_prefPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString();
    g_tablePath = QDir(QString::fromStdString(pupFolder))
                      .filePath(QStringLiteral("ColorizingDMD.vpx"))
                      .toStdString();
    g_start = std::chrono::steady_clock::now();

    // The VPX API first: PUP asks for it while it loads.
    impl.getVpxApiId = impl.api->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
    impl.api->SubscribeMsg(impl.endpoint, impl.getVpxApiId, &onGetVpxApi, nullptr);
    impl.getAuxRendererId = impl.api->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_AUX_RENDERER);
    impl.gameEndId = impl.api->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_GAME_END);
    impl.triggerId = impl.api->GetMsgID("Serum", kTriggerMessage);
    impl.running = true;
    if (!host.load(kPupId, {{"PUPFolder", pupFolder}}, error)) {
        stop();
        return false;
    }
    // The game's DMD event source before the game: PUP looks for one when it takes the game.
    impl.gameId = "pinmame::" + game;
    const ControllerDef controller{impl.endpoint, impl.gameId.c_str()};
    impl.dmdEvents = std::make_unique<CtrlItemProvider<DMDEventSrcId>>(
        impl.api, impl.endpoint, DMDESPI_GET_SRC_MSG, DMDESPI_ON_SRC_CHG_MSG);
    impl.dmdEvents->SetItem(DMDEventSrcId{controller, kTriggerMessage});
    impl.controller = std::make_unique<CtrlItemProvider<ControllerDef>>(
        impl.api, impl.endpoint, CTLPI_CONTROLLERS_GET_MSG, CTLPI_CONTROLLERS_ON_CHG_MSG);
    impl.controller->SetItem(controller);
    host.process();
    return true;
}

void PupHost::stop()
{
    Impl& impl = *m_impl;
    if (!impl.running) {
        return;
    }
    // The game ends: PUP unloads the pack.
    impl.api->BroadcastMsg(impl.endpoint, impl.gameEndId, nullptr);
    impl.controller.reset();
    impl.dmdEvents.reset();
    Host::instance().unload(kPupId);
    impl.api->UnsubscribeMsg(impl.getVpxApiId, &onGetVpxApi, nullptr);
    for (unsigned int id : {impl.getVpxApiId, impl.getAuxRendererId, impl.gameEndId, impl.triggerId}) {
        impl.api->ReleaseMsgID(id);
    }
    impl = Impl{};
}

bool PupHost::isRunning() const
{
    return m_impl->running;
}

void PupHost::trigger(unsigned int id)
{
    Impl& impl = *m_impl;
    if (impl.running) {
        impl.api->BroadcastMsg(impl.endpoint, impl.triggerId, &id);
    }
}

void PupHost::process()
{
    if (m_impl->running) {
        Host::instance().process();
    }
}

bool PupHost::render(Screen screen, QImage& image)
{
    Impl& impl = *m_impl;
    if (!impl.running || image.isNull()) {
        return false;
    }
    const VPXWindowId window = screen == Screen::Topper      ? VPXWINDOW_Topper
                               : screen == Screen::Backglass ? VPXWINDOW_Backglass
                                                             : VPXWINDOW_ScoreView;
    AncillaryRendererDef entries[4] = {};
    GetAncillaryRendererMsg request{window, 4, 0, entries};
    impl.api->BroadcastMsg(impl.endpoint, impl.getAuxRendererId, &request);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    bool drawn = false;
    for (unsigned int index = 0; index < std::min(request.count, 4u); ++index) {
        VPXRenderContext2D context{};
        context.window = window;
        context.srcWidth = static_cast<float>(image.width());
        context.srcHeight = static_cast<float>(image.height());
        context.is2D = 1;
        context.outWidth = static_cast<float>(image.width());
        context.outHeight = static_cast<float>(image.height());
        context.DrawImage = &drawImage;
        context.DrawDisplay = &drawDisplay;
        context.DrawSegDisplay = &drawSegDisplay;
        context.rendererData = &painter;
        drawn = entries[index].Render(&context, entries[index].context) != 0 || drawn;
    }
    return drawn;
}

}  // namespace colorizing::vpx

#else  // no plugin hosting in this build

namespace colorizing::vpx {

struct PupHost::Impl {
};

PupHost::PupHost()
    : m_impl(std::make_unique<Impl>())
{
}

PupHost::~PupHost() = default;

bool PupHost::isAvailable()
{
    return false;
}

bool PupHost::start(const std::string&, const std::string&, std::string* error)
{
    if (error) {
        *error = "this build has no VPX plugin hosting";
    }
    return false;
}

void PupHost::stop() {}

bool PupHost::isRunning() const
{
    return false;
}

void PupHost::trigger(unsigned int) {}

void PupHost::process() {}

bool PupHost::render(Screen, QImage&)
{
    return false;
}

}  // namespace colorizing::vpx

#endif
