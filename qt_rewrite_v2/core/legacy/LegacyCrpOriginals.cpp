#include "legacy/LegacyCrpOriginals.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include "legacy/LegacyByteSource.h"

namespace colorizing::legacy {

namespace {

constexpr std::size_t kRomNameBytes = 64;
constexpr std::size_t kMaxSpriteNames = 255;
constexpr std::size_t kSpriteNameBytes = 32;
constexpr std::size_t kPaletteCount = 256;
constexpr std::size_t kPaletteColorCount = 64;
constexpr std::size_t kPaletteNameBytes = 64;

// The reads the parser needs, over a LegacyByteSource.
class Cursor {
public:
    explicit Cursor(LegacyByteSource& source)
        : m_source(source)
    {
    }

    [[nodiscard]] std::size_t offset() const
    {
        return m_source.offset();
    }

    bool readBytes(uint8_t* out, std::size_t count)
    {
        return m_source.readBytes(out, count);
    }

    bool readU32(uint32_t& out)
    {
        return m_source.readU32(out);
    }

    bool readU16(uint16_t& out)
    {
        return m_source.readU16(out);
    }

    bool readU32Values(std::vector<uint32_t>& values)
    {
        return m_source.readU32s(values.data(), values.size());
    }

    bool readU16Values(std::vector<uint16_t>& values)
    {
        return m_source.readU16s(values.data(), values.size());
    }

private:
    LegacyByteSource& m_source;
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
    LegacyByteSource source(bytes, size);
    return parseLegacyCrpOriginalFrames(source, cromMetadata);
}

LegacyCrpOriginalParseResult parseLegacyCrpOriginalFrames(LegacyByteSource& source,
                                                          const LegacyCromMetadata& cromMetadata)
{
    const std::size_t size = source.size();
    if (cromMetadata.frameWidth == 0 || cromMetadata.frameHeight == 0 ||
        cromMetadata.frameCount == 0) {
        return fail("cROM metadata is incomplete for cRP original frame parsing");
    }

    Cursor cursor(source);
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
    const render::Size frameSize =
        cromMetadata.originalIs256x64
            ? render::Size{256, 64}
            : render::Size{static_cast<int>(cromMetadata.frameWidth),
                           static_cast<int>(cromMetadata.frameHeight)};
    const std::size_t frameBytes =
        static_cast<std::size_t>(frameSize.width) * static_cast<std::size_t>(frameSize.height);

    std::vector<uint8_t> scratch(frameBytes);
    for (std::size_t frameIndex = 0; frameIndex < cromMetadata.frameCount; ++frameIndex) {
        document::IndexedFrame frame;
        frame.size = frameSize;
        if (!cursor.readBytes(scratch.data(), scratch.size())) {
            return fail("unexpected end of cRP while reading original frames");
        }
        frame.pixels.assignValues(scratch.data(), scratch.size());
        originalData.originalSdFrames.push_back(std::move(frame));
    }

    document::EditorMetadata& metadata = originalData.editorMetadata;
    const auto remaining = [&cursor, size]() { return size - cursor.offset(); };

    // Older editor versions wrote shorter files, so the file may legitimately end at the
    // boundaries checked below. Everything missing keeps its default.
    if (cursor.offset() < size) {
        uint32_t maskSelectionMode = 0;
        if (!cursor.readU32Values(metadata.activeColorSets) ||
            !cursor.readU16Values(metadata.colorSets) ||
            !cursor.readBytes(&metadata.currentColorSet, 1) ||
            !cursor.readBytes(&metadata.firstDisplayedColorSet, 1) ||
            !cursor.readBytes(metadata.colorSetNames.data(), metadata.colorSetNames.size()) ||
            !cursor.readU32(metadata.drawColorMode) ||
            !cursor.readBytes(&metadata.drawMode, 1) ||
            !cursor.readU32(maskSelectionMode) ||
            !cursor.readU32(metadata.fillMode) ||
            !cursor.readBytes(metadata.maskNames.data(), metadata.maskNames.size()) ||
            !cursor.readU32(metadata.sectionCount) ||
            !cursor.readU32Values(metadata.sectionFirstFrames) ||
            !cursor.readBytes(metadata.sectionNames.data(), metadata.sectionNames.size())) {
            return fail("unexpected end of cRP before sprite names");
        }
        metadata.maskSelectionMode = static_cast<int32_t>(maskSelectionMode);
        metadata.sectionCount =
            std::min<uint32_t>(metadata.sectionCount, static_cast<uint32_t>(document::kMaxSections));

        if (!cursor.readBytes(metadata.spriteNames.data(), metadata.spriteNames.size())) {
            return fail("unexpected end of cRP while reading sprite names");
        }
        originalData.spriteNames.reserve(cromMetadata.spriteCount);
        for (std::size_t spriteIndex = 0;
             spriteIndex < cromMetadata.spriteCount && spriteIndex < kMaxSpriteNames;
             ++spriteIndex) {
            originalData.spriteNames.push_back(
                document::fixedName(metadata.spriteNames, spriteIndex, kSpriteNameBytes));
        }
    }

    if (cursor.offset() < size) {
        if (!cursor.readU32Values(metadata.spriteSourceFrames)) {
            return fail("unexpected end of cRP before frame durations");
        }
        originalData.frameDurationsMs.resize(cromMetadata.frameCount);
        if (!cursor.readU32Values(originalData.frameDurationsMs)) {
            return fail("unexpected end of cRP while reading frame durations");
        }
    }

    bool hasPalettes = false;
    if (cursor.offset() < size) {
        if (!cursor.readU16Values(metadata.spriteSourceRects) ||
            !cursor.readU32Values(metadata.spriteSourceMirrors)) {
            return fail("unexpected end of cRP before palettes");
        }
        const std::size_t paletteBlockBytes =
            kPaletteCount * kPaletteColorCount * sizeof(uint16_t) +
            metadata.editColors.size() * sizeof(uint16_t) +
            sizeof(uint32_t) +
            metadata.imagePositionNames.size() +
            metadata.imagePositions.size() * sizeof(int32_t) +
            metadata.paletteNames.size();
        hasPalettes = remaining() >= paletteBlockBytes;
    }

    if (hasPalettes) {
        originalData.palettes.assign(kPaletteCount, document::PaletteRecord{});
        for (document::PaletteRecord& palette : originalData.palettes) {
            palette.colors.resize(kPaletteColorCount);
            if (!cursor.readU16Values(palette.colors)) {
                return fail("unexpected end of cRP while reading palettes");
            }
        }
        std::vector<uint32_t> imagePositions(metadata.imagePositions.size());
        if (!cursor.readU16Values(metadata.editColors) ||
            !cursor.readU32(metadata.imagePositionCount) ||
            !cursor.readBytes(metadata.imagePositionNames.data(),
                              metadata.imagePositionNames.size()) ||
            !cursor.readU32Values(imagePositions)) {
            return fail("unexpected end of cRP before palette names");
        }
        std::transform(imagePositions.begin(),
                       imagePositions.end(),
                       metadata.imagePositions.begin(),
                       [](uint32_t value) { return static_cast<int32_t>(value); });
        metadata.imagePositionCount =
            std::min<uint32_t>(metadata.imagePositionCount,
                               static_cast<uint32_t>(document::kMaxImagePositions));
        if (!cursor.readBytes(metadata.paletteNames.data(), metadata.paletteNames.size())) {
            return fail("unexpected end of cRP while reading palette names");
        }
        for (std::size_t paletteIndex = 0; paletteIndex < kPaletteCount; ++paletteIndex) {
            originalData.palettes[paletteIndex].name =
                document::fixedName(metadata.paletteNames, paletteIndex, kPaletteNameBytes);
        }

        uint32_t importedPaletteCount = 0;
        if (remaining() >= sizeof(uint32_t)) {
            if (!cursor.readU32(importedPaletteCount) ||
                importedPaletteCount > remaining() / document::kImportedPaletteBytes) {
                return fail("unexpected end of cRP while reading imported palettes");
            }
            metadata.importedPalettes.resize(importedPaletteCount *
                                             document::kImportedPaletteBytes);
            if (!cursor.readBytes(metadata.importedPalettes.data(),
                                  metadata.importedPalettes.size())) {
                return fail("unexpected end of cRP while reading imported palettes");
            }
        }
        if (remaining() >= sizeof(uint32_t) && !cursor.readU32(metadata.timeElapsedMs)) {
            return fail("unexpected end of cRP while reading elapsed time");
        }
        if (remaining() >= sizeof(uint32_t) + metadata.pupPackName.size() &&
            (!cursor.readU32(metadata.hasPupPack) ||
             !cursor.readBytes(metadata.pupPackName.data(), metadata.pupPackName.size()))) {
            return fail("unexpected end of cRP while reading PuP pack name");
        }
    }

    LegacyCrpOriginalParseResult result;
    result.originalData = std::move(originalData);
    result.nextOffset = cursor.offset();
    result.ok = true;
    return result;
}

}  // namespace colorizing::legacy
