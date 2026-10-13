#include "FrameImageUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <QPainter>

#include "render/ProjectGeometry.h"

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

struct ShadowDirection {
    uint8_t mask = 0;
    int dx = 0;
    int dy = 0;
};

constexpr std::array<ShadowDirection, 8> kShadowDirections{{
    {0x01, -1, -1},
    {0x02, 0, -1},
    {0x04, 1, -1},
    {0x08, 1, 0},
    {0x10, 1, 1},
    {0x20, 0, 1},
    {0x40, -1, 1},
    {0x80, -1, 0},
}};

double clampedPixelCoverage(DmdRenderOptions options)
{
    return std::clamp(options.pixelCoverage, 0.1, 1.0);
}

struct SpriteMatch {
    std::size_t spriteIndex = 0;
    int frameX = 0;
    int frameY = 0;
    int spriteX = 0;
    int spriteY = 0;
    int width = 0;
    int height = 0;
};

render::Size spriteVisibleSize(const document::SpriteRecord& sprite)
{
    render::Size size{0, 0};
    for (int y = 0; y < sprite.originalSd.size.height; ++y) {
        for (int x = 0; x < sprite.originalSd.size.width; ++x) {
            if (sprite.originalSd.pixels[indexedPixelIndex(sprite.originalSd, x, y)] < 255) {
                size.width = std::max(size.width, x + 1);
                size.height = std::max(size.height, y + 1);
            }
        }
    }
    return size;
}

uint32_t originalDwordAt(const document::IndexedFrame& original, int x, int y)
{
    return static_cast<uint32_t>(original.pixels[indexedPixelIndex(original, x, y)]) |
           (static_cast<uint32_t>(original.pixels[indexedPixelIndex(original, x + 1, y)]) << 8) |
           (static_cast<uint32_t>(original.pixels[indexedPixelIndex(original, x + 2, y)]) << 16) |
           (static_cast<uint32_t>(original.pixels[indexedPixelIndex(original, x + 3, y)]) << 24);
}

bool spriteDetectionAreaMatches(const document::IndexedFrame& original,
                                const document::SpriteRecord& sprite,
                                const document::SpriteDetectionArea& area,
                                int frameX,
                                int frameY)
{
    for (int y = 0; y < static_cast<int>(area.height); ++y) {
        for (int x = 0; x < static_cast<int>(area.width); ++x) {
            const int spriteX = static_cast<int>(area.x) + x;
            const int spriteY = static_cast<int>(area.y) + y;
            const uint8_t spriteValue =
                sprite.originalSd.pixels[indexedPixelIndex(sprite.originalSd, spriteX, spriteY)];
            if (spriteValue == 255) {
                continue;
            }
            const int originalX = frameX + x;
            const int originalY = frameY + y;
            if (originalX < 0 || originalY < 0 ||
                originalX >= original.size.width || originalY >= original.size.height ||
                spriteValue !=
                    original.pixels[indexedPixelIndex(original, originalX, originalY)]) {
                return false;
            }
        }
    }
    return true;
}

bool hasEquivalentSpriteMatch(const std::vector<SpriteMatch>& matches, const SpriteMatch& match)
{
    return std::any_of(matches.begin(), matches.end(), [&](const SpriteMatch& existing) {
        return existing.spriteIndex == match.spriteIndex &&
               existing.frameX == match.frameX &&
               existing.frameY == match.frameY &&
               existing.width == match.width &&
               existing.height == match.height;
    });
}

std::vector<SpriteMatch> matchFrameSprites(const document::ProjectDocument& document,
                                           std::size_t frameIndex)
{
    std::vector<SpriteMatch> matches;
    const std::vector<document::FrameSpriteReference>& references =
        document.frameSpriteReferences(frameIndex);
    // Sprites are detected on the original frame at its own resolution, as libserum does.
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    for (const document::FrameSpriteReference& reference : references) {
        if (reference.spriteId == 255 ||
            reference.spriteId >= document.spriteCount()) {
            continue;
        }
        const document::SpriteRecord& sprite = document.sprite(reference.spriteId);
        const render::Size visibleSize = spriteVisibleSize(sprite);
        if (visibleSize.width <= 0 || visibleSize.height <= 0) {
            continue;
        }
        const int minX = std::clamp(static_cast<int>(reference.minX), 0, original.size.width - 1);
        const int minY = std::clamp(static_cast<int>(reference.minY), 0, original.size.height - 1);
        const int maxX =
            std::clamp(static_cast<int>(reference.maxX), minX, original.size.width - 1);
        const int maxY =
            std::clamp(static_cast<int>(reference.maxY), minY, original.size.height - 1);
        if (maxX - minX + 1 < 4) {
            continue;
        }

        for (std::size_t areaIndex = 0; areaIndex < sprite.detectionAreas.size(); ++areaIndex) {
            const document::SpriteDetectionArea& area = sprite.detectionAreas[areaIndex];
            if (area.x == 0xffff || area.y == 0xffff ||
                area.width == 0xffff || area.height == 0xffff ||
                area.width == 0 || area.height == 0 ||
                areaIndex >= sprite.detectionDwords.size() ||
                areaIndex >= sprite.detectionDwordPositions.size() ||
                sprite.detectionDwords[areaIndex] == 0xffffffff ||
                sprite.detectionDwordPositions[areaIndex] == 0xffff) {
                continue;
            }
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX - 3; ++x) {
                    if (originalDwordAt(original, x, y) != sprite.detectionDwords[areaIndex]) {
                        continue;
                    }

                    const int spriteDwordPosition =
                        static_cast<int>(sprite.detectionDwordPositions[areaIndex]);
                    const int spriteDwordX =
                        spriteDwordPosition % static_cast<int>(document::kMaxSpriteWidth);
                    const int spriteDwordY =
                        spriteDwordPosition / static_cast<int>(document::kMaxSpriteWidth);
                    const int areaX = static_cast<int>(area.x);
                    const int areaY = static_cast<int>(area.y);
                    const int areaWidth = static_cast<int>(area.width);
                    const int areaHeight = static_cast<int>(area.height);
                    if ((x - minX < spriteDwordX - areaX) ||
                        (y - minY < spriteDwordY - areaY)) {
                        continue;
                    }

                    const int areaFrameX = x - spriteDwordX + areaX;
                    const int areaFrameY = y - spriteDwordY + areaY;
                    if (areaFrameX + areaWidth > maxX ||
                        areaFrameY + areaHeight > maxY ||
                        !spriteDetectionAreaMatches(original,
                                                    sprite,
                                                    area,
                                                    areaFrameX,
                                                    areaFrameY)) {
                        continue;
                    }

                    SpriteMatch match;
                    match.spriteIndex = reference.spriteId;
                    if (x - minX < spriteDwordX) {
                        match.spriteX = spriteDwordX - (x - minX);
                        match.frameX = minX;
                        match.width = std::min(maxX - minX + 1,
                                               visibleSize.width - match.spriteX);
                    } else {
                        match.spriteX = 0;
                        match.frameX = x - spriteDwordX;
                        match.width = std::min(maxX + 1 - match.frameX,
                                               visibleSize.width);
                    }
                    if (y - minY < spriteDwordY) {
                        match.spriteY = spriteDwordY - (y - minY);
                        match.frameY = minY;
                        match.height = std::min(maxY - minY + 1,
                                                visibleSize.height - match.spriteY);
                    } else {
                        match.spriteY = 0;
                        match.frameY = y - spriteDwordY;
                        match.height = std::min(maxY + 1 - match.frameY,
                                                visibleSize.height);
                    }

                    if (match.width > 0 && match.height > 0 &&
                        !hasEquivalentSpriteMatch(matches, match)) {
                        matches.push_back(match);
                    }
                }
            }
        }
    }
    return matches;
}

// factor: plane pixels per original pixel, see render::ProjectGeometry::planeScale().
void applySpriteMatch(document::Rgb565Frame& frame,
                      const document::SpriteRecord& sprite,
                      const SpriteMatch& match,
                      render::Resolution resolution,
                      int factor,
                      bool hdOnly)
{
    // The SD sprite of an HD-only project is never drawn; the runtime shows nothing for a
    // frame with such a sprite.
    if (hdOnly && !sprite.hasHd) {
        return;
    }
    const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
    const document::IndexedFrame& mask = useHd ? sprite.maskHd : sprite.originalSd;
    const document::Rgb565Frame& colors = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    const int targetX = match.frameX * factor;
    const int targetY = match.frameY * factor;
    const int sourceX = match.spriteX * factor;
    const int sourceY = match.spriteY * factor;
    const int width = match.width * factor;
    const int height = match.height * factor;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int frameX = targetX + x;
            const int frameY = targetY + y;
            const int spriteX = sourceX + x;
            const int spriteY = sourceY + y;
            if (frameX < 0 || frameY < 0 ||
                frameX >= frame.size.width || frameY >= frame.size.height ||
                spriteX < 0 || spriteY < 0 ||
                spriteX >= mask.size.width || spriteY >= mask.size.height) {
                continue;
            }
            const std::size_t spriteIndex = indexedPixelIndex(mask, spriteX, spriteY);
            if (mask.pixels[spriteIndex] == 255) {
                continue;
            }
            frame.pixels[rgb565PixelIndex(frame, frameX, frameY)] =
                colors.pixels[rgb565PixelIndex(colors, spriteX, spriteY)];
        }
    }
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
    const render::Point originalPoint =
        document.geometry().toRomFrame(resolution, render::Point{x, y});
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

void applyDynamicShadows(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         render::Resolution resolution,
                         document::Rgb565Frame& frame)
{
    const std::optional<document::FrameDynamicData>& dynamicData =
        document.dynamicData(frameIndex);
    if (!dynamicData) {
        return;
    }

    const document::IndexedFrame& dynamicMask =
        resolution == render::Resolution::Hd ? dynamicData->maskHd : dynamicData->maskSd;
    const std::vector<uint8_t>& directions =
        resolution == render::Resolution::Hd ? dynamicData->shadowDirectionsHd
                                             : dynamicData->shadowDirectionsSd;
    const std::vector<uint16_t>& colors =
        resolution == render::Resolution::Hd ? dynamicData->shadowColorsHd
                                             : dynamicData->shadowColorsSd;
    if (directions.empty() || colors.empty()) {
        return;
    }

    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    const render::ProjectGeometry& geometry = document.geometry();
    std::vector<uint8_t> occupied(frame.pixels.size(), 0);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const uint8_t dynamicSet = dynamicMask.pixels[indexedPixelIndex(dynamicMask, x, y)];
            const render::Point originalPoint =
                geometry.toRomFrame(resolution, render::Point{x, y});
            const uint8_t originalValue =
                original.pixels[indexedPixelIndex(original, originalPoint.x, originalPoint.y)];
            if (dynamicSet < document::kMaxDynamicSetsPerFrame && originalValue > 0) {
                occupied[rgb565PixelIndex(frame, x, y)] = 1;
            }
        }
    }

    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const uint8_t dynamicSet = dynamicMask.pixels[indexedPixelIndex(dynamicMask, x, y)];
            const render::Point originalPoint =
                geometry.toRomFrame(resolution, render::Point{x, y});
            const uint8_t originalValue =
                original.pixels[indexedPixelIndex(original, originalPoint.x, originalPoint.y)];
            if (dynamicSet >= document::kMaxDynamicSetsPerFrame ||
                originalValue == 0 ||
                dynamicSet >= directions.size() ||
                dynamicSet >= colors.size()) {
                continue;
            }
            const uint8_t directionMask = directions[dynamicSet];
            if (directionMask == 0) {
                continue;
            }
            for (const ShadowDirection& direction : kShadowDirections) {
                if ((directionMask & direction.mask) == 0) {
                    continue;
                }
                const int shadowX = x + direction.dx;
                const int shadowY = y + direction.dy;
                if (shadowX < 0 || shadowY < 0 ||
                    shadowX >= frame.size.width || shadowY >= frame.size.height) {
                    continue;
                }
                const std::size_t shadowIndex = rgb565PixelIndex(frame, shadowX, shadowY);
                if (occupied[shadowIndex]) {
                    continue;
                }
                frame.pixels[shadowIndex] = colors[dynamicSet];
                occupied[shadowIndex] = 1;
            }
        }
    }
}

document::Rgb565Frame composeResolvedFrame(const document::ProjectDocument& document,
                                           std::size_t frameIndex,
                                           render::Resolution resolution,
                                           bool includeBackgroundLayer)
{
    const int spriteFactor = document.geometry().planeScale(resolution);
    if (document.rendersHdPlaneOnly() && !document.hasHdFrame(frameIndex)) {
        return document::makeRgb565Frame(document.geometry().planeSize(resolution));
    }
    const document::Rgb565Frame& source = document.colorizedFrame(frameIndex, resolution);
    document::Rgb565Frame frame = document::makeRgb565Frame(source.size);
    for (int y = 0; y < source.size.height; ++y) {
        for (int x = 0; x < source.size.width; ++x) {
            frame.pixels[rgb565PixelIndex(frame, x, y)] =
                resolvedFramePixel(document,
                                   frameIndex,
                                   resolution,
                                   x,
                                   y,
                                   includeBackgroundLayer);
        }
    }

    applyDynamicShadows(document, frameIndex, resolution, frame);

    const std::vector<SpriteMatch> matches = matchFrameSprites(document, frameIndex);
    for (const SpriteMatch& match : matches) {
        if (match.spriteIndex < document.spriteCount()) {
            applySpriteMatch(frame,
                             document.sprite(match.spriteIndex),
                             match,
                             resolution,
                             spriteFactor,
                             document.rendersHdPlaneOnly());
        }
    }
    return frame;
}

void applyColorRotations(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         render::Resolution resolution,
                         int elapsedMs,
                         document::Rgb565Frame& frame)
{
    if (elapsedMs <= 0) {
        return;
    }

    std::vector<std::pair<uint16_t, uint16_t>> replacements;
    replacements.reserve(document::kMaxColorRotations * document::kMaxColorRotationColors);
    for (std::size_t slotIndex = 0; slotIndex < document::kMaxColorRotations; ++slotIndex) {
        const uint16_t length = document.colorRotationLength(frameIndex, resolution, slotIndex);
        const uint16_t delayMs = document.colorRotationDelayMs(frameIndex, resolution, slotIndex);
        if (length < 2 || delayMs == 0) {
            continue;
        }
        const std::size_t step =
            static_cast<std::size_t>(elapsedMs / delayMs) % static_cast<std::size_t>(length);
        if (step == 0) {
            continue;
        }
        std::vector<uint16_t> colors(length);
        for (std::size_t colorIndex = 0; colorIndex < colors.size(); ++colorIndex) {
            colors[colorIndex] =
                document.colorRotationColor(frameIndex, resolution, slotIndex, colorIndex);
        }
        for (std::size_t colorIndex = 0; colorIndex < colors.size(); ++colorIndex) {
            replacements.push_back({
                colors[colorIndex],
                colors[(colorIndex + step) % colors.size()],
            });
        }
    }
    if (replacements.empty()) {
        return;
    }

    for (uint16_t& pixel : frame.pixels) {
        for (const auto& replacement : replacements) {
            if (pixel == replacement.first) {
                pixel = replacement.second;
                break;
            }
        }
    }
}

void paintDmdPixel(QPainter& painter,
                   double x,
                   double y,
                   double cellSize,
                   const QColor& color,
                   const DmdRenderOptions& options)
{
    const QRectF cellRect =
        painter.transform().mapRect(QRectF(x, y, cellSize, cellSize));
    const double coverage = clampedPixelCoverage(options);

    painter.save();
    painter.resetTransform();
    if (options.pixelShape == DmdPixelShape::Dot) {
        const double pixelSize = std::max(1.0, cellRect.width() * coverage);
        const QPointF center = cellRect.center();
        const QRectF pixelRect(center.x() - pixelSize / 2.0,
                               center.y() - pixelSize / 2.0,
                               pixelSize,
                               pixelSize);
        painter.setBrush(color);
        painter.drawEllipse(pixelRect);
        painter.restore();
        return;
    }

    const double pixelSize = std::max(1.0, cellRect.width() * coverage);
    const QPointF center = cellRect.center();
    const QRectF pixelRect(center.x() - pixelSize / 2.0,
                           center.y() - pixelSize / 2.0,
                           pixelSize,
                           pixelSize);
    painter.fillRect(pixelRect, color);
    painter.restore();
}

QImage renderRgb565FrameImage(const document::Rgb565Frame& frame,
                              int pixelScale,
                              DmdRenderOptions options)
{
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, options.pixelShape == DmdPixelShape::Dot);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            paintDmdPixel(painter,
                          x * scale,
                          y * scale,
                          scale,
                          rgb565ToColor(frame.pixels[index]),
                          options);
        }
    }
    return image;
}

QImage renderResolvedFrameImage(const document::ProjectDocument& document,
                                std::size_t frameIndex,
                                render::Resolution resolution,
                                int pixelScale,
                                bool includeBackgroundLayer,
                                DmdRenderOptions options)
{
    const document::Rgb565Frame frame =
        composeResolvedFrame(document, frameIndex, resolution, includeBackgroundLayer);
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, options.pixelShape == DmdPixelShape::Dot);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            paintDmdPixel(painter,
                          x * scale,
                          y * scale,
                          scale,
                          rgb565ToColor(frame.pixels[index]),
                          options);
        }
    }
    return image;
}

QImage renderIndexedFrameImage(const document::IndexedFrame& frame,
                               int pixelScale,
                               DmdRenderOptions options)
{
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, options.pixelShape == DmdPixelShape::Dot);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            paintDmdPixel(painter,
                          x * scale,
                          y * scale,
                          scale,
                          originalIndexToColor(frame.pixels[index]),
                          options);
        }
    }
    return image;
}

QImage renderSpriteColorizedPreviewImage(const document::SpriteRecord& sprite,
                                         render::Resolution resolution,
                                         bool hdOnly)
{
    const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
    const document::IndexedFrame& mask = useHd ? sprite.maskHd : sprite.originalSd;
    const document::Rgb565Frame& colorized = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    QImage image(static_cast<int>(document::kMaxSpriteWidth),
                 static_cast<int>(document::kMaxSpriteHeight),
                 QImage::Format_ARGB32);
    image.fill(Qt::black);
    if (hdOnly && !useHd) {
        return image;
    }
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * document::kMaxSpriteWidth +
                static_cast<std::size_t>(x);
            if (mask.pixels[index] == 255) {
                continue;
            }
            image.setPixelColor(x, y, rgb565ToColor(colorized.pixels[index]));
        }
    }
    return image;
}

QImage renderBackgroundPreviewImage(const document::BackgroundRecord& background,
                                    render::Resolution resolution,
                                    const render::ProjectGeometry& geometry)
{
    const bool useHd = resolution == render::Resolution::Hd && background.colorizedHd;
    if (geometry.layout() == render::PlaneLayout::NativeExtra && !useHd) {
        const render::Size size = geometry.planeSize(render::Resolution::Hd);
        QImage image(size.width, size.height, QImage::Format_ARGB32);
        image.fill(Qt::black);
        return image;
    }
    const document::Rgb565Frame& frame =
        useHd ? *background.colorizedHd : background.colorizedSd;
    QImage image(frame.size.width, frame.size.height, QImage::Format_ARGB32);
    image.fill(Qt::black);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            image.setPixelColor(x, y, rgb565ToColor(frame.pixels[index]));
        }
    }
    return image;
}

}  // namespace colorizing::app
