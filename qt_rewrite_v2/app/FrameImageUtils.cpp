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

// The value libserum compares for a pixel of the ROM frame or of a sprite's original: the shade,
// or lit/unlit for a sprite in shape mode.
uint8_t comparedValue(uint8_t value, bool shapeMode)
{
    return shapeMode && value > 0 ? uint8_t{1} : value;
}

uint32_t originalDwordAt(const document::IndexedFrame& original, int x, int y, bool shapeMode)
{
    uint32_t dword = 0;
    for (int i = 0; i < 4; ++i) {
        dword |= static_cast<uint32_t>(comparedValue(
                     original.pixels[indexedPixelIndex(original, x + i, y)], shapeMode))
                 << (8 * i);
    }
    return dword;
}

// libserum's loader does this to the detection words of a shape-mode sprite.
uint32_t comparedDetectionWord(uint32_t dword, bool shapeMode)
{
    if (!shapeMode) {
        return dword;
    }
    uint32_t compared = 0;
    for (int i = 0; i < 4; ++i) {
        if ((dword >> (8 * i)) & 0xff) {
            compared |= uint32_t{1} << (8 * i);
        }
    }
    return compared;
}

// Only the opaque pixels of the area count, as in libserum's SpriteAreaMatches().
bool spriteDetectionAreaMatches(const document::IndexedFrame& original,
                                const document::SpriteRecord& sprite,
                                const document::SpriteDetectionArea& area,
                                int frameX,
                                int frameY,
                                bool shapeMode)
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
                comparedValue(spriteValue, shapeMode) !=
                    comparedValue(
                        original.pixels[indexedPixelIndex(original, originalX, originalY)],
                        shapeMode)) {
                return false;
            }
        }
    }
    return true;
}

// The detection areas libserum keeps for a sprite (SerumData::BuildSpriteRuntimeSidecars()):
// set, not empty, inside the sprite, with at least one opaque pixel.
bool isUsableDetectionArea(const document::SpriteRecord& sprite,
                           const document::SpriteDetectionArea& area)
{
    if (area.x == 0xffff || area.width == 0 || area.height == 0 ||
        static_cast<std::size_t>(area.x) + area.width > document::kMaxSpriteWidth ||
        static_cast<std::size_t>(area.y) + area.height > document::kMaxSpriteHeight) {
        return false;
    }
    for (int y = 0; y < static_cast<int>(area.height); ++y) {
        for (int x = 0; x < static_cast<int>(area.width); ++x) {
            if (sprite.originalSd.pixels[indexedPixelIndex(sprite.originalSd,
                                                           static_cast<int>(area.x) + x,
                                                           static_cast<int>(area.y) + y)] != 255) {
                return true;
            }
        }
    }
    return false;
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

}  // namespace

std::vector<SpriteMatch> matchFrameSprites(const document::ProjectDocument& document,
                                           std::size_t frameIndex)
{
    std::vector<SpriteMatch> matches;
    const std::vector<document::FrameSpriteReference>& references =
        document.frameSpriteReferences(frameIndex);
    // Sprites are detected on the original frame at its own resolution, as libserum does.
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    std::vector<uint8_t> seenSprites;
    for (const document::FrameSpriteReference& reference : references) {
        // The frame's sprite list ends at its first empty slot.
        if (reference.spriteId == 255 || reference.spriteId >= document.spriteCount()) {
            break;
        }
        // A sprite listed twice is looked for once, with the search area of its first slot.
        if (std::find(seenSprites.begin(), seenSprites.end(), reference.spriteId) !=
            seenSprites.end()) {
            continue;
        }
        const document::SpriteRecord& sprite = document.sprite(reference.spriteId);
        const render::Size visibleSize = spriteVisibleSize(sprite);
        // libserum stores a sprite original without any shade above 0 as "no data".
        const bool hasLitPixel =
            std::any_of(sprite.originalSd.pixels.begin(),
                        sprite.originalSd.pixels.end(),
                        [](uint8_t value) { return value != 0 && value != 255; });
        const bool hasDetectionArea =
            std::any_of(sprite.detectionAreas.begin(),
                        sprite.detectionAreas.end(),
                        [&](const document::SpriteDetectionArea& area) {
                            return isUsableDetectionArea(sprite, area);
                        });
        if (visibleSize.width <= 0 || visibleSize.height <= 0 || !hasLitPixel ||
            !hasDetectionArea) {
            continue;
        }
        seenSprites.push_back(reference.spriteId);

        const int minX = static_cast<int16_t>(reference.minX);
        const int minY = static_cast<int16_t>(reference.minY);
        const int maxX = static_cast<int16_t>(reference.maxX);
        const int maxY = static_cast<int16_t>(reference.maxY);
        if (minX > maxX || minY > maxY || maxX - minX < 3) {
            continue;
        }
        const bool shapeMode = sprite.shapeMode > 0;
        // libserum scans the search area as it is stored; here it is cut to the frame.
        const int scanMinX = std::max(minX, 0);
        const int scanMinY = std::max(minY, 0);
        const int scanMaxX = std::min(maxX, original.size.width - 1);
        const int scanMaxY = std::min(maxY, original.size.height - 1);

        for (std::size_t areaIndex = 0; areaIndex < sprite.detectionAreas.size(); ++areaIndex) {
            const document::SpriteDetectionArea& area = sprite.detectionAreas[areaIndex];
            if (!isUsableDetectionArea(sprite, area) ||
                areaIndex >= sprite.detectionDwords.size() ||
                areaIndex >= sprite.detectionDwordPositions.size()) {
                continue;
            }
            const uint32_t detectionWord =
                comparedDetectionWord(sprite.detectionDwords[areaIndex], shapeMode);
            for (int y = scanMinY; y <= scanMaxY; ++y) {
                for (int x = scanMinX; x <= scanMaxX - 3; ++x) {
                    if (originalDwordAt(original, x, y, shapeMode) != detectionWord) {
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

                    // The area may end on the last column and row of the search area.
                    const int areaFrameX = x - spriteDwordX + areaX;
                    const int areaFrameY = y - spriteDwordY + areaY;
                    if (areaFrameX + areaWidth > maxX + 1 ||
                        areaFrameY + areaHeight > maxY + 1 ||
                        !spriteDetectionAreaMatches(original,
                                                    sprite,
                                                    area,
                                                    areaFrameX,
                                                    areaFrameY,
                                                    shapeMode)) {
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

                    if (!hasEquivalentSpriteMatch(matches, match)) {
                        matches.push_back(match);
                        if (matches.size() == document::kMaxSpritesPerFrame) {
                            return matches;
                        }
                    }
                }
            }
        }
    }
    return matches;
}

namespace {

// Draws a found sprite onto a plane the way libserum's Colorize_Spritev2() does.
//
// On a plane with the ROM frame's size the sprite's own arrays of that plane are used one to
// one: opacity, color, and a dynamic pixel takes its set's color for the ROM frame's shade
// there. On the 256x64 plane of a 128x32 project libserum draws the 128x32 sprite first and
// upscales it, and then puts the sprite's HD art on top except where the 128x32 sprite is
// dynamic. The upscale is done here by doubling pixels; libserum rounds edges, see the
// Handbook.
void applySpriteMatch(document::Rgb565Frame& frame,
                      const document::IndexedFrame& original,
                      const document::SpriteRecord& sprite,
                      const SpriteMatch& match,
                      render::Resolution resolution,
                      const render::ProjectGeometry& geometry,
                      uint32_t colorCount,
                      bool extraDynamicsLoaded)
{
    const bool hdOnly = geometry.layout() == render::PlaneLayout::NativeExtra;
    // The SD sprite of an HD-only project is never drawn; the runtime shows nothing for a
    // frame with such a sprite.
    if (hdOnly && !sprite.hasHd) {
        return;
    }
    const auto spriteIndexAt = [](int x, int y) {
        return static_cast<std::size_t>(y) * document::kMaxSpriteWidth +
               static_cast<std::size_t>(x);
    };
    const auto insideSprite = [](int x, int y) {
        return x >= 0 && y >= 0 && x < static_cast<int>(document::kMaxSpriteWidth) &&
               y < static_cast<int>(document::kMaxSpriteHeight);
    };
    // The color of a dynamic sprite pixel for the ROM frame pixel it lies on.
    const auto dynamicColor = [&](const std::vector<uint16_t>& colors, uint8_t set, int romX,
                                  int romY) -> uint16_t {
        const std::size_t index =
            static_cast<std::size_t>(set) * colorCount +
            original.pixels[indexedPixelIndex(original, romX, romY)];
        return index < colors.size() ? colors[index] : uint16_t{0};
    };
    const auto isDynamic = [](const document::IndexedFrame& mask, std::size_t index) {
        return index < mask.pixels.size() && mask.pixels[index] != 255;
    };
    const auto insideOriginal = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < original.size.width && y < original.size.height;
    };

    if (geometry.planeScale(resolution) == 1) {
        const bool extra = resolution == render::Resolution::Hd;
        const document::IndexedFrame& opacity = extra ? sprite.maskHd : sprite.originalSd;
        const document::Rgb565Frame& colors = extra ? sprite.colorizedHd : sprite.colorizedSd;
        const document::IndexedFrame& dynamicMask =
            extra ? sprite.dynamicMaskHd : sprite.dynamicMaskSd;
        const std::vector<uint16_t>& dynamicColors =
            extra ? sprite.dynamicColorsHd : sprite.dynamicColorsSd;
        for (int y = 0; y < match.height; ++y) {
            for (int x = 0; x < match.width; ++x) {
                const int frameX = match.frameX + x;
                const int frameY = match.frameY + y;
                const int spriteX = match.spriteX + x;
                const int spriteY = match.spriteY + y;
                if (!insideOriginal(frameX, frameY) || !insideSprite(spriteX, spriteY)) {
                    continue;
                }
                const std::size_t index = spriteIndexAt(spriteX, spriteY);
                if (opacity.pixels[index] == 255) {
                    continue;
                }
                frame.pixels[rgb565PixelIndex(frame, frameX, frameY)] =
                    (!extra || extraDynamicsLoaded) && isDynamic(dynamicMask, index)
                        ? dynamicColor(dynamicColors, dynamicMask.pixels[index], frameX, frameY)
                        : colors.pixels[index];
            }
        }
        return;
    }

    // The 256x64 plane of a 128x32 project.
    for (int y = 0; y < match.height * 2; ++y) {
        for (int x = 0; x < match.width * 2; ++x) {
            const int frameX = match.frameX * 2 + x;
            const int frameY = match.frameY * 2 + y;
            const int romX = match.frameX + x / 2;
            const int romY = match.frameY + y / 2;
            const int sdSpriteX = match.spriteX + x / 2;
            const int sdSpriteY = match.spriteY + y / 2;
            if (frameX >= frame.size.width || frameY >= frame.size.height ||
                !insideOriginal(romX, romY) || !insideSprite(sdSpriteX, sdSpriteY)) {
                continue;
            }
            const std::size_t sdIndex = spriteIndexAt(sdSpriteX, sdSpriteY);
            const bool sdOpaque = sprite.originalSd.pixels[sdIndex] != 255;
            const bool sdDynamic = sdOpaque && isDynamic(sprite.dynamicMaskSd, sdIndex);
            uint16_t& target = frame.pixels[rgb565PixelIndex(frame, frameX, frameY)];
            // The upscaled 128x32 sprite: all of it without HD art, its dynamic pixels with.
            if (sdOpaque && (sdDynamic || !sprite.hasHd)) {
                target = sdDynamic ? dynamicColor(sprite.dynamicColorsSd,
                                                  sprite.dynamicMaskSd.pixels[sdIndex],
                                                  romX,
                                                  romY)
                                   : sprite.colorizedSd.pixels[sdIndex];
            }
            const int hdSpriteX = match.spriteX * 2 + x;
            const int hdSpriteY = match.spriteY * 2 + y;
            if (!sprite.hasHd || sdDynamic || !insideSprite(hdSpriteX, hdSpriteY)) {
                continue;
            }
            const std::size_t hdIndex = spriteIndexAt(hdSpriteX, hdSpriteY);
            if (sprite.maskHd.pixels[hdIndex] != 255) {
                target = sprite.colorizedHd.pixels[hdIndex];
            }
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
            // libserum loads the extra-plane dynamic data of sprite N only if frame N has
            // extra-plane content (Serum_LoadFilev2Stream reads it with the frame flags as
            // parent).
            const bool extraDynamicsLoaded = match.spriteIndex < document.frameCount() &&
                                             document.hasHdFrame(match.spriteIndex);
            applySpriteMatch(frame,
                             document.originalFrame(frameIndex),
                             document.sprite(match.spriteIndex),
                             match,
                             resolution,
                             document.geometry(),
                             document.colorCount(),
                             extraDynamicsLoaded);
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
