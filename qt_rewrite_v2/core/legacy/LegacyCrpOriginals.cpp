#include "legacy/LegacyCrpOriginals.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace colorizing::legacy {

namespace {

constexpr std::size_t kRomNameBytes = 64;

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

    bool readBytes(uint8_t* out, std::size_t count)
    {
        if (m_offset > m_size || count > m_size - m_offset) {
            return false;
        }
        std::memcpy(out, m_bytes + m_offset, count);
        m_offset += count;
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

LegacyCrpOriginalParseResult fail(std::string message)
{
    LegacyCrpOriginalParseResult result;
    result.errorMessage = std::move(message);
    return result;
}

}  // namespace

LegacyCrpOriginalParseResult parseLegacyCrpOriginalFrames(
    const std::vector<uint8_t>& bytes,
    const LegacyCromMetadata& cromMetadata)
{
    return parseLegacyCrpOriginalFrames(bytes.data(), bytes.size(), cromMetadata);
}

LegacyCrpOriginalParseResult parseLegacyCrpOriginalFrames(
    const uint8_t* bytes,
    std::size_t size,
    const LegacyCromMetadata& cromMetadata)
{
    if (!bytes && size > 0) {
        return fail("cRP buffer is null");
    }
    if (cromMetadata.frameWidth == 0 || cromMetadata.frameHeight == 0 ||
        cromMetadata.frameCount == 0) {
        return fail("cROM metadata is incomplete for cRP original frame parsing");
    }

    Cursor cursor(bytes, size);
    uint8_t nameBytes[kRomNameBytes] = {};
    if (!cursor.readBytes(nameBytes, kRomNameBytes)) {
        return fail("unexpected end of cRP while reading ROM name");
    }

    LegacyCrpOriginalData originalData;
    originalData.romName = fixedNameToString(nameBytes);
    if (originalData.romName.empty()) {
        return fail("cRP ROM name is empty");
    }
    if (originalData.romName != cromMetadata.romName) {
        return fail("cRP ROM name does not match cROM ROM name");
    }

    originalData.originalSdFrames.reserve(cromMetadata.frameCount);
    const render::Size frameSize{static_cast<int>(cromMetadata.frameWidth),
                                 static_cast<int>(cromMetadata.frameHeight)};
    const std::size_t frameBytes =
        static_cast<std::size_t>(cromMetadata.frameWidth) *
        static_cast<std::size_t>(cromMetadata.frameHeight);

    for (std::size_t frameIndex = 0; frameIndex < cromMetadata.frameCount; ++frameIndex) {
        document::IndexedFrame frame = document::makeIndexedFrame(frameSize);
        if (frame.pixels.size() != frameBytes ||
            !cursor.readBytes(frame.pixels.data(), frame.pixels.size())) {
            return fail("unexpected end of cRP while reading original frames");
        }
        originalData.originalSdFrames.push_back(std::move(frame));
    }

    LegacyCrpOriginalParseResult result;
    result.originalData = std::move(originalData);
    result.nextOffset = cursor.offset();
    result.ok = true;
    return result;
}

}  // namespace colorizing::legacy

