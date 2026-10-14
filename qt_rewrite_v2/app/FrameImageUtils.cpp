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
                      bool extraDynamicsLoaded,
                      std::vector<uint8_t>* notRotating)
{
    // A sprite pixel takes part in the frame's color rotations whatever it covers, its
    // dynamic pixels too (Colorize_Spritev2 looks every sprite pixel's color up).
    const auto painted = [&](int frameX, int frameY) {
        if (notRotating) {
            (*notRotating)[rgb565PixelIndex(frame, frameX, frameY)] = 0;
        }
    };
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
    const auto dynamicColor = [&](const document::CompactArray<uint16_t>& colors, uint8_t set, int romX,
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
        const document::CompactArray<uint16_t>& dynamicColors =
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
                painted(frameX, frameY);
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
                painted(frameX, frameY);
            }
            const int hdSpriteX = match.spriteX * 2 + x;
            const int hdSpriteY = match.spriteY * 2 + y;
            if (!sprite.hasHd || sdDynamic || !insideSprite(hdSpriteX, hdSpriteY)) {
                continue;
            }
            const std::size_t hdIndex = spriteIndexAt(hdSpriteX, hdSpriteY);
            if (sprite.maskHd.pixels[hdIndex] != 255) {
                target = sprite.colorizedHd.pixels[hdIndex];
                painted(frameX, frameY);
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

QColor originalIndexToColor(uint8_t value, uint32_t colorCount)
{
    static const QColor kRamp[4] = {QColor(22, 18, 12), QColor(110, 54, 8),
                                    QColor(210, 112, 16), QColor(255, 184, 48)};
    if (colorCount <= 4) {
        return kRamp[value & 0x03];
    }
    // 16 shades along the same ramp: 0, 5, 10 and 15 are its four colors, the others lie
    // between them.
    const int shade = std::min<int>(value, 15);
    const int segment = std::min(shade / 5, 2);
    const int step = shade - segment * 5;
    const QColor& from = kRamp[segment];
    const QColor& to = kRamp[segment + 1];
    const auto mix = [step](int a, int b) { return a + (b - a) * step / 5; };
    return QColor(mix(from.red(), to.red()), mix(from.green(), to.green()),
                  mix(from.blue(), to.blue()));
}

namespace {

// resolvedFramePixel(), and whether the pixel is taken from a dynamic zone's color set.
uint16_t resolveFramePixel(const document::ProjectDocument& document,
                           std::size_t frameIndex,
                           render::Resolution resolution,
                           int x,
                           int y,
                           bool includeBackgroundLayer,
                           bool& fromDynamicZone)
{
    fromDynamicZone = false;
    const document::Rgb565Frame& colorized = document.colorizedFrame(frameIndex, resolution);
    const uint16_t color = colorized.pixels[rgb565PixelIndex(colorized, x, y)];

    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    const render::Point originalPoint =
        document.geometry().toRomFrame(resolution, render::Point{x, y});
    const std::size_t originalIndex =
        indexedPixelIndex(original, originalPoint.x, originalPoint.y);
    const uint8_t originalValue = original.pixels[originalIndex];

    // Dynamic zones are those of the plane with the ROM frame's size, read at the ROM frame
    // pixel. On the 256x64 plane of an ordinary project that is the 128x32 zone the pixel
    // lies in: libserum renders dynamic content at 128x32 and upscales it, and does not read
    // the 256x64 dynamic mask and colors (Colorize_Framev2, layer mode).
    const render::Resolution dynamicPlane = document.dynamicPlane();
    const bool scaledDynamics = resolution != dynamicPlane;
    const bool extraDynamics = dynamicPlane == render::Resolution::Hd;
    const std::optional<document::FrameDynamicData>& dynamicData =
        document.dynamicData(frameIndex);
    std::optional<uint16_t> dynamicColor;
    bool dynamicZone = false;
    if (dynamicData) {
        const document::IndexedFrame& dynamicMask =
            extraDynamics ? dynamicData->maskHd : dynamicData->maskSd;
        const uint8_t dynamicSet = originalIndex < dynamicMask.pixels.size()
                                       ? dynamicMask.pixels[originalIndex]
                                       : uint8_t{255};
        dynamicZone = dynamicSet != 255;
        const document::CompactArray<uint16_t>& dynamicColors =
            extraDynamics ? dynamicData->colorsHd : dynamicData->colorsSd;
        const std::size_t colorCount = dynamicColors.size() / document::kMaxDynamicSetsPerFrame;
        if (dynamicSet < document::kMaxDynamicSetsPerFrame && originalValue < colorCount) {
            dynamicColor = dynamicColors[static_cast<std::size_t>(dynamicSet) * colorCount +
                                         static_cast<std::size_t>(originalValue)];
        }
    }

    const std::optional<document::FrameBackgroundData>& backgroundData =
        document.backgroundData(frameIndex);
    const bool hasBackground = includeBackgroundLayer && backgroundData &&
                               backgroundData->backgroundId < document.backgroundCount();
    // The background shows through an unlit pixel where the plane's background mask is set.
    const auto backgroundPixel = [&]() -> std::optional<uint16_t> {
        if (!hasBackground || originalValue != 0) {
            return std::nullopt;
        }
        const document::BackgroundRecord& background =
            document.background(backgroundData->backgroundId);
        if (resolution == render::Resolution::Hd) {
            if (background.colorizedHd &&
                backgroundData->maskHd.pixels[indexedPixelIndex(backgroundData->maskHd, x, y)] !=
                    0) {
                return background.colorizedHd->pixels[rgb565PixelIndex(*background.colorizedHd,
                                                                       x,
                                                                       y)];
            }
            return std::nullopt;
        }
        if (backgroundData->maskSd.pixels[indexedPixelIndex(backgroundData->maskSd, x, y)] != 0) {
            return background.colorizedSd.pixels[rgb565PixelIndex(background.colorizedSd, x, y)];
        }
        return std::nullopt;
    };

    if (scaledDynamics) {
        // The upscaled 128x32 layer lies on top of the 256x64 frame and background. It holds
        // the dynamic zones, except where the 128x32 pass drew the 128x32 background instead
        // (unlit, 128x32 background mask set): there the 256x64 picture stays.
        const bool originalPlaneShowsBackground =
            hasBackground && originalValue == 0 && originalIndex <
                                                       backgroundData->maskSd.pixels.size() &&
            backgroundData->maskSd.pixels[originalIndex] != 0;
        if (dynamicZone && !originalPlaneShowsBackground) {
            fromDynamicZone = true;
            return dynamicColor.value_or(color);
        }
        return backgroundPixel().value_or(color);
    }

    if (const std::optional<uint16_t> background = backgroundPixel()) {
        return *background;
    }
    fromDynamicZone = dynamicZone;
    return dynamicColor.value_or(color);
}

}  // namespace

uint16_t resolvedFramePixel(const document::ProjectDocument& document,
                            std::size_t frameIndex,
                            render::Resolution resolution,
                            int x,
                            int y,
                            bool includeBackgroundLayer)
{
    bool fromDynamicZone = false;
    return resolveFramePixel(document, frameIndex, resolution, x, y, includeBackgroundLayer,
                             fromDynamicZone);
}

namespace {

// Dynamic shadows as libserum draws them. On the plane with the ROM frame's size a lit dynamic
// pixel shades its neighbors in its set's directions (CheckDynaShadow). On the 256x64 plane
// of an ordinary project the shadows are generated on that plane from the upscaled 128x32
// zones, one 256x64 pixel wide (GenerateExtraPlaneShadows, default offset); a set without a
// 128x32 shadow direction takes direction and color from the stored 256x64 tables, which is
// the one thing libserum reads of the 256x64 dynamic data there.
// A pixel that takes a shadow is marked in notRotating, if that is given.
void applyDynamicShadows(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         render::Resolution resolution,
                         document::Rgb565Frame& frame,
                         std::vector<uint8_t>* notRotating)
{
    const std::optional<document::FrameDynamicData>& dynamicData =
        document.dynamicData(frameIndex);
    if (!dynamicData) {
        return;
    }

    const render::Resolution dynamicPlane = document.dynamicPlane();
    const bool scaledDynamics = resolution != dynamicPlane;
    const bool extraDynamics = dynamicPlane == render::Resolution::Hd;
    const document::IndexedFrame& dynamicMask =
        extraDynamics ? dynamicData->maskHd : dynamicData->maskSd;
    const document::CompactArray<uint8_t>& directions =
        extraDynamics ? dynamicData->shadowDirectionsHd : dynamicData->shadowDirectionsSd;
    const document::CompactArray<uint16_t>& colors =
        extraDynamics ? dynamicData->shadowColorsHd : dynamicData->shadowColorsSd;
    // Per set: the tables of the dynamic plane, or the other plane's where the picture is the
    // scaled one and the set has no direction of its own.
    const auto shadowOf = [&](uint8_t set, uint8_t& direction, uint16_t& color) {
        direction = set < directions.size() ? directions[set] : uint8_t{0};
        color = set < colors.size() ? colors[set] : uint16_t{0};
        if (direction == 0 && scaledDynamics && set < dynamicData->shadowDirectionsHd.size()) {
            direction = dynamicData->shadowDirectionsHd[set];
            if (set < dynamicData->shadowColorsHd.size()) {
                color = dynamicData->shadowColorsHd[set];
            }
        }
    };

    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    const render::ProjectGeometry& geometry = document.geometry();
    // The set of a pixel that shows lit dynamic content; 255 for every other pixel.
    const auto litDynamicSet = [&](int x, int y) -> uint8_t {
        const render::Point originalPoint = geometry.toRomFrame(resolution, render::Point{x, y});
        const std::size_t originalIndex =
            indexedPixelIndex(original, originalPoint.x, originalPoint.y);
        const uint8_t dynamicSet = originalIndex < dynamicMask.pixels.size()
                                       ? dynamicMask.pixels[originalIndex]
                                       : uint8_t{255};
        if (dynamicSet >= document::kMaxDynamicSetsPerFrame ||
            original.pixels[originalIndex] == 0) {
            return 255;
        }
        return dynamicSet;
    };

    std::vector<uint8_t> occupied(frame.pixels.size(), 0);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            if (litDynamicSet(x, y) != 255) {
                occupied[rgb565PixelIndex(frame, x, y)] = 1;
            }
        }
    }

    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const uint8_t dynamicSet = litDynamicSet(x, y);
            if (dynamicSet == 255) {
                continue;
            }
            uint8_t directionMask = 0;
            uint16_t shadowColor = 0;
            shadowOf(dynamicSet, directionMask, shadowColor);
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
                frame.pixels[shadowIndex] = shadowColor;
                occupied[shadowIndex] = 1;
                if (notRotating) {
                    (*notRotating)[shadowIndex] = 1;
                }
            }
        }
    }
}

}  // namespace

document::Rgb565Frame composeResolvedFrame(const document::ProjectDocument& document,
                                           std::size_t frameIndex,
                                           render::Resolution resolution,
                                           bool includeBackgroundLayer,
                                           std::vector<uint8_t>* notRotating)
{
    if (document.rendersHdPlaneOnly() && !document.hasHdFrame(frameIndex)) {
        document::Rgb565Frame empty =
            document::makeRgb565Frame(document.geometry().planeSize(resolution));
        if (notRotating) {
            notRotating->assign(empty.pixels.size(), 1);
        }
        return empty;
    }
    const document::Rgb565Frame& source = document.colorizedFrame(frameIndex, resolution);
    document::Rgb565Frame frame = document::makeRgb565Frame(source.size);
    if (notRotating) {
        notRotating->assign(frame.pixels.size(), 0);
    }
    for (int y = 0; y < source.size.height; ++y) {
        for (int x = 0; x < source.size.width; ++x) {
            bool fromDynamicZone = false;
            const std::size_t index = rgb565PixelIndex(frame, x, y);
            frame.pixels[index] = resolveFramePixel(document,
                                                    frameIndex,
                                                    resolution,
                                                    x,
                                                    y,
                                                    includeBackgroundLayer,
                                                    fromDynamicZone);
            if (notRotating && fromDynamicZone) {
                (*notRotating)[index] = 1;
            }
        }
    }

    applyDynamicShadows(document, frameIndex, resolution, frame, notRotating);

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
                             extraDynamicsLoaded,
                             notRotating);
        }
    }
    return frame;
}

void applyColorRotations(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         render::Resolution resolution,
                         int elapsedMs,
                         document::Rgb565Frame& frame,
                         const std::vector<uint8_t>* notRotating)
{
    if (elapsedMs <= 0) {
        return;
    }

    // libserum's rule (SerumData::BuildColorRotationLookup, Serum_Rotate): a color belongs to
    // the first slot, and the first place in it, that lists it, whether that slot runs or
    // not; a slot runs if it has a length and a delay.
    struct Place {
        uint16_t color = 0;
        std::size_t slot = 0;
        std::size_t position = 0;
    };
    std::vector<Place> places;
    places.reserve(document::kMaxColorRotations * document::kMaxColorRotationColors);
    std::array<std::vector<uint16_t>, document::kMaxColorRotations> slotColors;
    std::array<std::size_t, document::kMaxColorRotations> slotSteps{};
    bool anyStep = false;
    for (std::size_t slotIndex = 0; slotIndex < document::kMaxColorRotations; ++slotIndex) {
        const uint16_t length = document.colorRotationLength(frameIndex, resolution, slotIndex);
        const uint16_t delayMs = document.colorRotationDelayMs(frameIndex, resolution, slotIndex);
        std::vector<uint16_t>& colors = slotColors[slotIndex];
        colors.resize(length);
        for (std::size_t colorIndex = 0; colorIndex < colors.size(); ++colorIndex) {
            colors[colorIndex] =
                document.colorRotationColor(frameIndex, resolution, slotIndex, colorIndex);
            const bool listed =
                std::any_of(places.begin(), places.end(), [&](const Place& place) {
                    return place.color == colors[colorIndex];
                });
            if (!listed) {
                places.push_back({colors[colorIndex], slotIndex, colorIndex});
            }
        }
        if (length > 0 && delayMs > 0) {
            slotSteps[slotIndex] =
                static_cast<std::size_t>(elapsedMs / delayMs) % static_cast<std::size_t>(length);
            anyStep = anyStep || slotSteps[slotIndex] != 0;
        }
    }
    if (!anyStep) {
        return;
    }

    const bool masked = notRotating && notRotating->size() == frame.pixels.size();
    for (std::size_t index = 0; index < frame.pixels.size(); ++index) {
        if (masked && (*notRotating)[index] != 0) {
            continue;
        }
        uint16_t& pixel = frame.pixels[index];
        for (const Place& place : places) {
            if (pixel == place.color) {
                const std::vector<uint16_t>& colors = slotColors[place.slot];
                pixel = colors[(place.position + slotSteps[place.slot]) % colors.size()];
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
                               DmdRenderOptions options,
                               uint32_t colorCount)
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
                          originalIndexToColor(frame.pixels[index], colorCount),
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
