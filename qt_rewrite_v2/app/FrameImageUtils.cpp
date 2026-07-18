#include "FrameImageUtils.h"

#include <algorithm>

#include <QPainter>

namespace colorizing::app {

namespace {

std::size_t rgb565PixelIndex(const document::Rgb565Frame& frame, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
           static_cast<std::size_t>(x);
}

std::size_t indexedPixelIndex(const document::IndexedFrame& frame, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
           static_cast<std::size_t>(x);
}

render::Point originalPointForResolution(render::Resolution resolution, int x, int y)
{
    if (resolution == render::Resolution::Hd) {
        return render::Point{x / 2, y / 2};
    }
    return render::Point{x, y};
}

}  // namespace

QColor rgb565ToColor(uint16_t value)
{
    const int r5 = (value >> 11) & 0x1f;
    const int g6 = (value >> 5) & 0x3f;
    const int b5 = value & 0x1f;
    return QColor((r5 * 255) / 31, (g6 * 255) / 63, (b5 * 255) / 31);
}

QColor originalIndexToColor(uint8_t value)
{
    switch (value & 0x03) {
    case 0:
        return QColor(22, 18, 12);
    case 1:
        return QColor(110, 54, 8);
    case 2:
        return QColor(210, 112, 16);
    default:
        return QColor(255, 184, 48);
    }
}

uint16_t resolvedFramePixel(const document::ProjectDocument& document,
                            std::size_t frameIndex,
                            render::Resolution resolution,
                            int x,
                            int y,
                            bool includeBackgroundLayer)
{
    const document::Rgb565Frame& colorized = document.colorizedFrame(frameIndex, resolution);
    uint16_t color = colorized.pixels[rgb565PixelIndex(colorized, x, y)];

    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    const render::Point originalPoint = originalPointForResolution(resolution, x, y);
    const std::size_t originalIndex =
        indexedPixelIndex(original, originalPoint.x, originalPoint.y);
    const uint8_t originalValue = original.pixels[originalIndex];

    const std::optional<document::FrameBackgroundData>& backgroundData =
        document.backgroundData(frameIndex);
    bool backgroundApplied = false;
    if (includeBackgroundLayer && backgroundData &&
        backgroundData->backgroundId < document.backgroundCount() && originalValue == 0) {
        const document::BackgroundRecord& background =
            document.background(backgroundData->backgroundId);
        const document::Rgb565Frame* backgroundFrame = nullptr;
        const document::IndexedFrame* backgroundMask = nullptr;
        if (resolution == render::Resolution::Hd && background.colorizedHd) {
            backgroundFrame = &*background.colorizedHd;
            backgroundMask = &backgroundData->maskHd;
        } else if (resolution == render::Resolution::Sd) {
            backgroundFrame = &background.colorizedSd;
            backgroundMask = &backgroundData->maskSd;
        }
        if (backgroundFrame && backgroundMask &&
            backgroundMask->pixels[indexedPixelIndex(*backgroundMask, x, y)] != 0) {
            color = backgroundFrame->pixels[rgb565PixelIndex(*backgroundFrame, x, y)];
            backgroundApplied = true;
        }
    }
    if (backgroundApplied) {
        return color;
    }

    const std::optional<document::FrameDynamicData>& dynamicData =
        document.dynamicData(frameIndex);
    if (!dynamicData) {
        return color;
    }
    const document::IndexedFrame& dynamicMask =
        resolution == render::Resolution::Hd ? dynamicData->maskHd : dynamicData->maskSd;
    const uint8_t dynamicSet = dynamicMask.pixels[indexedPixelIndex(dynamicMask, x, y)];
    if (dynamicSet == 255 || dynamicSet >= document::kMaxDynamicSetsPerFrame) {
        return color;
    }

    const std::vector<uint16_t>& dynamicColors =
        resolution == render::Resolution::Hd ? dynamicData->colorsHd : dynamicData->colorsSd;
    const std::size_t colorCount = dynamicColors.size() / document::kMaxDynamicSetsPerFrame;
    if (colorCount == 0 || originalValue >= colorCount) {
        return color;
    }
    return dynamicColors[static_cast<std::size_t>(dynamicSet) * colorCount +
                         static_cast<std::size_t>(originalValue)];
}

QImage renderRgb565FrameImage(const document::Rgb565Frame& frame, int pixelScale)
{
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            painter.fillRect(QRect(x * scale, y * scale, scale, scale),
                             rgb565ToColor(frame.pixels[index]));
        }
    }
    return image;
}

QImage renderResolvedFrameImage(const document::ProjectDocument& document,
                                std::size_t frameIndex,
                                render::Resolution resolution,
                                int pixelScale,
                                bool includeBackgroundLayer)
{
    const document::Rgb565Frame& frame = document.colorizedFrame(frameIndex, resolution);
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            painter.fillRect(QRect(x * scale, y * scale, scale, scale),
                             rgb565ToColor(resolvedFramePixel(document,
                                                              frameIndex,
                                                              resolution,
                                                              x,
                                                              y,
                                                              includeBackgroundLayer)));
        }
    }
    return image;
}

QImage renderIndexedFrameImage(const document::IndexedFrame& frame, int pixelScale)
{
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            painter.fillRect(QRect(x * scale, y * scale, scale, scale),
                             originalIndexToColor(frame.pixels[index]));
        }
    }
    return image;
}

}  // namespace colorizing::app
