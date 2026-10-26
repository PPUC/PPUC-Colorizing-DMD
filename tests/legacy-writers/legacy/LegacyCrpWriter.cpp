#include "legacy/LegacyCrpWriter.h"

#include "document/EditorMetadata.h"
#include "legacy/LegacyByteWriter.h"

namespace colorizing::legacy {

namespace {

using detail::kRomNameBytes;
using detail::Writer;
using detail::WriteCancelled;

std::size_t pixelCount(render::Size size)
{
    return static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
}

bool writeCrpContent(std::ostream& stream,
                     const document::ProjectSnapshot& snapshot,
                     std::string& errorMessage,
                     LegacySectionOffsets* sections,
                     LegacyIoProgress* progress,
                     bool withPupPack)
{
    if (!document::isValidProjectSnapshot(snapshot, &errorMessage)) {
        return false;
    }

    // Sprite and palette names are edited through the model; everything else is carried in the
    // metadata tables. Unchanged names keep their original bytes.
    document::EditorMetadata metadata = snapshot.editorMetadata;
    document::ensureSpriteTableEntries(metadata, snapshot.sprites.size());
    for (std::size_t i = 0; i < snapshot.sprites.size(); ++i) {
        document::setFixedName(metadata.spriteNames,
                               i,
                               document::kSpriteNameBytes,
                               snapshot.sprites[i].name);
    }
    const bool hasPalettes = snapshot.palettes.size() == document::kPaletteCount;
    for (std::size_t i = 0; hasPalettes && i < snapshot.palettes.size(); ++i) {
        document::setFixedName(metadata.paletteNames,
                               i,
                               document::kPaletteNameBytes,
                               snapshot.palettes[i].name);
    }

    Writer writer(stream, sections, progress);
    writer.section("ROM name");
    writer.name(snapshot.romName, kRomNameBytes);
    writer.section("original frames");
    for (const auto& frame : snapshot.frames) {
        writer.bytes(frame.originalSd.pixels);
    }
    writer.section("color sets");
    writer.u32s(metadata.activeColorSets);
    writer.u16s(metadata.colorSets);
    writer.u8(metadata.currentColorSet);
    writer.u8(metadata.firstDisplayedColorSet);
    writer.bytes(metadata.colorSetNames);
    writer.section("draw settings");
    writer.u32(metadata.drawColorMode);
    writer.u8(metadata.drawMode);
    writer.u32(static_cast<uint32_t>(metadata.maskSelectionMode));
    writer.u32(metadata.fillMode);
    writer.section("mask names");
    writer.bytes(metadata.maskNames);
    writer.section("sections");
    writer.u32(metadata.sectionCount);
    writer.u32s(metadata.sectionFirstFrames);
    writer.bytes(metadata.sectionNames);
    // The cRP has room for kCrpSpriteEntries sprites; the .cRPX keeps the entries beyond.
    document::truncateSpriteTableEntries(metadata, document::kCrpSpriteEntries);
    writer.section("sprite names");
    writer.bytes(metadata.spriteNames);
    writer.section("sprite source frames");
    writer.u32s(metadata.spriteSourceFrames);
    writer.section("frame durations");
    for (const auto& frame : snapshot.frames) {
        writer.u32(frame.durationMs);
    }
    writer.section("sprite source rectangles");
    writer.u16s(metadata.spriteSourceRects);
    writer.u32s(metadata.spriteSourceMirrors);
    writer.section("palettes");
    for (std::size_t i = 0; i < document::kPaletteCount; ++i) {
        writer.u16sOr(hasPalettes ? snapshot.palettes[i].colors : std::vector<uint16_t>{},
                      document::kPaletteColorCount,
                      0);
    }
    writer.section("edit colors");
    writer.u16s(metadata.editColors);
    writer.section("image positions");
    writer.u32(metadata.imagePositionCount);
    writer.bytes(metadata.imagePositionNames);
    for (int32_t value : metadata.imagePositions) {
        writer.u32(static_cast<uint32_t>(value));
    }
    writer.section("palette names");
    writer.bytes(metadata.paletteNames);
    writer.section("imported palettes");
    writer.u32(static_cast<uint32_t>(metadata.importedPalettes.size() /
                                     document::kImportedPaletteBytes));
    writer.bytes(metadata.importedPalettes);
    writer.section("elapsed time");
    writer.u32(metadata.timeElapsedMs);
    writer.section("PuP pack");
    writer.u32(withPupPack ? metadata.hasPupPack : 0);
    writer.bytes(document::legacyPupPackNameBytes(withPupPack ? metadata.pupPackName
                                                              : std::string()));
    writer.section("end");

    if (!writer.finish()) {
        errorMessage = "could not write cRP data";
        return false;
    }
    return true;
}

}  // namespace

bool writeLegacyCrp(std::ostream& stream,
                    const document::ProjectSnapshot& snapshot,
                    std::string& errorMessage,
                    LegacySectionOffsets* sections,
                    LegacyIoProgress* progress)
{
    try {
        return writeCrpContent(stream, snapshot, errorMessage, sections, progress, true);
    } catch (const WriteCancelled&) {
        errorMessage = kLegacyIoCancelledMessage;
        return false;
    }
}

bool writeCrpxCrpPart(std::ostream& stream,
                      const document::ProjectSnapshot& snapshot,
                      std::string& errorMessage,
                      LegacySectionOffsets* sections,
                      LegacyIoProgress* progress)
{
    try {
        return writeCrpContent(stream, snapshot, errorMessage, sections, progress, false);
    } catch (const WriteCancelled&) {
        errorMessage = kLegacyIoCancelledMessage;
        return false;
    }
}

uint64_t legacyCrpByteCount(const document::ProjectSnapshot& snapshot)
{
    const document::EditorMetadata& metadata = snapshot.editorMetadata;
    const uint64_t frames = snapshot.frames.size();
    const uint64_t rom = pixelCount(document::originalFrameSize(snapshot));
    return kRomNameBytes + frames * rom + 4 * metadata.activeColorSets.size() +
           2 * metadata.colorSets.size() + 1 + 1 + metadata.colorSetNames.size() + 4 + 1 + 4 +
           4 + metadata.maskNames.size() + 4 + 4 * metadata.sectionFirstFrames.size() +
           metadata.sectionNames.size() +
           document::kCrpSpriteEntries * (document::kSpriteNameBytes + 4 + 2 * 4 + 4 * 2) +
           4 * frames +
           2 * document::kPaletteCount * document::kPaletteColorCount +
           2 * metadata.editColors.size() + 4 + metadata.imagePositionNames.size() +
           4 * metadata.imagePositions.size() + metadata.paletteNames.size() + 4 +
           metadata.importedPalettes.size() + 4 + 4 + document::kPupPackNameBytes;
}

}  // namespace colorizing::legacy
