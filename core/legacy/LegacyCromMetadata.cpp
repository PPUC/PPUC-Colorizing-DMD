#include "legacy/LegacyCromMetadata.h"

#include <algorithm>
#include <cstring>

namespace colorizing::legacy {

namespace {

constexpr std::size_t kRomNameBytes = 64;
constexpr uint32_t kModernHeaderThreshold = 14 * sizeof(uint32_t);
constexpr uint32_t kBackgroundHeaderThreshold = 13 * sizeof(uint32_t);
constexpr uint32_t kOriginal256x64HeaderThreshold = 20 * sizeof(uint32_t);
constexpr uint32_t kFormatVersionHeaderThreshold = 21 * sizeof(uint32_t);
// The most sprite slots a frame of format version 1 has (libserum's MAX_SPRITES_PER_FRAME).
constexpr uint32_t kMaxFormatSpritesPerFrame = 128;

class Cursor {
public:
    Cursor(const uint8_t* bytes, std::size_t size)
        : m_bytes(bytes)
        , m_size(size)
    {
    }

    [[nodiscard]] std::size_t offset() const
    {
        return m_offset;
    }

    [[nodiscard]] bool canRead(std::size_t count) const
    {
        return m_offset <= m_size && count <= m_size - m_offset;
    }

    bool readBytes(uint8_t* out, std::size_t count)
    {
        if (!canRead(count)) {
            return false;
        }
        std::memcpy(out, m_bytes + m_offset, count);
        m_offset += count;
        return true;
    }

    bool readU16(uint16_t& out)
    {
        if (!canRead(sizeof(uint16_t))) {
            return false;
        }
        out = static_cast<uint16_t>(m_bytes[m_offset]) |
              static_cast<uint16_t>(static_cast<uint16_t>(m_bytes[m_offset + 1]) << 8);
        m_offset += sizeof(uint16_t);
        return true;
    }

    bool readU32(uint32_t& out)
    {
        if (!canRead(sizeof(uint32_t))) {
            return false;
        }
        out = static_cast<uint32_t>(m_bytes[m_offset]) |
              (static_cast<uint32_t>(m_bytes[m_offset + 1]) << 8) |
              (static_cast<uint32_t>(m_bytes[m_offset + 2]) << 16) |
              (static_cast<uint32_t>(m_bytes[m_offset + 3]) << 24);
        m_offset += sizeof(uint32_t);
        return true;
    }

private:
    const uint8_t* m_bytes = nullptr;
    std::size_t m_size = 0;
    std::size_t m_offset = 0;
};

std::string fixedNameToString(const uint8_t (&nameBytes)[kRomNameBytes])
{
    const auto end = std::find(nameBytes, nameBytes + kRomNameBytes, uint8_t{0});
    return std::string(reinterpret_cast<const char*>(nameBytes),
                       reinterpret_cast<const char*>(end));
}

LegacyCromParseResult fail(std::string message)
{
    LegacyCromParseResult result;
    result.errorMessage = std::move(message);
    return result;
}

bool readRequiredU32(Cursor& cursor, uint32_t& out, const char* fieldName, std::string& error)
{
    if (cursor.readU32(out)) {
        return true;
    }
    error = std::string("unexpected end of cROM while reading ") + fieldName;
    return false;
}

}  // namespace

LegacyCromParseResult parseLegacyCromMetadata(const std::vector<uint8_t>& bytes)
{
    return parseLegacyCromMetadata(bytes.data(), bytes.size());
}

LegacyCromParseResult parseLegacyCromMetadata(const uint8_t* bytes, std::size_t size)
{
    if (!bytes && size > 0) {
        return fail("cROM buffer is null");
    }

    Cursor cursor(bytes, size);
    uint8_t nameBytes[kRomNameBytes] = {};
    if (!cursor.readBytes(nameBytes, kRomNameBytes)) {
        return fail("unexpected end of cROM while reading ROM name");
    }

    LegacyCromMetadata metadata;
    metadata.romName = fixedNameToString(nameBytes);
    if (metadata.romName.empty()) {
        return fail("cROM ROM name is empty");
    }

    std::string error;
    if (!readRequiredU32(cursor, metadata.headerLengthBytes, "header length", error) ||
        !readRequiredU32(cursor, metadata.frameWidth, "frame width", error) ||
        !readRequiredU32(cursor, metadata.frameHeight, "frame height", error)) {
        return fail(error);
    }

    metadata.modernRgb565Format = metadata.headerLengthBytes >= kModernHeaderThreshold;
    if (metadata.modernRgb565Format) {
        if (!readRequiredU32(cursor, metadata.hdFrameWidth, "HD frame width", error) ||
            !readRequiredU32(cursor, metadata.hdFrameHeight, "HD frame height", error)) {
            return fail(error);
        }
    } else if (metadata.frameHeight == 64) {
        metadata.hdFrameWidth = metadata.frameWidth / 2;
        metadata.hdFrameHeight = metadata.frameHeight / 2;
    } else {
        metadata.hdFrameWidth = metadata.frameWidth * 2;
        metadata.hdFrameHeight = metadata.frameHeight * 2;
    }

    if (!readRequiredU32(cursor, metadata.frameCount, "frame count", error) ||
        !readRequiredU32(cursor, metadata.colorCount, "color count", error)) {
        return fail(error);
    }

    if (!metadata.modernRgb565Format) {
        uint32_t ignoredColorizedColorCount = 0;
        if (!readRequiredU32(cursor, ignoredColorizedColorCount, "legacy colorized color count", error)) {
            return fail(error);
        }
    }

    if (!readRequiredU32(cursor, metadata.comparisonMaskCount, "comparison mask count", error)) {
        return fail(error);
    }

    if (!metadata.modernRgb565Format) {
        uint32_t ignoredMovingRectCount = 0;
        if (!readRequiredU32(cursor, ignoredMovingRectCount, "legacy moving rectangle count", error)) {
            return fail(error);
        }
    }

    if (!readRequiredU32(cursor, metadata.spriteCount, "sprite count", error)) {
        return fail(error);
    }

    if (metadata.headerLengthBytes >= kBackgroundHeaderThreshold) {
        if (!cursor.readU16(metadata.backgroundCount)) {
            return fail("unexpected end of cROM while reading background count");
        }
    }

    if (metadata.headerLengthBytes >= kOriginal256x64HeaderThreshold) {
        uint32_t originalIs256x64 = 0;
        if (!readRequiredU32(cursor, originalIs256x64, "256x64 original flag", error)) {
            return fail(error);
        }
        metadata.originalIs256x64 = originalIs256x64 != 0;
    }

    if (metadata.headerLengthBytes >= kFormatVersionHeaderThreshold) {
        if (!readRequiredU32(cursor, metadata.formatVersion, "format version", error)) {
            return fail(error);
        }
        if (metadata.formatVersion > kNewestCromFormatVersion) {
            return fail("the cROM has format version " + std::to_string(metadata.formatVersion) +
                        "; this editor reads up to " +
                        std::to_string(kNewestCromFormatVersion));
        }
        if (metadata.formatVersion >= 1) {
            if (!readRequiredU32(cursor, metadata.spritesPerFrame, "sprites per frame", error)) {
                return fail(error);
            }
            if (metadata.spritesPerFrame == 0 ||
                metadata.spritesPerFrame > kMaxFormatSpritesPerFrame) {
                return fail("cROM sprites per frame must be between 1 and 128");
            }
        }
    }

    if (metadata.frameWidth == 0 || metadata.frameHeight == 0 ||
        metadata.hdFrameWidth == 0 || metadata.hdFrameHeight == 0) {
        return fail("cROM frame dimensions must be non-zero");
    }
    if (metadata.frameCount == 0) {
        return fail("cROM frame count must be non-zero");
    }
    if (metadata.colorCount == 0) {
        return fail("cROM color count must be non-zero");
    }

    LegacyCromParseResult result;
    result.metadata = metadata;
    result.nextOffset = cursor.offset();
    result.ok = true;
    return result;
}

}  // namespace colorizing::legacy

