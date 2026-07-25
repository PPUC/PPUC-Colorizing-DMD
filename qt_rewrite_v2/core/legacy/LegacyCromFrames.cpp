#include "legacy/LegacyCromFrames.h"

#include <cstring>
#include <utility>

namespace colorizing::legacy {

namespace {

constexpr std::size_t kHashCodeBytes = sizeof(uint32_t);
constexpr std::size_t kRgb565Bytes = sizeof(uint16_t);
constexpr std::size_t kU32Bytes = sizeof(uint32_t);
constexpr std::size_t kMaxSpritesPerFrame = 32;
constexpr std::size_t kMaxSpriteWidth = 256;
constexpr std::size_t kMaxSpriteHeight = 64;
constexpr std::size_t kMaxColorRotations = 4;
constexpr std::size_t kMaxColorRotationLength = 64;
constexpr std::size_t kMaxSpriteDetectAreas = 4;

class Cursor {
public:
    Cursor(const uint8_t* bytes, std::size_t size, std::size_t offset)
        : m_bytes(bytes)
        , m_size(size)
        , m_offset(offset)
    {
    }

    [[nodiscard]] std::size_t offset() const
    {
        return m_offset;
    }

    [[nodiscard]] bool skip(std::size_t count)
    {
        if (m_offset > m_size || count > m_size - m_offset) {
            return false;
        }
        m_offset += count;
        return true;
    }

    [[nodiscard]] bool readBytes(uint8_t* out, std::size_t count)
    {
        if (m_offset > m_size || count > m_size - m_offset) {
            return false;
        }
        std::memcpy(out, m_bytes + m_offset, count);
        m_offset += count;
        return true;
    }

    [[nodiscard]] bool readU16(uint16_t& out)
    {
        if (m_offset > m_size || kRgb565Bytes > m_size - m_offset) {
            return false;
        }
        out = static_cast<uint16_t>(m_bytes[m_offset]) |
              static_cast<uint16_t>(static_cast<uint16_t>(m_bytes[m_offset + 1]) << 8);
        m_offset += kRgb565Bytes;
        return true;
    }

    [[nodiscard]] bool readU32(uint32_t& out)
    {
        if (m_offset > m_size || kU32Bytes > m_size - m_offset) {
            return false;
        }
        out = static_cast<uint32_t>(m_bytes[m_offset]) |
              (static_cast<uint32_t>(m_bytes[m_offset + 1]) << 8) |
              (static_cast<uint32_t>(m_bytes[m_offset + 2]) << 16) |
              (static_cast<uint32_t>(m_bytes[m_offset + 3]) << 24);
        m_offset += kU32Bytes;
        return true;
    }

private:
    const uint8_t* m_bytes = nullptr;
    std::size_t m_size = 0;
    std::size_t m_offset = 0;
};

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

bool skipRequired(Cursor& cursor, std::size_t bytes, const char* fieldName, std::string& error)
{
    if (cursor.skip(bytes)) {
        return true;
    }
    error = std::string("unexpected end of cROM while skipping ") + fieldName;
    return false;
}

bool readRgb565Frame(Cursor& cursor,
                     document::Rgb565Frame& frame,
                     const char* fieldName,
                     std::string& error)
{
    for (uint16_t& pixel : frame.pixels) {
        if (!cursor.readU16(pixel)) {
            error = std::string("unexpected end of cROM while reading ") + fieldName;
            return false;
        }
    }
    return true;
}

bool readIndexedFrame(Cursor& cursor,
                      document::IndexedFrame& frame,
                      const char* fieldName,
                      std::string& error)
{
    if (cursor.readBytes(frame.pixels.data(), frame.pixels.size())) {
        return true;
    }
    error = std::string("unexpected end of cROM while reading ") + fieldName;
    return false;
}

bool readRgb565Values(Cursor& cursor,
                      std::vector<uint16_t>& values,
                      const char* fieldName,
                      std::string& error)
{
    for (uint16_t& value : values) {
        if (!cursor.readU16(value)) {
            error = std::string("unexpected end of cROM while reading ") + fieldName;
            return false;
        }
    }
    return true;
}

bool readU32Values(Cursor& cursor,
                   std::vector<uint32_t>& values,
                   const char* fieldName,
                   std::string& error)
{
    for (uint32_t& value : values) {
        if (!cursor.readU32(value)) {
            error = std::string("unexpected end of cROM while reading ") + fieldName;
            return false;
        }
    }
    return true;
}

}  // namespace

LegacyCromFrameParseResult parseModernLegacyCromFrames(const std::vector<uint8_t>& bytes)
{
    return parseModernLegacyCromFrames(bytes.data(), bytes.size());
}

LegacyCromFrameParseResult parseModernLegacyCromFrames(const uint8_t* bytes, std::size_t size)
{
    const LegacyCromParseResult metadataResult = parseLegacyCromMetadata(bytes, size);
    if (!metadataResult.ok) {
        return fail(metadataResult.errorMessage);
    }
    if (!metadataResult.metadata.modernRgb565Format) {
        return fail("legacy paletted cROM frame parsing is not implemented yet");
    }

    const LegacyCromMetadata& metadata = metadataResult.metadata;
    Cursor cursor(bytes, size, metadataResult.nextOffset);

    const std::size_t sdPixels = checkedProduct(metadata.frameWidth, metadata.frameHeight);
    const std::size_t hdPixels = checkedProduct(metadata.hdFrameWidth, metadata.hdFrameHeight);
    const std::size_t frameCount = metadata.frameCount;

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
    for (std::size_t maskIndex = 0; maskIndex < metadata.comparisonMaskCount; ++maskIndex) {
        document::IndexedFrame mask =
            document::makeIndexedFrame(render::Size{static_cast<int>(metadata.frameWidth),
                                                    static_cast<int>(metadata.frameHeight)});
        if (mask.pixels.size() != sdPixels ||
            !cursor.readBytes(mask.pixels.data(), mask.pixels.size())) {
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
    frameData.colorizedSdFrames.reserve(frameCount);
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document::Rgb565Frame frame =
            document::makeRgb565Frame(render::Size{static_cast<int>(metadata.frameWidth),
                                                   static_cast<int>(metadata.frameHeight)});
        if (frame.pixels.size() != sdPixels ||
            !readRgb565Frame(cursor, frame, "SD colorized frames", error)) {
            return fail(error);
        }
        frameData.colorizedSdFrames.push_back(std::move(frame));
    }

    frameData.colorizedHdFramesOffset = cursor.offset();
    frameData.colorizedHdFrames.reserve(frameCount);
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document::Rgb565Frame frame =
            document::makeRgb565Frame(render::Size{static_cast<int>(metadata.hdFrameWidth),
                                                   static_cast<int>(metadata.hdFrameHeight)});
        if (frame.pixels.size() != hdPixels ||
            !readRgb565Frame(cursor, frame, "HD colorized frames", error)) {
            return fail(error);
        }
        frameData.colorizedHdFrames.push_back(std::move(frame));
    }

    frameData.dynamicFrames.resize(frameCount);
    frameData.dynamicMasksSdOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document::FrameDynamicData& dynamicFrame = frameData.dynamicFrames[frameIndex];
        dynamicFrame.maskSd =
            document::makeIndexedFrame(render::Size{static_cast<int>(metadata.frameWidth),
                                                    static_cast<int>(metadata.frameHeight)}, 255);
        if (dynamicFrame.maskSd.pixels.size() != sdPixels ||
            !readIndexedFrame(cursor, dynamicFrame.maskSd, "SD dynamic masks", error)) {
            return fail(error);
        }
    }

    frameData.dynamicMasksHdOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document::FrameDynamicData& dynamicFrame = frameData.dynamicFrames[frameIndex];
        dynamicFrame.maskHd =
            document::makeIndexedFrame(render::Size{static_cast<int>(metadata.hdFrameWidth),
                                                    static_cast<int>(metadata.hdFrameHeight)}, 255);
        if (dynamicFrame.maskHd.pixels.size() != hdPixels ||
            !readIndexedFrame(cursor, dynamicFrame.maskHd, "HD dynamic masks", error)) {
            return fail(error);
        }
    }

    const std::size_t dynamicColorCount =
        document::kMaxDynamicSetsPerFrame * static_cast<std::size_t>(metadata.colorCount);
    frameData.dynamicColorsSdOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        std::vector<uint16_t>& colors = frameData.dynamicFrames[frameIndex].colorsSd;
        colors.resize(dynamicColorCount);
        if (!readRgb565Values(cursor, colors, "SD dynamic color sets", error)) {
            return fail(error);
        }
    }

    frameData.dynamicColorsHdOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        std::vector<uint16_t>& colors = frameData.dynamicFrames[frameIndex].colorsHd;
        colors.resize(dynamicColorCount);
        if (!readRgb565Values(cursor, colors, "HD dynamic color sets", error)) {
            return fail(error);
        }
    }

    const std::size_t spritePixels = kMaxSpriteWidth * kMaxSpriteHeight;
    const std::size_t spriteCount = metadata.spriteCount;
    std::vector<uint8_t> extraSpriteFlags(spriteCount);
    frameData.extraSpriteFlagsOffset = cursor.offset();
    if (!cursor.readBytes(extraSpriteFlags.data(), extraSpriteFlags.size())) {
        return fail("unexpected end of cROM while reading extra sprite flags");
    }

    frameData.frameSpriteReferences.assign(
        frameCount,
        std::vector<document::FrameSpriteReference>(kMaxSpritesPerFrame));
    frameData.frameSpritesOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        for (std::size_t spriteSlot = 0; spriteSlot < kMaxSpritesPerFrame; ++spriteSlot) {
            uint8_t spriteId = 255;
            if (!cursor.readBytes(&spriteId, 1)) {
                return fail("unexpected end of cROM while reading frame sprite assignments");
            }
            frameData.frameSpriteReferences[frameIndex][spriteSlot].spriteId = spriteId;
        }
    }

    frameData.sprites.resize(spriteCount);
    frameData.spriteOriginalsOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        document::SpriteRecord& sprite = frameData.sprites[spriteIndex];
        sprite.hasHd = spriteIndex < extraSpriteFlags.size() && extraSpriteFlags[spriteIndex] != 0;
        sprite.originalSd =
            document::makeIndexedFrame(render::Size{static_cast<int>(kMaxSpriteWidth),
                                                    static_cast<int>(kMaxSpriteHeight)}, 255);
        if (sprite.originalSd.pixels.size() != spritePixels ||
            !readIndexedFrame(cursor, sprite.originalSd, "sprite originals", error)) {
            return fail(error);
        }
    }

    frameData.spriteColorsSdOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        document::SpriteRecord& sprite = frameData.sprites[spriteIndex];
        sprite.colorizedSd =
            document::makeRgb565Frame(render::Size{static_cast<int>(kMaxSpriteWidth),
                                                   static_cast<int>(kMaxSpriteHeight)});
        if (sprite.colorizedSd.pixels.size() != spritePixels ||
            !readRgb565Frame(cursor, sprite.colorizedSd, "SD sprite colors", error)) {
            return fail(error);
        }
    }

    frameData.spriteMasksHdOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        document::SpriteRecord& sprite = frameData.sprites[spriteIndex];
        sprite.maskHd =
            document::makeIndexedFrame(render::Size{static_cast<int>(kMaxSpriteWidth),
                                                    static_cast<int>(kMaxSpriteHeight)}, 255);
        if (sprite.maskHd.pixels.size() != spritePixels ||
            !readIndexedFrame(cursor, sprite.maskHd, "HD sprite masks", error)) {
            return fail(error);
        }
    }

    frameData.spriteColorsHdOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        document::SpriteRecord& sprite = frameData.sprites[spriteIndex];
        sprite.colorizedHd =
            document::makeRgb565Frame(render::Size{static_cast<int>(kMaxSpriteWidth),
                                                   static_cast<int>(kMaxSpriteHeight)});
        if (sprite.colorizedHd.pixels.size() != spritePixels ||
            !readRgb565Frame(cursor, sprite.colorizedHd, "HD sprite colors", error)) {
            return fail(error);
        }
    }

    if (!skipRequired(cursor, frameCount, "active frame flags", error) ||
        !skipRequired(cursor,
                      frameCount * kMaxColorRotations * kMaxColorRotationLength * kRgb565Bytes,
                      "SD color rotations",
                      error) ||
        !skipRequired(cursor,
                      frameCount * kMaxColorRotations * kMaxColorRotationLength * kRgb565Bytes,
                      "HD color rotations",
                      error)) {
        return fail(error);
    }

    frameData.spriteDetectionDwordsOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        std::vector<uint32_t>& dwords = frameData.sprites[spriteIndex].detectionDwords;
        dwords.resize(kMaxSpriteDetectAreas);
        if (!readU32Values(cursor, dwords, "sprite detection dwords", error)) {
            return fail(error);
        }
    }

    frameData.spriteDetectionDwordPositionsOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        std::vector<uint16_t>& positions =
            frameData.sprites[spriteIndex].detectionDwordPositions;
        positions.resize(kMaxSpriteDetectAreas);
        if (!readRgb565Values(cursor,
                              positions,
                              "sprite detection dword positions",
                              error)) {
            return fail(error);
        }
    }

    frameData.spriteDetectionAreasOffset = cursor.offset();
    for (std::size_t spriteIndex = 0; spriteIndex < spriteCount; ++spriteIndex) {
        std::vector<document::SpriteDetectionArea>& areas =
            frameData.sprites[spriteIndex].detectionAreas;
        areas.resize(kMaxSpriteDetectAreas);
        for (document::SpriteDetectionArea& area : areas) {
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
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        for (std::size_t spriteSlot = 0; spriteSlot < kMaxSpritesPerFrame; ++spriteSlot) {
            document::FrameSpriteReference& reference =
                frameData.frameSpriteReferences[frameIndex][spriteSlot];
            if (!cursor.readU16(reference.minX) || !cursor.readU16(reference.minY) ||
                !cursor.readU16(reference.maxX) || !cursor.readU16(reference.maxY)) {
                return fail("unexpected end of cROM while reading frame sprite bounding boxes");
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
    for (std::size_t backgroundIndex = 0; backgroundIndex < metadata.backgroundCount;
         ++backgroundIndex) {
        document::BackgroundRecord& background = frameData.backgrounds[backgroundIndex];
        background.colorizedSd =
            document::makeRgb565Frame(render::Size{static_cast<int>(metadata.frameWidth),
                                                   static_cast<int>(metadata.frameHeight)});
        if (background.colorizedSd.pixels.size() != sdPixels ||
            !readRgb565Frame(cursor, background.colorizedSd, "SD background frames", error)) {
            return fail(error);
        }
    }

    frameData.backgroundFramesHdOffset = cursor.offset();
    for (std::size_t backgroundIndex = 0; backgroundIndex < metadata.backgroundCount;
         ++backgroundIndex) {
        document::Rgb565Frame backgroundHd =
            document::makeRgb565Frame(render::Size{static_cast<int>(metadata.hdFrameWidth),
                                                   static_cast<int>(metadata.hdFrameHeight)});
        if (backgroundHd.pixels.size() != hdPixels ||
            !readRgb565Frame(cursor, backgroundHd, "HD background frames", error)) {
            return fail(error);
        }
        if (backgroundIndex < frameData.extraBackgroundFlags.size() &&
            frameData.extraBackgroundFlags[backgroundIndex] != 0) {
            frameData.backgrounds[backgroundIndex].colorizedHd = std::move(backgroundHd);
        }
    }

    frameData.frameBackgrounds.resize(frameCount);
    frameData.backgroundIdsOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        if (!cursor.readU16(frameData.frameBackgrounds[frameIndex].backgroundId)) {
            return fail("unexpected end of cROM while reading background IDs");
        }
    }

    frameData.backgroundMasksSdOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document::FrameBackgroundData& backgroundData = frameData.frameBackgrounds[frameIndex];
        backgroundData.maskSd =
            document::makeIndexedFrame(render::Size{static_cast<int>(metadata.frameWidth),
                                                    static_cast<int>(metadata.frameHeight)}, 0);
        if (backgroundData.maskSd.pixels.size() != sdPixels ||
            !readIndexedFrame(cursor, backgroundData.maskSd, "SD background masks", error)) {
            return fail(error);
        }
    }

    frameData.backgroundMasksHdOffset = cursor.offset();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document::FrameBackgroundData& backgroundData = frameData.frameBackgrounds[frameIndex];
        backgroundData.maskHd =
            document::makeIndexedFrame(render::Size{static_cast<int>(metadata.hdFrameWidth),
                                                    static_cast<int>(metadata.hdFrameHeight)}, 0);
        if (backgroundData.maskHd.pixels.size() != hdPixels ||
            !readIndexedFrame(cursor, backgroundData.maskHd, "HD background masks", error)) {
            return fail(error);
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

}  // namespace colorizing::legacy
