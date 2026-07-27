#include "legacy/LegacyCrpOriginals.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

namespace colorizing::legacy {

namespace {

constexpr std::size_t kRomNameBytes = 64;
constexpr std::size_t kBoolBytes = 4;
constexpr std::size_t kU32Bytes = 4;
constexpr std::size_t kIntBytes = 4;
constexpr std::size_t kMaxColorSets = 64;
constexpr std::size_t kColorSetColorCount = 16;
constexpr std::size_t kMaskCount = 64;
constexpr std::size_t kMaskNameBytes = 32;
constexpr std::size_t kMaxSections = 512;
constexpr std::size_t kSectionNameBytes = 32;
constexpr std::size_t kMaxSpriteNames = 255;
constexpr std::size_t kSpriteNameBytes = 32;
constexpr std::size_t kSpriteRectBytes = 255 * 4 * sizeof(uint16_t);
constexpr std::size_t kSpriteRectMirrorBytes = 255 * 2 * kBoolBytes;
constexpr std::size_t kPaletteCount = 256;
constexpr std::size_t kPaletteColorCount = 64;
constexpr std::size_t kImagePositionSaveCount = 256;
constexpr std::size_t kImagePositionNameBytes = 64;
constexpr std::size_t kImagePositionValueCount = 16;
constexpr std::size_t kPaletteNameBytes = 64;

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

    bool readU32(uint32_t& out)
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

    bool readU16(uint16_t& out)
    {
        if (m_offset > m_size || 2 > m_size - m_offset) {
            return false;
        }
        out = static_cast<uint16_t>(m_bytes[m_offset]) |
              static_cast<uint16_t>(static_cast<uint16_t>(m_bytes[m_offset + 1]) << 8);
        m_offset += 2;
        return true;
    }

    bool skip(std::size_t count)
    {
        if (m_offset > m_size || count > m_size - m_offset) {
            return false;
        }
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

std::string fixedNameToString(const std::vector<uint8_t>& nameBytes)
{
    const auto end = std::find(nameBytes.begin(), nameBytes.end(), uint8_t{0});
    return std::string(reinterpret_cast<const char*>(nameBytes.data()),
                       static_cast<std::size_t>(std::distance(nameBytes.begin(), end)));
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

    if (cursor.offset() < size) {
        const std::size_t beforeSpriteNamesBytes =
            kMaxColorSets * kBoolBytes +
            kMaxColorSets * kColorSetColorCount +
            1 +
            1 +
            kMaxColorSets * kRomNameBytes +
            kU32Bytes +
            1 +
            kIntBytes +
            kBoolBytes +
            kMaskCount * kMaskNameBytes +
            kU32Bytes +
            kMaxSections * kU32Bytes +
            kMaxSections * kSectionNameBytes;
        if (!cursor.skip(beforeSpriteNamesBytes)) {
            return fail("unexpected end of cRP before sprite names");
        }

        originalData.spriteNames.clear();
        originalData.spriteNames.reserve(cromMetadata.spriteCount);
        for (std::size_t spriteIndex = 0; spriteIndex < kMaxSpriteNames; ++spriteIndex) {
            std::vector<uint8_t> nameBytes(kSpriteNameBytes);
            if (!cursor.readBytes(nameBytes.data(), nameBytes.size())) {
                return fail("unexpected end of cRP while reading sprite names");
            }
            if (spriteIndex < cromMetadata.spriteCount) {
                originalData.spriteNames.push_back(fixedNameToString(nameBytes));
            }
        }

        const std::size_t spriteColorSourceBytes = kMaxSpriteNames * kU32Bytes;
        if (cursor.offset() < size) {
            if (!cursor.skip(spriteColorSourceBytes)) {
                return fail("unexpected end of cRP before frame durations");
            }

            originalData.frameDurationsMs.resize(cromMetadata.frameCount);
            for (uint32_t& durationMs : originalData.frameDurationsMs) {
                if (!cursor.readU32(durationMs)) {
                    return fail("unexpected end of cRP while reading frame durations");
                }
            }

            if (cursor.offset() < size) {
                if (!cursor.skip(kSpriteRectBytes + kSpriteRectMirrorBytes)) {
                    return fail("unexpected end of cRP before palettes");
                }
                const std::size_t paletteBytes =
                    kPaletteCount * kPaletteColorCount * sizeof(uint16_t);
                const std::size_t editColorsBytes = 16 * sizeof(uint16_t);
                const std::size_t imagePositionBytes =
                    sizeof(uint32_t) +
                    kImagePositionSaveCount * kImagePositionNameBytes +
                    kImagePositionSaveCount * kImagePositionValueCount * sizeof(int32_t);
                const std::size_t paletteNameBytes = kPaletteCount * kPaletteNameBytes;
                if (cursor.offset() + paletteBytes + editColorsBytes +
                        imagePositionBytes + paletteNameBytes <=
                    size) {
                    originalData.palettes.assign(kPaletteCount, document::PaletteRecord{});
                    for (document::PaletteRecord& palette : originalData.palettes) {
                        palette.colors.resize(kPaletteColorCount);
                        for (uint16_t& color : palette.colors) {
                            if (!cursor.readU16(color)) {
                                return fail("unexpected end of cRP while reading palettes");
                            }
                        }
                    }
                    if (!cursor.skip(editColorsBytes + imagePositionBytes)) {
                        return fail("unexpected end of cRP before palette names");
                    }
                    for (document::PaletteRecord& palette : originalData.palettes) {
                        std::vector<uint8_t> nameBytes(kPaletteNameBytes);
                        if (!cursor.readBytes(nameBytes.data(), nameBytes.size())) {
                            return fail("unexpected end of cRP while reading palette names");
                        }
                        palette.name = fixedNameToString(nameBytes);
                    }
                }
            }
        }
    }

    LegacyCrpOriginalParseResult result;
    result.originalData = std::move(originalData);
    result.nextOffset = cursor.offset();
    result.ok = true;
    return result;
}

}  // namespace colorizing::legacy
