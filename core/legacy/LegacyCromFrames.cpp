#include "legacy/LegacyCromFrames.h"

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyByteSource.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace colorizing::legacy {

namespace {

constexpr std::size_t kHashCodeBytes = sizeof(uint32_t);
constexpr std::size_t kRgb565Bytes = sizeof(uint16_t);
constexpr std::size_t kMaxSpriteWidth = 256;
constexpr std::size_t kMaxSpriteHeight = 64;
constexpr std::size_t kMaxColorRotations = 4;
constexpr std::size_t kMaxColorRotationLength = 64;
constexpr std::size_t kMaxSpriteDetectAreas = 4;
constexpr uint32_t kSpriteDynamicHeaderThreshold = 18 * sizeof(uint32_t);
constexpr uint32_t kSpriteShapeModeHeaderThreshold = 19 * sizeof(uint32_t);
// More than the longest header (revision 20: name, 12 values, the background count).
constexpr std::size_t kMaxHeaderBytes = 256;

LegacyCromFrameParseResult fail(std::string message)
{
    LegacyCromFrameParseResult result;
    result.errorMessage = std::move(message);
    return result;
}

std::size_t checkedProduct(uint32_t a, uint32_t b)
{
    return static_cast<std::size_t>(a) * static_cast<std::size_t>(b);
}

bool skipRequired(LegacyByteSource& cursor,
                  std::size_t bytes,
                  const char* fieldName,
                  std::string& error)
{
    if (cursor.skip(bytes)) {
        return true;
    }
    error = std::string("unexpected end of cROM while skipping ") + fieldName;
    return false;
}

// Reads `count` values into a block, which shares one buffer with every other block of the
// same values when they are all alike (see document::CompactArray). The scratch buffer is
// reused from block to block.
template <typename T>
bool readValues(LegacyByteSource& cursor,
                document::CompactArray<T>& values,
                std::size_t count,
                std::vector<T>& scratch,
                const char* fieldName,
                std::string& error)
{
    scratch.resize(count);
    bool ok = false;
    if constexpr (sizeof(T) == 1) {
        ok = cursor.readBytes(reinterpret_cast<uint8_t*>(scratch.data()), count);
    } else {
        static_assert(sizeof(T) == 2, "cROM blocks hold bytes or 16-bit values");
        ok = cursor.readU16s(reinterpret_cast<uint16_t*>(scratch.data()), count);
    }
    if (!ok) {
        error = std::string("unexpected end of cROM while reading ") + fieldName;
        return false;
    }
    values.assignValues(scratch.data(), count);
    return true;
}

bool readU32Values(LegacyByteSource& cursor,
                   std::vector<uint32_t>& values,
                   const char* fieldName,
                   std::string& error)
{
    if (!cursor.readU32s(values.data(), values.size())) {
        error = std::string("unexpected end of cROM while reading ") + fieldName;
        return false;
    }
    return true;
}

bool readU16Values(LegacyByteSource& cursor,
                   std::vector<uint16_t>& values,
                   const char* fieldName,
                   std::string& error)
{
    if (!cursor.readU16s(values.data(), values.size())) {
        error = std::string("unexpected end of cROM while reading ") + fieldName;
        return false;
    }
    return true;
}

bool hasNonZeroPixel(const document::Rgb565Frame& frame)
{
    return std::any_of(frame.pixels.begin(), frame.pixels.end(), [](uint16_t pixel) {
        return pixel != 0;
    });
}

render::Size sizeOf(uint32_t width, uint32_t height)
{
    return render::Size{static_cast<int>(width), static_cast<int>(height)};
}

}  // namespace

LegacyCromFrameParseResult parseModernLegacyCromFrames(const std::vector<uint8_t>& bytes)
{
    return parseModernLegacyCromFrames(bytes.data(), bytes.size());
}

LegacyCromFrameParseResult parseModernLegacyCromFrames(const uint8_t* bytes, std::size_t size)
{
    if (!bytes && size > 0) {
        return fail("cROM buffer is null");
    }
    LegacyByteSource source(bytes, size);
    return parseModernLegacyCromFrames(source);
}

LegacyCromFrameParseResult parseModernLegacyCromFrames(LegacyByteSource& cursor)
{
    // The header is short; parse it from the front of the data.
    std::size_t headerAvailable = 0;
    const uint8_t* header = cursor.peek(kMaxHeaderBytes, headerAvailable);
    const LegacyCromParseResult metadataResult = parseLegacyCromMetadata(header, headerAvailable);
    if (!metadataResult.ok) {
        return fail(metadataResult.errorMessage);
    }
    if (!metadataResult.metadata.modernRgb565Format) {
        return fail("legacy paletted cROM frame parsing is not implemented yet");
    }
    if (!cursor.skip(metadataResult.nextOffset)) {
        return fail("unexpected end of cROM while reading the header");
    }

    const LegacyCromMetadata& metadata = metadataResult.metadata;
    const render::Size sdSize = sizeOf(metadata.frameWidth, metadata.frameHeight);
    const render::Size hdSize = sizeOf(metadata.hdFrameWidth, metadata.hdFrameHeight);
    const std::size_t sdPixels = checkedProduct(metadata.frameWidth, metadata.frameHeight);
    const std::size_t hdPixels = checkedProduct(metadata.hdFrameWidth, metadata.hdFrameHeight);
    const std::size_t frameCount = metadata.frameCount;
    std::vector<uint8_t> bytes;
    std::vector<uint16_t> values;

    std::string error;
    if (!skipRequired(cursor, frameCount * kHashCodeBytes, "hash codes", error)) {
        return fail(error);
    }

    LegacyCromFrameData frameData;
    frameData.metadata = metadata;
    frameData.shapeComparisonModes.resize(frameCount);
    frameData.shapeComparisonModesOffset = cursor.offset();
    if (!cursor.readBytes(frameData.shapeComparisonModes.data(),
                          frameData.shapeComparisonModes.size())) {
        return fail("unexpected end of cROM while reading shape comparison modes");
    }

    frameData.comparisonMaskIds.resize(frameCount);
    frameData.comparisonMaskIdsOffset = cursor.offset();
    if (!cursor.readBytes(frameData.comparisonMaskIds.data(),
                          frameData.comparisonMaskIds.size())) {
        return fail("unexpected end of cROM while reading comparison mask IDs");
    }

    frameData.comparisonMasksOffset = cursor.offset();
    frameData.comparisonMasks.reserve(metadata.comparisonMaskCount);
    const render::Size maskSize = metadata.originalIs256x64 ? render::Size{256, 64} : sdSize;
    for (std::size_t maskIndex = 0; maskIndex < metadata.comparisonMaskCount; ++maskIndex) {
        document::IndexedFrame mask;
        mask.size = maskSize;
        const std::size_t maskPixels = static_cast<std::size_t>(maskSize.width) *
                                       static_cast<std::size_t>(maskSize.height);
        if (!readValues(cursor, mask.pixels, maskPixels, bytes, "comparison masks", error)) {
            return fail("unexpected end of cROM while reading comparison masks");
        }
        frameData.comparisonMasks.push_back(std::move(mask));
    }

    frameData.extraFrameFlags.resize(frameCount);
    frameData.extraFrameFlagsOffset = cursor.offset();
    if (!cursor.readBytes(frameData.extraFrameFlags.data(), frameData.extraFrameFlags.size())) {
        return fail("unexpected end of cROM while reading extra frame flags");
    }

    frameData.colorizedSdFramesOffset = cursor.offset();
    frameData.colorizedSdFrames.resize(frameCount);
    for (document::Rgb565Frame& frame : frameData.colorizedSdFrames) {
        frame.size = sdSize;
        if (!readValues(cursor, frame.pixels, sdPixels, values, "SD colorized frames", error)) {
            return fail(error);
        }
    }

    frameData.colorizedHdFramesOffset = cursor.offset();
    frameData.colorizedHdFrames.resize(frameCount);
    for (document::Rgb565Frame& frame : frameData.colorizedHdFrames) {
        frame.size = hdSize;
        if (!readValues(cursor, frame.pixels, hdPixels, values, "HD colorized frames", error)) {
            return fail(error);
        }
    }

    frameData.dynamicFrames.resize(frameCount);
    frameData.dynamicMasksSdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        dynamicFrame.maskSd.size = sdSize;
        if (!readValues(cursor, dynamicFrame.maskSd.pixels, sdPixels, bytes, "SD dynamic masks",
                        error)) {
            return fail(error);
        }
    }

    frameData.dynamicMasksHdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        dynamicFrame.maskHd.size = hdSize;
        if (!readValues(cursor, dynamicFrame.maskHd.pixels, hdPixels, bytes, "HD dynamic masks",
                        error)) {
            return fail(error);
        }
    }

    const std::size_t dynamicColorCount =
        document::kMaxDynamicSetsPerFrame * static_cast<std::size_t>(metadata.colorCount);
    frameData.dynamicColorsSdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        if (!readValues(cursor, dynamicFrame.colorsSd, dynamicColorCount, values,
                        "SD dynamic color sets", error)) {
            return fail(error);
        }
    }

    frameData.dynamicColorsHdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        if (!readValues(cursor, dynamicFrame.colorsHd, dynamicColorCount, values,
                        "HD dynamic color sets", error)) {
            return fail(error);
        }
    }

    const std::size_t spritePixels = kMaxSpriteWidth * kMaxSpriteHeight;
    const render::Size spriteSize = sizeOf(kMaxSpriteWidth, kMaxSpriteHeight);
    const std::size_t spriteCount = metadata.spriteCount;
    std::vector<uint8_t> extraSpriteFlags(spriteCount);
    frameData.extraSpriteFlagsOffset = cursor.offset();
    if (!cursor.readBytes(extraSpriteFlags.data(), extraSpriteFlags.size())) {
        return fail("unexpected end of cROM while reading extra sprite flags");
    }

    // The slots of a frame: 32 of 8-bit IDs (255 empty) before format version 1, the header's
    // count of 16-bit IDs (0xffff empty) from it on.
    const std::size_t slots = metadata.spritesPerFrame;
    const bool wideIds = metadata.formatVersion >= 1;
    frameData.frameSpriteReferences.assign(
        frameCount, std::vector<document::FrameSpriteReference>(slots));
    frameData.frameSpritesOffset = cursor.offset();
    {
        std::vector<uint8_t> narrowIds(slots);
        std::vector<uint16_t> wideIdValues(slots);
        for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            const bool read = wideIds ? cursor.readU16s(wideIdValues.data(), slots)
                                      : cursor.readBytes(narrowIds.data(), slots);
            if (!read) {
                return fail("unexpected end of cROM while reading frame sprite assignments");
            }
            for (std::size_t spriteSlot = 0; spriteSlot < slots; ++spriteSlot) {
                frameData.frameSpriteReferences[frameIndex][spriteSlot].spriteId =
                    wideIds ? wideIdValues[spriteSlot]
                    : narrowIds[spriteSlot] == 255 ? document::kNoSpriteId
                                                   : narrowIds[spriteSlot];
            }
        }
    }

    frameData.sprites.resize(spriteCount);
    frameData.spriteOriginalsOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        document::SpriteRecord& sprite = frameData.sprites[spriteIndex];
        sprite.hasHd = spriteIndex < extraSpriteFlags.size() && extraSpriteFlags[spriteIndex] != 0;
        sprite.originalSd.size = spriteSize;
        if (!readValues(cursor, sprite.originalSd.pixels, spritePixels, bytes, "sprite originals",
                        error)) {
            return fail(error);
        }
    }

    frameData.spriteColorsSdOffset = cursor.offset();
    for (document::SpriteRecord& sprite : frameData.sprites) {
        sprite.colorizedSd.size = spriteSize;
        if (!readValues(cursor, sprite.colorizedSd.pixels, spritePixels, values,
                        "SD sprite colors", error)) {
            return fail(error);
        }
    }

    frameData.spriteMasksHdOffset = cursor.offset();
    for (document::SpriteRecord& sprite : frameData.sprites) {
        sprite.maskHd.size = spriteSize;
        if (!readValues(cursor, sprite.maskHd.pixels, spritePixels, bytes, "HD sprite masks",
                        error)) {
            return fail(error);
        }
    }

    frameData.spriteColorsHdOffset = cursor.offset();
    for (document::SpriteRecord& sprite : frameData.sprites) {
        sprite.colorizedHd.size = spriteSize;
        if (!readValues(cursor, sprite.colorizedHd.pixels, spritePixels, values,
                        "HD sprite colors", error)) {
            return fail(error);
        }
    }

    frameData.activeFrameFlagsOffset = cursor.offset();
    if (!skipRequired(cursor, frameCount, "active frame flags", error)) {
        return fail(error);
    }

    const std::size_t colorRotationValueCount = kMaxColorRotations * kMaxColorRotationLength;
    frameData.colorRotationsSd.resize(frameCount);
    frameData.colorRotationsSdOffset = cursor.offset();
    for (document::CompactArray<uint16_t>& rotations : frameData.colorRotationsSd) {
        if (!readValues(cursor, rotations, colorRotationValueCount, values, "SD color rotations",
                        error)) {
            return fail(error);
        }
    }

    frameData.colorRotationsHd.resize(frameCount);
    frameData.colorRotationsHdOffset = cursor.offset();
    for (document::CompactArray<uint16_t>& rotations : frameData.colorRotationsHd) {
        if (!readValues(cursor, rotations, colorRotationValueCount, values, "HD color rotations",
                        error)) {
            return fail(error);
        }
    }

    frameData.spriteDetectionDwordsOffset = cursor.offset();
    for (document::SpriteRecord& sprite : frameData.sprites) {
        sprite.detectionDwords.resize(kMaxSpriteDetectAreas);
        if (!readU32Values(cursor, sprite.detectionDwords, "sprite detection dwords", error)) {
            return fail(error);
        }
    }

    frameData.spriteDetectionDwordPositionsOffset = cursor.offset();
    for (document::SpriteRecord& sprite : frameData.sprites) {
        sprite.detectionDwordPositions.resize(kMaxSpriteDetectAreas);
        if (!readU16Values(cursor, sprite.detectionDwordPositions,
                           "sprite detection dword positions", error)) {
            return fail(error);
        }
    }

    frameData.spriteDetectionAreasOffset = cursor.offset();
    for (document::SpriteRecord& sprite : frameData.sprites) {
        sprite.detectionAreas.resize(kMaxSpriteDetectAreas);
        for (document::SpriteDetectionArea& area : sprite.detectionAreas) {
            if (!cursor.readU16(area.x) || !cursor.readU16(area.y) ||
                !cursor.readU16(area.width) || !cursor.readU16(area.height)) {
                return fail("unexpected end of cROM while reading sprite detection areas");
            }
        }
    }

    frameData.triggerIdsOffset = cursor.offset();
    frameData.triggerIds.resize(frameCount);
    if (!readU32Values(cursor, frameData.triggerIds, "trigger IDs", error)) {
        return fail(error);
    }

    frameData.frameSpriteBoundingBoxesOffset = cursor.offset();
    {
        std::vector<uint16_t> boxes(4 * slots);
        for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            if (!cursor.readU16s(boxes.data(), 4 * slots)) {
                return fail("unexpected end of cROM while reading frame sprite bounding boxes");
            }
            for (std::size_t spriteSlot = 0; spriteSlot < slots; ++spriteSlot) {
                document::FrameSpriteReference& reference =
                    frameData.frameSpriteReferences[frameIndex][spriteSlot];
                reference.minX = boxes[4 * spriteSlot];
                reference.minY = boxes[4 * spriteSlot + 1];
                reference.maxX = boxes[4 * spriteSlot + 2];
                reference.maxY = boxes[4 * spriteSlot + 3];
            }
        }
    }

    frameData.extraBackgroundFlags.resize(metadata.backgroundCount);
    frameData.extraBackgroundFlagsOffset = cursor.offset();
    if (!cursor.readBytes(frameData.extraBackgroundFlags.data(),
                          frameData.extraBackgroundFlags.size())) {
        return fail("unexpected end of cROM while reading extra background flags");
    }

    frameData.backgrounds.resize(metadata.backgroundCount);
    frameData.backgroundFramesSdOffset = cursor.offset();
    for (document::BackgroundRecord& background : frameData.backgrounds) {
        background.colorizedSd.size = sdSize;
        if (!readValues(cursor, background.colorizedSd.pixels, sdPixels, values,
                        "SD background frames", error)) {
            return fail(error);
        }
    }

    frameData.backgroundFramesHdOffset = cursor.offset();
    for (std::size_t backgroundIndex = 0; backgroundIndex < metadata.backgroundCount;
         ++backgroundIndex) {
        document::Rgb565Frame backgroundHd;
        backgroundHd.size = hdSize;
        if (!readValues(cursor, backgroundHd.pixels, hdPixels, values, "HD background frames",
                        error)) {
            return fail(error);
        }
        if (backgroundIndex < frameData.extraBackgroundFlags.size() &&
            frameData.extraBackgroundFlags[backgroundIndex] != 0) {
            frameData.backgrounds[backgroundIndex].colorizedHd = std::move(backgroundHd);
        } else if (hasNonZeroPixel(backgroundHd)) {
            frameData.backgrounds[backgroundIndex].inactiveHd = std::move(backgroundHd);
        }
    }

    frameData.frameBackgrounds.resize(frameCount);
    frameData.backgroundIdsOffset = cursor.offset();
    for (document::FrameBackgroundData& backgroundData : frameData.frameBackgrounds) {
        if (!cursor.readU16(backgroundData.backgroundId)) {
            return fail("unexpected end of cROM while reading background IDs");
        }
    }

    frameData.backgroundMasksSdOffset = cursor.offset();
    for (document::FrameBackgroundData& backgroundData : frameData.frameBackgrounds) {
        backgroundData.maskSd.size = sdSize;
        if (!readValues(cursor, backgroundData.maskSd.pixels, sdPixels, bytes,
                        "SD background masks", error)) {
            return fail(error);
        }
    }

    frameData.backgroundMasksHdOffset = cursor.offset();
    for (document::FrameBackgroundData& backgroundData : frameData.frameBackgrounds) {
        backgroundData.maskHd.size = hdSize;
        if (!readValues(cursor, backgroundData.maskHd.pixels, hdPixels, bytes,
                        "HD background masks", error)) {
            return fail(error);
        }
    }

    frameData.dynamicShadowDirectionsSdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        if (!readValues(cursor, dynamicFrame.shadowDirectionsSd, document::kMaxDynamicSetsPerFrame,
                        bytes, "SD dynamic shadow directions", error)) {
            return fail("unexpected end of cROM while reading SD dynamic shadow directions");
        }
    }

    frameData.dynamicShadowColorsSdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        if (!readValues(cursor, dynamicFrame.shadowColorsSd, document::kMaxDynamicSetsPerFrame,
                        values, "SD dynamic shadow colors", error)) {
            return fail(error);
        }
    }

    frameData.dynamicShadowDirectionsHdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        if (!readValues(cursor, dynamicFrame.shadowDirectionsHd, document::kMaxDynamicSetsPerFrame,
                        bytes, "HD dynamic shadow directions", error)) {
            return fail("unexpected end of cROM while reading HD dynamic shadow directions");
        }
    }

    frameData.dynamicShadowColorsHdOffset = cursor.offset();
    for (document::FrameDynamicData& dynamicFrame : frameData.dynamicFrames) {
        if (!readValues(cursor, dynamicFrame.shadowColorsHd, document::kMaxDynamicSetsPerFrame,
                        values, "HD dynamic shadow colors", error)) {
            return fail(error);
        }
    }

    // Sprite dynamic content was appended in later format revisions. Older or shortened files
    // simply end here and keep the "no dynamic content" defaults.
    const std::size_t spriteDynamicColorCount =
        document::kMaxDynamicSetsPerSprite * static_cast<std::size_t>(metadata.colorCount);
    const std::size_t spriteDynamicBytes =
        spriteCount * (2 * spriteDynamicColorCount * kRgb565Bytes + 2 * spritePixels);
    const bool hasSpriteDynamicData =
        metadata.headerLengthBytes >= kSpriteDynamicHeaderThreshold &&
        cursor.remaining() >= spriteDynamicBytes;
    for (document::SpriteRecord& sprite : frameData.sprites) {
        document::normalizeSpriteRecord(sprite, metadata.colorCount);
    }
    if (hasSpriteDynamicData) {
        for (document::SpriteRecord& sprite : frameData.sprites) {
            if (!readValues(cursor, sprite.dynamicColorsSd, sprite.dynamicColorsSd.size(), values,
                            "SD sprite dynamic colors", error)) {
                return fail(error);
            }
        }
        for (document::SpriteRecord& sprite : frameData.sprites) {
            if (!readValues(cursor, sprite.dynamicColorsHd, sprite.dynamicColorsHd.size(), values,
                            "HD sprite dynamic colors", error)) {
                return fail(error);
            }
        }
        for (document::SpriteRecord& sprite : frameData.sprites) {
            if (!readValues(cursor, sprite.dynamicMaskSd.pixels, sprite.dynamicMaskSd.pixels.size(),
                            bytes, "SD sprite dynamic masks", error)) {
                return fail(error);
            }
        }
        for (document::SpriteRecord& sprite : frameData.sprites) {
            if (!readValues(cursor, sprite.dynamicMaskHd.pixels, sprite.dynamicMaskHd.pixels.size(),
                            bytes, "HD sprite dynamic masks", error)) {
                return fail(error);
            }
        }
        if (metadata.headerLengthBytes >= kSpriteShapeModeHeaderThreshold &&
            cursor.remaining() >= spriteCount) {
            for (document::SpriteRecord& sprite : frameData.sprites) {
                if (!cursor.readBytes(&sprite.shapeMode, 1)) {
                    return fail("unexpected end of cROM while reading sprite shape modes");
                }
            }
        }
    }

    LegacyCromFrameParseResult result;
    result.frameData = std::move(frameData);
    result.nextOffset = cursor.offset();
    result.ok = true;
    return result;
}

std::optional<document::Rgb565Frame> hdFrameIfPresent(const LegacyCromFrameData& frameData,
                                                      std::size_t frameIndex)
{
    if (frameIndex >= frameData.extraFrameFlags.size() ||
        frameIndex >= frameData.colorizedHdFrames.size() ||
        frameData.extraFrameFlags[frameIndex] == 0) {
        return std::nullopt;
    }
    return frameData.colorizedHdFrames[frameIndex];
}

std::optional<document::Rgb565Frame> inactiveHdFrameIfPresent(
    const LegacyCromFrameData& frameData,
    std::size_t frameIndex)
{
    if (frameIndex >= frameData.extraFrameFlags.size() ||
        frameIndex >= frameData.colorizedHdFrames.size() ||
        frameData.extraFrameFlags[frameIndex] != 0 ||
        !hasNonZeroPixel(frameData.colorizedHdFrames[frameIndex])) {
        return std::nullopt;
    }
    return frameData.colorizedHdFrames[frameIndex];
}

}  // namespace colorizing::legacy
