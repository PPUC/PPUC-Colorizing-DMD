#include "legacy/LegacyCromFrames.h"

#include <cstring>
#include <utility>

namespace colorizing::legacy {

namespace {

constexpr std::size_t kHashCodeBytes = sizeof(uint32_t);
constexpr std::size_t kRgb565Bytes = sizeof(uint16_t);

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
    const std::size_t comparisonMaskPixels =
        static_cast<std::size_t>(metadata.comparisonMaskCount) * sdPixels;

    std::string error;
    if (!skipRequired(cursor, frameCount * kHashCodeBytes, "hash codes", error) ||
        !skipRequired(cursor, frameCount, "shape comparison modes", error) ||
        !skipRequired(cursor, frameCount, "comparison mask IDs", error) ||
        !skipRequired(cursor, comparisonMaskPixels, "comparison masks", error)) {
        return fail(error);
    }

    LegacyCromFrameData frameData;
    frameData.metadata = metadata;
    frameData.extraFrameFlags.resize(frameCount);
    if (!cursor.readBytes(frameData.extraFrameFlags.data(), frameData.extraFrameFlags.size())) {
        return fail("unexpected end of cROM while reading extra frame flags");
    }

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
