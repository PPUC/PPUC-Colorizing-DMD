#include "edit/StructureCommand.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/EditorMetadata.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/EditCommand.h"
#include "edit/UndoHistories.h"
#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacyCrpOriginals.h"
#include "legacy/LegacyProjectSerializer.h"
#include "legacy/LegacySnapshotBuilder.h"
#include "runtime/RuntimePreview.h"

using colorizing::document::BackgroundRecord;
using colorizing::document::EditorMetadata;
using colorizing::document::FrameSpriteReference;
using colorizing::document::IndexedFrame;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::Rgb565Frame;
using colorizing::document::SpriteRecord;
using colorizing::document::fixedName;
using colorizing::document::kImportedPaletteBytes;
using colorizing::document::kMaskNameBytes;
constexpr std::size_t kMaxBackgrounds = colorizing::document::kMaxBackgrounds;
using colorizing::document::kMaxComparisonMasks;
constexpr std::size_t kMaxSprites = colorizing::document::kMaxSprites;
using colorizing::document::kNoBackgroundId;
using colorizing::document::kNoComparisonMaskId;
using colorizing::document::kNoSpriteId;
using colorizing::document::kNoSpriteSourceRect;
using colorizing::document::kSectionNameBytes;
using colorizing::document::kSpriteNameBytes;
using colorizing::document::setFixedName;
using colorizing::edit::EditCommand;
using colorizing::edit::EditKind;
using colorizing::edit::FrameAttribute;
using colorizing::edit::FrameAttributeEditCommand;
using colorizing::edit::HistoryKey;
using colorizing::edit::MaskEditCommand;
using colorizing::edit::PixelEditCommand;
using colorizing::edit::RecordKind;
using colorizing::edit::StructureCommand;
using colorizing::edit::UndoHistories;
using colorizing::edit::historyKey;
using colorizing::edit::structureHistoryKey;
using colorizing::render::PlaneLayout;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::render::Resolution;
using colorizing::render::Size;
using colorizing::runtime::PlaneRequest;
using colorizing::runtime::RuntimeFrame;
using colorizing::runtime::RuntimePreview;
namespace fixtures = colorizing::runtime_fixtures;

namespace {

void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

template <typename ExceptionType, typename Func>
void expectThrows(Func func, const std::string& message)
{
    try {
        func();
    } catch (const ExceptionType&) {
        return;
    } catch (const std::exception& ex) {
        fail(message + " threw different exception: " + ex.what());
    }
    fail(message + " did not throw");
}

const std::vector<PlaneLayout> kLayouts{PlaneLayout::Ordinary, PlaneLayout::NativeExtra,
                                        PlaneLayout::NativeOriginal};

std::string layoutName(PlaneLayout layout)
{
    return layout == PlaneLayout::Ordinary
               ? "128x32"
               : layout == PlaneLayout::NativeExtra ? "256x64" : "192x64";
}

bool sameFrame(const std::optional<Rgb565Frame>& a, const std::optional<Rgb565Frame>& b)
{
    return a.has_value() == b.has_value() && (!a || a->pixels == b->pixels);
}

bool sameReferences(const std::vector<FrameSpriteReference>& a,
                    const std::vector<FrameSpriteReference>& b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](const FrameSpriteReference& x, const FrameSpriteReference& y) {
                          return x.spriteId == y.spriteId && x.minX == y.minX &&
                                 x.minY == y.minY && x.maxX == y.maxX && x.maxY == y.maxY;
                      });
}

// Everything of a frame except the IDs that point at other records.
bool sameFrameContent(const ProjectFrameSnapshot& a, const ProjectFrameSnapshot& b)
{
    const bool sameDynamic =
        a.dynamicData.has_value() == b.dynamicData.has_value() &&
        (!a.dynamicData ||
         (a.dynamicData->maskSd.pixels == b.dynamicData->maskSd.pixels &&
          a.dynamicData->maskHd.pixels == b.dynamicData->maskHd.pixels &&
          a.dynamicData->colorsSd == b.dynamicData->colorsSd &&
          a.dynamicData->colorsHd == b.dynamicData->colorsHd &&
          a.dynamicData->shadowDirectionsSd == b.dynamicData->shadowDirectionsSd &&
          a.dynamicData->shadowColorsSd == b.dynamicData->shadowColorsSd &&
          a.dynamicData->shadowDirectionsHd == b.dynamicData->shadowDirectionsHd &&
          a.dynamicData->shadowColorsHd == b.dynamicData->shadowColorsHd));
    const bool sameBackgroundMasks =
        a.backgroundData.has_value() == b.backgroundData.has_value() &&
        (!a.backgroundData ||
         (a.backgroundData->maskSd.pixels == b.backgroundData->maskSd.pixels &&
          a.backgroundData->maskHd.pixels == b.backgroundData->maskHd.pixels));
    return a.originalSd.pixels == b.originalSd.pixels &&
           a.colorizedSd.pixels == b.colorizedSd.pixels &&
           sameFrame(a.colorizedHd, b.colorizedHd) && sameFrame(a.inactiveHd, b.inactiveHd) &&
           a.shapeComparisonMode == b.shapeComparisonMode && sameDynamic &&
           sameBackgroundMasks && a.triggerId == b.triggerId && a.durationMs == b.durationMs &&
           a.colorRotationsSd == b.colorRotationsSd && a.colorRotationsHd == b.colorRotationsHd;
}

bool sameWholeFrame(const ProjectFrameSnapshot& a, const ProjectFrameSnapshot& b)
{
    return sameFrameContent(a, b) && a.comparisonMaskId == b.comparisonMaskId &&
           (!a.backgroundData ||
            a.backgroundData->backgroundId == b.backgroundData->backgroundId) &&
           sameReferences(a.spriteReferences, b.spriteReferences);
}

bool sameSprite(const SpriteRecord& a, const SpriteRecord& b)
{
    const auto sameAreas = [&]() {
        return a.detectionAreas.size() == b.detectionAreas.size() &&
               std::equal(a.detectionAreas.begin(), a.detectionAreas.end(),
                          b.detectionAreas.begin(), [](const auto& x, const auto& y) {
                              return x.x == y.x && x.y == y.y && x.width == y.width &&
                                     x.height == y.height;
                          });
    };
    return a.hasHd == b.hasHd && a.originalSd.pixels == b.originalSd.pixels &&
           a.colorizedSd.pixels == b.colorizedSd.pixels && a.maskHd.pixels == b.maskHd.pixels &&
           a.colorizedHd.pixels == b.colorizedHd.pixels &&
           a.detectionDwords == b.detectionDwords &&
           a.detectionDwordPositions == b.detectionDwordPositions && sameAreas() &&
           a.name == b.name && a.dynamicMaskSd.pixels == b.dynamicMaskSd.pixels &&
           a.dynamicMaskHd.pixels == b.dynamicMaskHd.pixels &&
           a.dynamicColorsSd == b.dynamicColorsSd && a.dynamicColorsHd == b.dynamicColorsHd &&
           a.shapeMode == b.shapeMode;
}

bool sameBackground(const BackgroundRecord& a, const BackgroundRecord& b)
{
    return a.colorizedSd.pixels == b.colorizedSd.pixels &&
           sameFrame(a.colorizedHd, b.colorizedHd) && sameFrame(a.inactiveHd, b.inactiveHd);
}

bool sameMetadata(const EditorMetadata& a, const EditorMetadata& b)
{
    return a.activeColorSets == b.activeColorSets && a.colorSets == b.colorSets &&
           a.currentColorSet == b.currentColorSet &&
           a.firstDisplayedColorSet == b.firstDisplayedColorSet &&
           a.colorSetNames == b.colorSetNames && a.drawColorMode == b.drawColorMode &&
           a.drawMode == b.drawMode && a.maskSelectionMode == b.maskSelectionMode &&
           a.fillMode == b.fillMode && a.maskNames == b.maskNames &&
           a.sectionCount == b.sectionCount && a.sectionFirstFrames == b.sectionFirstFrames &&
           a.sectionNames == b.sectionNames && a.spriteNames == b.spriteNames &&
           a.spriteSourceFrames == b.spriteSourceFrames &&
           a.spriteSourceRects == b.spriteSourceRects &&
           a.spriteSourceMirrors == b.spriteSourceMirrors && a.editColors == b.editColors &&
           a.imagePositionCount == b.imagePositionCount &&
           a.imagePositionNames == b.imagePositionNames && a.imagePositions == b.imagePositions &&
           a.paletteNames == b.paletteNames && a.importedPalettes == b.importedPalettes &&
           a.timeElapsedMs == b.timeElapsedMs && a.hasPupPack == b.hasPupPack &&
           a.pupPackName == b.pupPackName;
}

// Empty if the snapshots hold the same project, otherwise what differs first.
std::string difference(const ProjectSnapshot& a, const ProjectSnapshot& b)
{
    if (a.frames.size() != b.frames.size()) {
        return "frame count";
    }
    for (std::size_t f = 0; f < a.frames.size(); ++f) {
        if (!sameWholeFrame(a.frames[f], b.frames[f])) {
            return "frame " + std::to_string(f);
        }
    }
    if (a.comparisonMasks.size() != b.comparisonMasks.size()) {
        return "comparison mask count";
    }
    for (std::size_t m = 0; m < a.comparisonMasks.size(); ++m) {
        if (a.comparisonMasks[m].pixels != b.comparisonMasks[m].pixels) {
            return "comparison mask " + std::to_string(m);
        }
    }
    if (a.backgrounds.size() != b.backgrounds.size()) {
        return "background count";
    }
    for (std::size_t i = 0; i < a.backgrounds.size(); ++i) {
        if (!sameBackground(a.backgrounds[i], b.backgrounds[i])) {
            return "background " + std::to_string(i);
        }
    }
    if (a.sprites.size() != b.sprites.size()) {
        return "sprite count";
    }
    for (std::size_t i = 0; i < a.sprites.size(); ++i) {
        if (!sameSprite(a.sprites[i], b.sprites[i])) {
            return "sprite " + std::to_string(i);
        }
    }
    if (!sameMetadata(a.editorMetadata, b.editorMetadata)) {
        return "editor metadata";
    }
    if (a.romName != b.romName || a.colorCount != b.colorCount ||
        a.palettes.size() != b.palettes.size()) {
        return "project header";
    }
    return {};
}

void expectSame(const ProjectDocument& document, const ProjectSnapshot& expected,
                const std::string& what)
{
    const std::string differing = difference(document.toSnapshot(), expected);
    expect(differing.empty(), what + ": differs in " + differing);
}

// Every ID in a frame names a record that exists, or "none".
void expectConsistentReferences(const ProjectDocument& document, const std::string& what)
{
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        const uint8_t maskId = document.comparisonMaskId(f);
        expect(maskId == kNoComparisonMaskId || maskId < document.comparisonMaskCount(),
               what + ": frame " + std::to_string(f) + " names a mask that does not exist");
        if (document.backgroundData(f)) {
            const uint16_t backgroundId = document.backgroundId(f);
            expect(backgroundId == kNoBackgroundId || backgroundId < document.backgroundCount(),
                   what + ": frame " + std::to_string(f) +
                       " names a background that does not exist");
        }
        for (const FrameSpriteReference& reference : document.frameSpriteReferences(f)) {
            expect(reference.spriteId == kNoSpriteId ||
                       reference.spriteId < document.spriteCount(),
                   what + ": frame " + std::to_string(f) + " names a sprite that does not exist");
        }
    }
    std::string error;
    expect(colorizing::document::isValidProjectSnapshot(document.toSnapshot(), &error),
           what + ": the snapshot is valid: " + error);
}

struct Files {
    std::vector<uint8_t> crom;
    std::vector<uint8_t> crp;
};

Files serialize(const ProjectSnapshot& snapshot, const std::string& what)
{
    std::ostringstream crom;
    std::ostringstream crp;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrom(crom, snapshot, error),
           what + ": the cROM is written: " + error);
    expect(colorizing::legacy::writeLegacyCrp(crp, snapshot, error),
           what + ": the cRP is written: " + error);
    const std::string cromBytes = crom.str();
    const std::string crpBytes = crp.str();
    return {std::vector<uint8_t>(cromBytes.begin(), cromBytes.end()),
            std::vector<uint8_t>(crpBytes.begin(), crpBytes.end())};
}

ProjectSnapshot parse(const Files& files, const std::string& what)
{
    const auto crom = colorizing::legacy::parseModernLegacyCromFrames(files.crom);
    expect(crom.ok, what + ": the cROM parses: " + crom.errorMessage);
    expect(crom.nextOffset == files.crom.size(), what + ": the whole cROM is read");
    const auto crp =
        colorizing::legacy::parseLegacyCrpOriginalFrames(files.crp, crom.frameData.metadata);
    expect(crp.ok, what + ": the cRP parses: " + crp.errorMessage);
    expect(crp.nextOffset == files.crp.size(), what + ": the whole cRP is read");
    const auto snapshot =
        colorizing::legacy::buildProjectSnapshot(crom.frameData, crp.originalData);
    expect(snapshot.ok, what + ": a snapshot is built: " + snapshot.errorMessage);
    return snapshot.snapshot;
}

bool samePicture(const RuntimeFrame& a, const RuntimeFrame& b)
{
    const auto samePlane = [](const auto& x, const auto& y) {
        return x.has_value() == y.has_value() && (!x || x->image.pixels == y->image.pixels);
    };
    return a.identified == b.identified && a.skipped == b.skipped &&
           samePlane(a.rows32, b.rows32) && samePlane(a.rows64, b.rows64);
}

// The document saves to files that load again as the same project, and libserum, given the
// complete saved cROM, identifies every frame as itself and renders it as it renders the
// frame alone.
void expectSavesAndRenders(const ProjectDocument& document, const std::string& what)
{
    expectConsistentReferences(document, what);
    const Files files = serialize(document.toSnapshot(), what);
    const ProjectDocument reloaded = ProjectDocument::fromSnapshot(parse(files, what));
    const Files again = serialize(reloaded.toSnapshot(), what + ", reloaded");
    expect(files.crom == again.crom, what + ": the reloaded project writes the same cROM");
    expect(files.crp == again.crp, what + ": the reloaded project writes the same cRP");
    expect(reloaded.frameCount() == document.frameCount() &&
               reloaded.spriteCount() == document.spriteCount() &&
               reloaded.backgroundCount() == document.backgroundCount() &&
               reloaded.comparisonMaskCount() == document.comparisonMaskCount(),
           what + ": the reloaded project has the same record counts");
    for (std::size_t s = 0; s < document.spriteCount(); ++s) {
        expect(sameSprite(reloaded.toSnapshot().sprites[s], document.toSnapshot().sprites[s]),
               what + ": sprite " + std::to_string(s) + " is reloaded as it was");
    }

    RuntimePreview& preview = RuntimePreview::instance();
    const PlaneRequest request = fixtures::nativeRequest(document.geometry());
    std::vector<RuntimeFrame> alone;
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        alone.push_back(preview.renderFrame(document, f, request));
    }
    std::string error;
    expect(preview.openCrom(files.crom, document.geometry(), document.frameCount(), request,
                            &error),
           what + ": libserum loads the saved cROM: " + error);
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        const RuntimeFrame frame = preview.colorize(document.originalFrame(f));
        const std::string frameText = what + ": frame " + std::to_string(f);
        expect(frame.ok(), frameText + " is colorized: " + frame.error);
        expect(frame.identified, frameText + " is identified");
        expect(frame.unchanged || frame.frameIndex == f, frameText + " is identified as itself");
        expect(frame.unchanged || samePicture(frame, alone[f]),
               frameText + " renders from the saved cROM as it renders alone");
    }
    preview.close();
}

std::vector<RuntimeFrame> renderAll(const ProjectDocument& document)
{
    std::vector<RuntimeFrame> frames;
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        frames.push_back(RuntimePreview::instance().renderFrame(
            document, f, fixtures::nativeRequest(document.geometry())));
    }
    return frames;
}

// The full fixture with cRP tables that have something in every entry the operations move,
// including entries behind the last record, which a Win32 file can hold.
ProjectSnapshot makeSnapshot(PlaneLayout layout)
{
    ProjectSnapshot snapshot = fixtures::makeFullProject(layout);
    EditorMetadata& metadata = snapshot.editorMetadata;
    for (std::size_t f = 0; f < snapshot.frames.size(); ++f) {
        snapshot.frames[f].durationMs = static_cast<uint32_t>(100 + f);
        snapshot.frames[f].triggerId = static_cast<uint32_t>(7000 + f);
    }
    // Frames 0 to 5 have an imported palette.
    metadata.importedPalettes.assign(6 * kImportedPaletteBytes, 0);
    for (std::size_t f = 0; f < 6; ++f) {
        std::fill_n(metadata.importedPalettes.begin() +
                        static_cast<std::ptrdiff_t>(f * kImportedPaletteBytes),
                    kImportedPaletteBytes,
                    static_cast<uint8_t>(0x10 + f));
    }
    metadata.sectionCount = 3;
    metadata.sectionFirstFrames[0] = 0;
    metadata.sectionFirstFrames[1] = 3;
    metadata.sectionFirstFrames[2] = 6;
    setFixedName(metadata.sectionNames, 0, kSectionNameBytes, "Attract");
    setFixedName(metadata.sectionNames, 1, kSectionNameBytes, "Multiball");
    setFixedName(metadata.sectionNames, 2, kSectionNameBytes, "Bonus");
    setFixedName(metadata.sectionNames, 3, kSectionNameBytes, "left behind");
    metadata.sectionFirstFrames[3] = 77;
    for (std::size_t s = 0; s < 5; ++s) {
        const std::string name = "sprite " + std::to_string(s);
        if (s < snapshot.sprites.size()) {
            snapshot.sprites[s].name = name;
        }
        setFixedName(metadata.spriteNames, s, kSpriteNameBytes, name);
        metadata.spriteSourceFrames[s] = static_cast<uint32_t>(2 + 2 * s);
        for (std::size_t i = 0; i < 4; ++i) {
            metadata.spriteSourceRects[s * 4 + i] = static_cast<uint16_t>(10 * s + i + 1);
        }
        metadata.spriteSourceMirrors[s * 2] = static_cast<uint32_t>(s % 2);
        metadata.spriteSourceMirrors[s * 2 + 1] = 1;
    }
    for (std::size_t m = 0; m < 5; ++m) {
        setFixedName(metadata.maskNames, m, kMaskNameBytes, "mask " + std::to_string(m));
    }
    setFixedName(metadata.maskNames, kMaxComparisonMasks - 1, kMaskNameBytes, "last slot");
    return snapshot;
}

ProjectDocument makeDocument(PlaneLayout layout)
{
    ProjectDocument document = ProjectDocument::fromSnapshot(makeSnapshot(layout));
    document.markClean();
    return document;
}

std::vector<uint8_t> importedPaletteMarks(const ProjectDocument& document)
{
    std::vector<uint8_t> marks;
    const std::vector<uint8_t>& palettes = document.editorMetadata().importedPalettes;
    for (std::size_t offset = 0; offset < palettes.size(); offset += kImportedPaletteBytes) {
        marks.push_back(palettes[offset]);
    }
    return marks;
}

void testDeleteFrames(PlaneLayout layout, const std::vector<std::size_t>& deleted)
{
    std::string what = layoutName(layout) + ", delete frames";
    for (std::size_t frame : deleted) {
        what += " " + std::to_string(frame);
    }
    ProjectDocument document = makeDocument(layout);
    const ProjectSnapshot before = document.toSnapshot();
    const std::vector<RuntimeFrame> picturesBefore = renderAll(document);
    const uint64_t identificationBefore = document.identificationRevision();

    const std::unique_ptr<StructureCommand> command =
        colorizing::edit::deleteFrames(document, deleted);
    expect(command != nullptr, what + ": there is a command");
    expect(command->recordKind() == RecordKind::Frame && !command->addsRecords() &&
               !command->recordsPresent(),
           what + ": the command describes a deletion");
    expect(document.isDirty(), what + ": the document is modified");
    expect(document.identificationRevision() != identificationBefore,
           what + ": frame identification is out of date");

    const std::set<std::size_t> gone(deleted.begin(), deleted.end());
    std::vector<std::size_t> remaining;
    for (std::size_t f = 0; f < before.frames.size(); ++f) {
        if (gone.count(f) == 0) {
            remaining.push_back(f);
        }
    }
    const ProjectSnapshot after = document.toSnapshot();
    expect(after.frames.size() == remaining.size(), what + ": frame count");
    const std::vector<RuntimeFrame> picturesAfter = renderAll(document);
    for (std::size_t k = 0; k < remaining.size(); ++k) {
        expect(sameWholeFrame(after.frames[k], before.frames[remaining[k]]),
               what + ": frame " + std::to_string(k) + " is the old frame " +
                   std::to_string(remaining[k]));
        // libserum keeps the 256x64 dynamic data of sprite N only if frame N has 256x64
        // data, so there the picture of a frame with sprites depends on the frame numbers.
        const bool dependsOnNumbers = layout == PlaneLayout::NativeExtra &&
                                      !before.frames[remaining[k]].spriteReferences.empty();
        expect(dependsOnNumbers || samePicture(picturesAfter[k], picturesBefore[remaining[k]]),
               what + ": frame " + std::to_string(k) + " renders as the old frame " +
                   std::to_string(remaining[k]));
    }
    expect(after.comparisonMasks.size() == before.comparisonMasks.size() &&
               after.backgrounds.size() == before.backgrounds.size() &&
               after.sprites.size() == before.sprites.size(),
           what + ": masks, backgrounds and sprites stay");

    std::vector<uint8_t> expectedMarks;
    for (std::size_t f : remaining) {
        if (f < 6) {
            expectedMarks.push_back(static_cast<uint8_t>(0x10 + f));
        }
    }
    expect(importedPaletteMarks(document) == expectedMarks,
           what + ": the imported palettes of the remaining frames stay, in order");

    const EditorMetadata& metadata = document.editorMetadata();
    for (std::size_t s = 0; s < before.sprites.size(); ++s) {
        const std::size_t source = before.editorMetadata.spriteSourceFrames[s];
        const std::size_t lower =
            static_cast<std::size_t>(std::count_if(deleted.begin(), deleted.end(),
                                                   [source](std::size_t d) { return d < source; }));
        expect(metadata.spriteSourceFrames[s] == source - lower,
               what + ": source frame of sprite " + std::to_string(s));
        expect(metadata.spriteSourceRects[s * 4] ==
                   (gone.count(source) ? kNoSpriteSourceRect
                                       : before.editorMetadata.spriteSourceRects[s * 4]),
               what + ": source rectangle of sprite " + std::to_string(s));
    }
    // Sprites behind the last one are not touched.
    expect(metadata.spriteSourceFrames[4] == before.editorMetadata.spriteSourceFrames[4] &&
               metadata.spriteSourceRects[16] == before.editorMetadata.spriteSourceRects[16],
           what + ": unused sprite table entries stay");
    expectSavesAndRenders(document, what);

    command->revert(document);
    expect(command->recordsPresent(), what + ": undone, the frames are back");
    expectSame(document, before, what + ", undone");
    command->apply(document);
    expectSame(document, after, what + ", redone");
    command->revert(document);
    expectSame(document, before, what + ", undone again");
}

void testDeleteFramesRejects()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    const ProjectSnapshot before = document.toSnapshot();
    expect(colorizing::edit::deleteFrames(document, {}) == nullptr,
           "deleting no frame is no command");
    expectThrows<std::logic_error>(
        [&]() { (void)colorizing::edit::deleteFrames(document, {0, 1, 2, 3, 4, 5, 6, 7, 8}); },
        "deleting every frame");
    expectThrows<std::out_of_range>(
        [&]() { (void)colorizing::edit::deleteFrames(document, {2, 9}); },
        "deleting a frame that does not exist");
    expectSame(document, before, "a refused deletion");
    expect(!document.isDirty(), "a refused deletion leaves the document clean");

    // Any order, and a frame named twice.
    const auto command = colorizing::edit::deleteFrames(document, {7, 2, 7, 4});
    expect(command->recordIndices() == std::vector<std::size_t>({2, 4, 7}),
           "the command holds the frames once, ascending");
    expect(document.frameCount() == 6, "three frames are deleted");

    ProjectDocument single = ProjectDocument::fromSnapshot(
        fixtures::makeProject(PlaneLayout::Ordinary, 1));
    expectThrows<std::logic_error>([&]() { (void)colorizing::edit::deleteFrames(single, {0}); },
                                   "deleting the only frame");
}

struct Section {
    uint32_t first = 0;
    std::string name;
};

std::vector<Section> sections(const EditorMetadata& metadata)
{
    std::vector<Section> result;
    for (std::size_t s = 0; s < metadata.sectionCount; ++s) {
        result.push_back({metadata.sectionFirstFrames[s],
                          fixedName(metadata.sectionNames, s, kSectionNameBytes)});
    }
    return result;
}

// Delete_Frame's section handling, one frame at a time: Del_Section_Frame, then the first
// frames behind the deleted one move down.
std::vector<Section> win32DeleteFrame(std::vector<Section> list, uint32_t frame)
{
    const auto sectionAt = [&list](uint32_t first) {
        for (std::size_t s = 0; s < list.size(); ++s) {
            if (list[s].first == first) {
                return static_cast<int>(s);
            }
        }
        return -1;
    };
    const int section = sectionAt(frame);
    if (section > -1 && sectionAt(frame + 1) > -1) {
        list.erase(list.begin() + section);
    }
    for (Section& entry : list) {
        if (entry.first > frame) {
            --entry.first;
        }
    }
    return list;
}

void testFrameDeletionSections()
{
    const auto run = [](const std::vector<uint32_t>& firsts,
                        const std::vector<std::size_t>& deleted, std::size_t frameCount) {
        ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, frameCount);
        snapshot.editorMetadata.sectionCount = static_cast<uint32_t>(firsts.size());
        for (std::size_t s = 0; s < firsts.size(); ++s) {
            snapshot.editorMetadata.sectionFirstFrames[s] = firsts[s];
            setFixedName(snapshot.editorMetadata.sectionNames, s, kSectionNameBytes,
                         "S" + std::to_string(s));
        }
        ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
        const auto command = colorizing::edit::deleteFrames(document, deleted);
        std::vector<Section> result = sections(document.editorMetadata());
        // Nothing of a removed section is left behind the list.
        for (std::size_t s = result.size(); s < firsts.size(); ++s) {
            expect(document.editorMetadata().sectionFirstFrames[s] == 0 &&
                       fixedName(document.editorMetadata().sectionNames, s, kSectionNameBytes)
                           .empty(),
                   "a removed section leaves an empty table entry");
        }
        command->revert(document);
        expect(difference(document.toSnapshot(),
                          ProjectDocument::fromSnapshot(snapshot).toSnapshot())
                   .empty(),
               "undoing the deletion restores the sections");
        return result;
    };
    const auto text = [](const std::vector<Section>& list) {
        std::string result;
        for (const Section& entry : list) {
            result += entry.name + "@" + std::to_string(entry.first) + " ";
        }
        return result;
    };

    // The first frame of a section goes: the section starts at the frame behind it.
    expect(text(run({0, 3, 6}, {3}, 9)) == "S0@0 S1@3 S2@5 ", "section start deleted");
    // A frame inside a section: later sections move down.
    expect(text(run({0, 3, 6}, {1}, 9)) == "S0@0 S1@2 S2@5 ", "frame inside a section deleted");
    // A whole section goes: it is removed, the next one takes its place.
    expect(text(run({0, 3, 6}, {3, 4, 5}, 9)) == "S0@0 S2@3 ", "emptied section is removed");
    // A section of one frame followed by a section start (Del_Section_Frame's own case).
    expect(text(run({0, 3, 4}, {3}, 9)) == "S0@0 S2@3 ", "one-frame section is removed");
    // Two sections emptied at once, and the start of the third deleted too.
    expect(text(run({0, 2, 4}, {0, 1, 2, 3, 4}, 9)) == "S2@0 ", "several sections emptied");
    // The last frames go with their section: nothing is left to start at. The Win32 editor
    // keeps such a section pointing behind the last frame; here it is removed.
    expect(text(run({0, 3, 6}, {6, 7, 8}, 9)) == "S0@0 S1@3 ", "section at the end emptied");
    expect(text(run({0, 3, 8}, {8}, 9)) == "S0@0 S1@3 ", "last frame was a section start");
    // Two sections that start at the same frame stay together.
    expect(text(run({2, 2, 5}, {2}, 9)) == "S0@2 S1@2 S2@4 ", "sections sharing a start");

    // Every set of frames of a small project, against the Win32 editor deleting them one by
    // one, in ascending and in descending order. Sets that empty a section at the end of
    // the project are left out: there the Win32 editor keeps a section without frames.
    const std::vector<uint32_t> firsts{0, 2, 3, 6};
    const std::size_t frameCount = 8;
    std::size_t compared = 0;
    for (unsigned set = 1; set + 1 < (1u << frameCount); ++set) {
        std::vector<std::size_t> deleted;
        for (std::size_t f = 0; f < frameCount; ++f) {
            if (set & (1u << f)) {
                deleted.push_back(f);
            }
        }
        bool emptiesTail = true;
        for (std::size_t f = firsts.back(); f < frameCount; ++f) {
            emptiesTail = emptiesTail && (set & (1u << f)) != 0;
        }
        if (emptiesTail) {
            continue;
        }
        std::vector<Section> ascending;
        for (std::size_t s = 0; s < firsts.size(); ++s) {
            ascending.push_back({firsts[s], "S" + std::to_string(s)});
        }
        std::vector<Section> descending = ascending;
        for (std::size_t i = 0; i < deleted.size(); ++i) {
            // Earlier deletions moved the later frames down.
            ascending = win32DeleteFrame(ascending, static_cast<uint32_t>(deleted[i] - i));
            descending = win32DeleteFrame(
                descending, static_cast<uint32_t>(deleted[deleted.size() - 1 - i]));
        }
        const std::string result = text(run(firsts, deleted, frameCount));
        expect(result == text(ascending) && result == text(descending),
               "sections after deleting frame set " + std::to_string(set) + ": " + result +
                   "| Win32 ascending " + text(ascending) + "| descending " + text(descending));
        ++compared;
    }
    expect(compared > 150, "the Win32 section rule was compared for many frame sets");
}

void testFramesShorterThan()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    document.setFrameDurationMs(1, 14);
    document.setFrameDurationMs(4, 20);
    document.setFrameDurationMs(6, 0);
    expect(colorizing::edit::framesShorterThan(document, {0, 1, 4, 6, 7}, 21) ==
               std::vector<std::size_t>({1, 4, 6}),
           "frames shorter than 21 ms among the given ones");
    expect(colorizing::edit::framesShorterThan(document, {0, 1, 4, 6, 7}, 20) ==
               std::vector<std::size_t>({1, 6}),
           "a frame of exactly the duration is not shorter");
    expect(colorizing::edit::framesShorterThan(document, {0, 7}, 50).empty(),
           "only the given frames are looked at");
    expect(colorizing::edit::framesShorterThan(document, {1}, 0).empty(),
           "nothing is shorter than 0 ms");
}

void testAddSprite(PlaneLayout layout)
{
    const std::string what = layoutName(layout) + ", add sprite";
    ProjectDocument document = makeDocument(layout);
    const ProjectSnapshot before = document.toSnapshot();

    expectThrows<std::invalid_argument>(
        [&]() { (void)colorizing::edit::addSprite(document, "", 0); }, what + " without a name");
    expectThrows<std::invalid_argument>(
        [&]() { (void)colorizing::edit::addSprite(document, std::string(32, 'x'), 0); },
        what + " with a name of 32 characters");
    expectThrows<std::invalid_argument>(
        [&]() { (void)colorizing::edit::addSprite(document, "sprite 1", 0); },
        what + " with a name in use");
    expectSame(document, before, what + ", refused");

    const std::string name(31, 'n');
    const auto command = colorizing::edit::addSprite(document, name, 5);
    expect(command->recordKind() == RecordKind::Sprite && command->addsRecords() &&
               command->recordsPresent() &&
               command->recordIndices() == std::vector<std::size_t>({3}),
           what + ": the command describes the new sprite");
    expect(document.spriteCount() == 4, what + ": sprite count");
    const SpriteRecord sprite = document.sprite(3);
    const auto all = [](const auto& values, auto value) {
        return std::all_of(values.begin(), values.end(),
                           [value](auto entry) { return entry == value; });
    };
    expect(sprite.name == name && all(sprite.originalSd.pixels, uint8_t{255}) &&
               all(sprite.colorizedSd.pixels, uint16_t{0}) &&
               all(sprite.dynamicMaskSd.pixels, uint8_t{255}) &&
               all(sprite.dynamicColorsSd, uint16_t{0}) && sprite.shapeMode == 0,
           what + ": the sprite is blank");
    for (std::size_t area = 0; area < 4; ++area) {
        expect(sprite.detectionAreas[area].x == 0xffff, what + ": no detection area");
    }
    // What the runtime needs: the 256x64 data in a project that renders only that, none
    // where there is no extra plane, and no HD as in the Win32 editor otherwise.
    expect(sprite.hasHd == (layout == PlaneLayout::NativeExtra), what + ": extra-plane flag");
    const ProjectSnapshot after = document.toSnapshot();
    expect(after.sprites[3].maskHd.pixels.size() == 256 * 64 &&
               all(after.sprites[3].maskHd.pixels, uint8_t{255}) &&
               all(after.sprites[3].colorizedHd.pixels, uint16_t{0}),
           what + ": the extra-plane sprite is written empty");
    const EditorMetadata& metadata = document.editorMetadata();
    expect(metadata.spriteSourceFrames[3] == 5 &&
               metadata.spriteSourceRects[12] == kNoSpriteSourceRect &&
               metadata.spriteSourceMirrors[6] == 0 && metadata.spriteSourceMirrors[7] == 0 &&
               fixedName(metadata.spriteNames, 3, kSpriteNameBytes) == name,
           what + ": the sprite's cRP entries");
    expect(metadata.spriteSourceFrames[2] == before.editorMetadata.spriteSourceFrames[2] &&
               metadata.spriteSourceFrames[4] == before.editorMetadata.spriteSourceFrames[4],
           what + ": other cRP entries stay");
    expectSavesAndRenders(document, what);

    // The sprite is used and edited, then the addition is undone: the frames lose it.
    document.setFrameSpriteReference(4, 2, FrameSpriteReference{3, 1, 2, 30, 20});
    document.setFrameSpriteReference(3, 1, FrameSpriteReference{3, 0, 0, 9, 9});
    document.setSpriteColorizedPixel(3, layout == PlaneLayout::NativeExtra ? Resolution::Hd
                                                                           : Resolution::Sd,
                                     1, 1, 0x1234);
    const ProjectSnapshot used = document.toSnapshot();
    command->revert(document);
    expect(!command->recordsPresent(), what + ": undone, the sprite is gone");
    expectSame(document, before, what + ", undone");
    expectConsistentReferences(document, what + ", undone");
    command->apply(document);
    expectSame(document, used, what + ", redone");
    command->revert(document);
    expectSame(document, before, what + ", undone again");
}

void testSpriteLimit()
{
    ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, 2);
    snapshot.sprites.assign(kMaxSprites - 1, fixtures::emptySprite());
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const auto last = colorizing::edit::addSprite(document, "the last one", 0);
    expect(document.spriteCount() == kMaxSprites, "as many sprites as allowed fit");
    expectThrows<std::length_error>(
        [&]() { (void)colorizing::edit::addSprite(document, "one too many", 0); },
        "adding one more sprite");
    expect(document.spriteCount() == kMaxSprites, "a refused sprite is not added");
    // The last possible sprite can go and come back.
    last->revert(document);
    expect(document.spriteCount() == kMaxSprites - 1, "sprite 255 undone");
    last->apply(document);
    expect(document.sprite(kMaxSprites - 1).name == "the last one", "sprite 255 redone");
    const auto deleted = colorizing::edit::deleteSprite(document, 0);
    deleted->revert(document);
    expect(document.spriteCount() == kMaxSprites, "a sprite deleted at the limit comes back");
}

void testDeleteSprite(PlaneLayout layout, std::size_t deleted)
{
    const std::string what =
        layoutName(layout) + ", delete sprite " + std::to_string(deleted);
    ProjectDocument document = makeDocument(layout);
    // Sprite 1 in many frames and slots: twice in frame 5, in the last slot of frame 6 with
    // other sprites around it, alone in frame 7.
    document.setFrameSpriteReference(5, 0, FrameSpriteReference{1, 1, 1, 20, 20});
    document.setFrameSpriteReference(5, 1, FrameSpriteReference{0, 2, 2, 21, 21});
    document.setFrameSpriteReference(5, 2, FrameSpriteReference{1, 3, 3, 22, 22});
    document.setFrameSpriteReference(5, 3, FrameSpriteReference{2, 4, 4, 23, 23});
    for (std::size_t slot = 0; slot < 32; ++slot) {
        document.setFrameSpriteReference(
            6, slot,
            FrameSpriteReference{static_cast<uint8_t>(slot % 3), static_cast<uint16_t>(slot), 0,
                                 static_cast<uint16_t>(40 + slot), 9});
    }
    document.setFrameSpriteReference(7, 0, FrameSpriteReference{1, 5, 5, 25, 25});
    const ProjectSnapshot before = document.toSnapshot();
    const std::vector<RuntimeFrame> picturesBefore = renderAll(document);
    const std::size_t useCount = document.spriteUseCount(deleted);

    const auto command = colorizing::edit::deleteSprite(document, deleted);
    expect(command->recordKind() == RecordKind::Sprite && !command->addsRecords() &&
               !command->recordsPresent(),
           what + ": the command describes a deletion");
    expect(command->unassignedFrames().size() == useCount,
           what + ": the command names the frames that lost the sprite");
    const ProjectSnapshot after = document.toSnapshot();
    expect(after.sprites.size() == 2, what + ": sprite count");
    for (std::size_t s = 0; s < 2; ++s) {
        const std::size_t old = s < deleted ? s : s + 1;
        expect(sameSprite(after.sprites[s], before.sprites[old]),
               what + ": sprite " + std::to_string(s) + " is the old sprite " +
                   std::to_string(old));
    }
    for (std::size_t f = 0; f < after.frames.size(); ++f) {
        // The slot list without the deleted sprite, closed up, the other sprites renumbered.
        std::vector<FrameSpriteReference> expected;
        const std::vector<FrameSpriteReference>& old = before.frames[f].spriteReferences;
        for (FrameSpriteReference reference : old) {
            if (reference.spriteId == deleted) {
                continue;
            }
            if (reference.spriteId != kNoSpriteId && reference.spriteId > deleted) {
                --reference.spriteId;
            }
            expected.push_back(reference);
        }
        while (expected.size() < old.size()) {
            FrameSpriteReference empty = old.back();
            empty.spriteId = kNoSpriteId;
            expected.push_back(empty);
        }
        expect(sameReferences(after.frames[f].spriteReferences, expected),
               what + ": sprite slots of frame " + std::to_string(f));
        expect(sameFrameContent(after.frames[f], before.frames[f]),
               what + ": frame " + std::to_string(f) + " is otherwise unchanged");
    }
    expect(document.spriteUseCount(0) > 0, what + ": another sprite is still in use");

    const EditorMetadata& metadata = document.editorMetadata();
    const EditorMetadata& old = before.editorMetadata;
    for (std::size_t s = 0; s < 2; ++s) {
        const std::size_t from = s < deleted ? s : s + 1;
        expect(fixedName(metadata.spriteNames, s, kSpriteNameBytes) ==
                       "sprite " + std::to_string(from) &&
                   metadata.spriteSourceFrames[s] == old.spriteSourceFrames[from] &&
                   metadata.spriteSourceRects[s * 4 + 3] == old.spriteSourceRects[from * 4 + 3] &&
                   metadata.spriteSourceMirrors[s * 2] == old.spriteSourceMirrors[from * 2],
               what + ": cRP entries of sprite " + std::to_string(s));
    }
    expect(fixedName(metadata.spriteNames, 2, kSpriteNameBytes).empty() &&
               metadata.spriteSourceFrames[2] == 0 && metadata.spriteSourceRects[8] == 0 &&
               metadata.spriteSourceMirrors[5] == 0,
           what + ": the entry behind the last sprite is empty");
    expect(fixedName(metadata.spriteNames, 3, kSpriteNameBytes) == "sprite 3" &&
               metadata.spriteSourceFrames[4] == old.spriteSourceFrames[4],
           what + ": entries that never were a sprite stay");

    // Frames that did not show the deleted sprite render as before: the other sprites are
    // still found, under their new numbers.
    const std::vector<RuntimeFrame> picturesAfter = renderAll(document);
    for (std::size_t f : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        if (deleted != 2 || f != 3) {
            expect(samePicture(picturesAfter[f], picturesBefore[f]),
                   what + ": frame " + std::to_string(f) + " renders as before");
        }
    }
    if (deleted == 1) {
        // Frame 4 showed sprite 1 in its first slot and sprite 0 behind it.
        const RuntimeFrame& frame = picturesAfter[4];
        const auto* plane = frame.plane(fixtures::nativeRequest(document.geometry()));
        expect(plane != nullptr, what + ": frame 4 has a picture");
        expect(plane->image.pixels[static_cast<std::size_t>(3) * plane->image.size.width + 12] ==
                   fixtures::spriteColor(0, 0, 0),
               what + ": the sprite behind the deleted one is still drawn");
    }
    expectSavesAndRenders(document, what);

    command->revert(document);
    expectSame(document, before, what + ", undone");
    command->apply(document);
    expectSame(document, after, what + ", redone");
    command->revert(document);
    expectSame(document, before, what + ", undone again");
}

void testAddBackground(PlaneLayout layout)
{
    const std::string what = layoutName(layout) + ", add background";
    ProjectDocument document = makeDocument(layout);
    const ProjectSnapshot before = document.toSnapshot();
    if (layout == PlaneLayout::NativeExtra) {
        // Frame 0 has no 256x64 frame, which the background would need.
        expectThrows<std::logic_error>(
            [&]() { (void)colorizing::edit::addBackgroundFromFrame(document, 0); },
            what + " from a frame without its 256x64 data");
        expectSame(document, before, what + ", refused");
    }
    expectThrows<std::out_of_range>(
        [&]() { (void)colorizing::edit::addBackgroundFromFrame(document, 9); },
        what + " from a frame that does not exist");

    const auto command = colorizing::edit::addBackgroundFromFrame(document, 5);
    expect(command->recordKind() == RecordKind::Background && command->addsRecords() &&
               command->recordIndices() == std::vector<std::size_t>({3}),
           what + ": the command describes the new background");
    expect(document.backgroundCount() == 4, what + ": background count");
    const BackgroundRecord& background = document.background(3);
    expect(background.colorizedSd.pixels == before.frames[5].colorizedSd.pixels,
           what + ": the original plane is the frame's");
    if (layout == PlaneLayout::NativeExtra) {
        expect(background.colorizedHd &&
                   background.colorizedHd->pixels == before.frames[5].colorizedHd->pixels,
               what + ": the 256x64 image is the frame's");
    } else {
        expect(!background.colorizedHd, what + ": no extra-plane image");
    }
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        expect(sameWholeFrame(document.toSnapshot().frames[f], before.frames[f]),
               what + ": no frame is changed");
    }

    // Assigned to a frame, which then shows it; the frame is all black for that.
    document.setBackgroundId(4, 3);
    expectSavesAndRenders(document, what);
    const ProjectSnapshot used = document.toSnapshot();
    command->revert(document);
    expectSame(document, [&]() {
        // The frame that used the new background has none now.
        ProjectSnapshot expected = before;
        expected.frames[4].backgroundData->backgroundId = kNoBackgroundId;
        return expected;
    }(), what + ", undone");
    expectConsistentReferences(document, what + ", undone");
    command->apply(document);
    expectSame(document, used, what + ", redone");
}

void testBackgroundLimit()
{
    ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, 2);
    // Copies of one picture share its pixels.
    snapshot.backgrounds.assign(
        kMaxBackgrounds - 1,
        {colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize)});
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const auto last = colorizing::edit::addBackgroundFromFrame(document, 1);
    expect(document.backgroundCount() == kMaxBackgrounds, "as many backgrounds as allowed fit");
    expectThrows<std::length_error>(
        [&]() { (void)colorizing::edit::addBackgroundFromFrame(document, 0); },
        "adding one more background");
    last->revert(document);
    last->apply(document);
    expect(document.backgroundCount() == kMaxBackgrounds, "background 255 undone and redone");
}

void testDeleteBackground(PlaneLayout layout, std::size_t deleted)
{
    const std::string what =
        layoutName(layout) + ", delete background " + std::to_string(deleted);
    ProjectDocument document = makeDocument(layout);
    // Frame 1 uses background 2 and frame 4 background 0 already.
    const ProjectSnapshot before = document.toSnapshot();
    const std::vector<RuntimeFrame> picturesBefore = renderAll(document);
    const std::size_t useCount = document.backgroundUseCount(deleted);

    const auto command = colorizing::edit::deleteBackground(document, deleted);
    expect(command->unassignedFrames().size() == useCount,
           what + ": the command names the frames that lost the background");
    const ProjectSnapshot after = document.toSnapshot();
    expect(after.backgrounds.size() == 2, what + ": background count");
    for (std::size_t b = 0; b < 2; ++b) {
        expect(sameBackground(after.backgrounds[b], before.backgrounds[b < deleted ? b : b + 1]),
               what + ": background " + std::to_string(b));
    }
    const std::vector<RuntimeFrame> picturesAfter = renderAll(document);
    for (std::size_t f = 0; f < after.frames.size(); ++f) {
        expect(sameFrameContent(after.frames[f], before.frames[f]) &&
                   after.frames[f].comparisonMaskId == before.frames[f].comparisonMaskId &&
                   sameReferences(after.frames[f].spriteReferences,
                                  before.frames[f].spriteReferences),
               what + ": frame " + std::to_string(f) + " keeps everything but its background");
        if (!before.frames[f].backgroundData) {
            continue;
        }
        const uint16_t old = before.frames[f].backgroundData->backgroundId;
        const uint16_t expected = old == deleted
                                      ? kNoBackgroundId
                                      : old != kNoBackgroundId && old > deleted
                                            ? static_cast<uint16_t>(old - 1)
                                            : old;
        expect(after.frames[f].backgroundData->backgroundId == expected,
               what + ": background of frame " + std::to_string(f));
        if (old != deleted) {
            expect(samePicture(picturesAfter[f], picturesBefore[f]),
                   what + ": frame " + std::to_string(f) + " renders as before");
        }
    }
    expectSavesAndRenders(document, what);

    command->revert(document);
    expectSame(document, before, what + ", undone");
    command->apply(document);
    expectSame(document, after, what + ", redone");
    command->revert(document);
    expectSame(document, before, what + ", undone again");
}

void testAddComparisonMask(PlaneLayout layout)
{
    const std::string what = layoutName(layout) + ", add comparison mask";
    ProjectDocument document = makeDocument(layout);
    const ProjectSnapshot before = document.toSnapshot();
    const auto command = colorizing::edit::addComparisonMask(document);
    expect(command->recordKind() == RecordKind::ComparisonMask && command->addsRecords() &&
               command->recordIndices() == std::vector<std::size_t>({3}),
           what + ": the command describes the new mask");
    expect(document.comparisonMaskCount() == 4, what + ": mask count");
    const IndexedFrame& mask = document.comparisonMask(3);
    expect(mask.size == document.originalFrameSize() &&
               std::all_of(mask.pixels.begin(), mask.pixels.end(),
                           [](uint8_t value) { return value == 0; }),
           what + ": the mask has the original's size and masks nothing");
    expect(sameMetadata(document.editorMetadata(), before.editorMetadata),
           what + ": the mask names stay, the new mask has the name of its slot");
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        expect(document.comparisonMaskId(f) == before.frames[f].comparisonMaskId,
               what + ": no frame is assigned the new mask");
    }
    document.setComparisonMaskId(6, 3);
    document.setComparisonMaskPixel(3, 4, 4, 1);
    expectSavesAndRenders(document, what);
    const ProjectSnapshot used = document.toSnapshot();
    command->revert(document);
    expectSame(document, [&]() {
        ProjectSnapshot expected = before;
        expected.frames[6].comparisonMaskId = kNoComparisonMaskId;
        return expected;
    }(), what + ", undone");
    command->apply(document);
    expectSame(document, used, what + ", redone");
}

// Common To New Mask: a new mask with its content, assigned to frames that had other masks;
// one step, undone to the masks they had.
void testAddComparisonMaskForFrames(PlaneLayout layout)
{
    const std::string what = layoutName(layout) + ", add comparison mask for frames";
    ProjectDocument document = makeDocument(layout);
    const ProjectSnapshot before = document.toSnapshot();
    IndexedFrame content = colorizing::document::makeIndexedFrame(document.originalFrameSize(), 0);
    content.pixels[0] = 1;
    content.pixels[static_cast<std::size_t>(4) * content.size.width + 4] = 7;
    const std::vector<std::size_t> frames{1, 2, 5};
    const auto command = colorizing::edit::addComparisonMaskForFrames(document, content, frames);
    expect(command->recordKind() == RecordKind::ComparisonMask && command->addsRecords() &&
               command->recordIndices() == std::vector<std::size_t>({3}) &&
               document.comparisonMaskCount() == 4,
           what + ": the command adds mask 3");
    const IndexedFrame& mask = document.comparisonMask(3);
    const std::size_t at44 = static_cast<std::size_t>(4) * mask.size.width + 4;
    expect(mask.pixels[0] == 1 && mask.pixels[at44] == 1 &&
               std::count(mask.pixels.begin(), mask.pixels.end(), uint8_t{1}) == 2,
           what + ": the mask covers the pixels given");
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        const bool assigned = std::find(frames.begin(), frames.end(), f) != frames.end();
        expect(document.comparisonMaskId(f) ==
                   (assigned ? uint8_t{3} : before.frames[f].comparisonMaskId),
               what + ": the frames use it, the others keep theirs");
    }
    expectSavesAndRenders(document, what);
    const ProjectSnapshot after = document.toSnapshot();
    command->revert(document);
    expectSame(document, before, what + ", undone: the frames have their masks again");
    command->apply(document);
    expectSame(document, after, what + ", redone");
    command->revert(document);
    expectSame(document, before, what + ", undone again");

    const IndexedFrame wrong = colorizing::document::makeIndexedFrame({8, 8}, 1);
    expectThrows<std::invalid_argument>(
        [&]() { (void)colorizing::edit::addComparisonMaskForFrames(document, wrong, frames); },
        what + ": a mask of another size");
    expectSame(document, before, what + ": a refused mask changes nothing");
}

void testComparisonMaskLimit()
{
    ProjectDocument document =
        ProjectDocument::fromSnapshot(fixtures::makeProject(PlaneLayout::Ordinary, 2));
    std::vector<std::unique_ptr<StructureCommand>> commands;
    for (std::size_t m = 0; m < kMaxComparisonMasks; ++m) {
        commands.push_back(colorizing::edit::addComparisonMask(document));
    }
    expect(document.comparisonMaskCount() == kMaxComparisonMasks, "64 masks fit");
    expectThrows<std::length_error>([&]() { (void)colorizing::edit::addComparisonMask(document); },
                                    "adding mask 65");
    document.setComparisonMaskId(1, 63);
    expectSavesAndRenders(document, "64 comparison masks");
    for (auto command = commands.rbegin(); command != commands.rend(); ++command) {
        (*command)->revert(document);
    }
    expect(document.comparisonMaskCount() == 0 &&
               document.comparisonMaskId(1) == kNoComparisonMaskId,
           "all masks undone, no frame names one");
}

void testDeleteComparisonMask(PlaneLayout layout, std::size_t deleted)
{
    const std::string what =
        layoutName(layout) + ", delete comparison mask " + std::to_string(deleted);
    ProjectDocument document = makeDocument(layout);
    // Frame 1 uses mask 2 and frame 2 mask 1 already: mask 0 is not in use, mask 1 is used
    // once and mask 2 by three frames.
    document.setComparisonMaskId(7, 2);
    document.setComparisonMaskId(8, 2);
    const ProjectSnapshot before = document.toSnapshot();
    const std::size_t useCount = document.comparisonMaskUseCount(deleted);
    expect(useCount == (deleted == 0 ? 0u : deleted == 1 ? 1u : 3u), what + ": use count");
    const uint64_t identificationBefore = document.identificationRevision();

    const auto command = colorizing::edit::deleteComparisonMask(document, deleted);
    expect(command->unassignedFrames().size() == useCount,
           what + ": the command names the frames that lost the mask");
    expect(document.identificationRevision() != identificationBefore,
           what + ": frame identification is out of date");
    const ProjectSnapshot after = document.toSnapshot();
    expect(after.comparisonMasks.size() == 2, what + ": mask count");
    for (std::size_t m = 0; m < 2; ++m) {
        const std::size_t old = m < deleted ? m : m + 1;
        expect(after.comparisonMasks[m].pixels == before.comparisonMasks[old].pixels,
               what + ": mask " + std::to_string(m));
        expect(fixedName(after.editorMetadata.maskNames, m, kMaskNameBytes) ==
                   "mask " + std::to_string(old),
               what + ": name of mask " + std::to_string(m));
    }
    // The names of the unused slots move too, and the last slot is empty.
    expect(fixedName(after.editorMetadata.maskNames, 3, kMaskNameBytes) == "mask 4" &&
               fixedName(after.editorMetadata.maskNames, 62, kMaskNameBytes) == "last slot" &&
               fixedName(after.editorMetadata.maskNames, 63, kMaskNameBytes).empty(),
           what + ": the mask names behind it move down");
    for (std::size_t f = 0; f < after.frames.size(); ++f) {
        const uint8_t old = before.frames[f].comparisonMaskId;
        const uint8_t expected = old == deleted
                                     ? kNoComparisonMaskId
                                     : old != kNoComparisonMaskId && old > deleted
                                           ? static_cast<uint8_t>(old - 1)
                                           : old;
        expect(after.frames[f].comparisonMaskId == expected,
               what + ": mask of frame " + std::to_string(f));
    }
    expectSavesAndRenders(document, what);

    command->revert(document);
    expectSame(document, before, what + ", undone");
    command->apply(document);
    expectSame(document, after, what + ", redone");
    command->revert(document);
    expectSame(document, before, what + ", undone again");
}

std::unique_ptr<EditCommand> framePixel(std::size_t frame, uint16_t color)
{
    return std::make_unique<PixelEditCommand>(
        EditKind::FramePixels, frame, Resolution::Sd,
        std::vector<colorizing::edit::PixelChange>{{1, 1, 0, color}});
}

void pushApplied(UndoHistories& histories, ProjectDocument& document,
                 std::unique_ptr<EditCommand> command)
{
    command->apply(document);
    const HistoryKey key = historyKey(command->target());
    histories.push(key, std::move(command));
}

void pushStructure(UndoHistories& histories, std::unique_ptr<StructureCommand> command)
{
    histories.push(structureHistoryKey(), std::move(command));
}

void testFrameDeletionDropsFrameHistories()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    UndoHistories histories;
    const auto key = [](EditKind kind, std::size_t index) {
        return HistoryKey{kind, index, Resolution::Sd};
    };
    for (std::size_t frame : {std::size_t{1}, std::size_t{3}, std::size_t{4}, std::size_t{6}}) {
        pushApplied(histories, document, framePixel(frame, 0x1111));
    }
    pushApplied(histories, document,
                std::make_unique<FrameAttributeEditCommand>(6, FrameAttribute::DurationMs, 106,
                                                            500));
    pushApplied(histories, document,
                std::make_unique<FrameAttributeEditCommand>(2, FrameAttribute::DurationMs, 102,
                                                            500));
    // A redo stack counts as well.
    pushApplied(histories, document, framePixel(8, 0x2222));
    (void)histories.undo(key(EditKind::FramePixels, 8), document);
    pushApplied(histories, document,
                std::make_unique<MaskEditCommand>(
                    EditKind::ComparisonMask, 1, Resolution::Sd,
                    std::vector<colorizing::edit::MaskPixelChange>{{2, 2, 0, 1}}));
    const ProjectSnapshot before = document.toSnapshot();

    pushStructure(histories, colorizing::edit::deleteFrames(document, {3, 5}));
    expect(histories.canUndo(key(EditKind::FramePixels, 1)) &&
               histories.canUndo(key(EditKind::FrameAttributes, 2)),
           "histories of frames before the first deleted one stay");
    expect(!histories.canUndo(key(EditKind::FramePixels, 3)) &&
               !histories.canUndo(key(EditKind::FramePixels, 4)) &&
               !histories.canUndo(key(EditKind::FramePixels, 6)) &&
               !histories.canUndo(key(EditKind::FrameAttributes, 6)) &&
               !histories.canRedo(key(EditKind::FramePixels, 8)),
           "histories of deleted and renumbered frames are dropped");
    expect(histories.canUndo(key(EditKind::ComparisonMask, 1)),
           "a comparison mask history has nothing to do with frame numbers");
    expect(histories.canUndo(structureHistoryKey()), "the deletion can be undone");

    // An edit of what is frame 4 now (the old frame 6), then the deletion is undone: the
    // edit's history would name the old frame 4.
    pushApplied(histories, document, framePixel(4, 0x3333));
    pushApplied(histories, document, framePixel(0, 0x4444));
    expect(histories.undo(structureHistoryKey(), document) != nullptr, "the deletion is undone");
    expect(!histories.canUndo(key(EditKind::FramePixels, 4)),
           "a history made for a renumbered frame is dropped when the numbers change back");
    expect(histories.canUndo(key(EditKind::FramePixels, 0)) &&
               histories.canUndo(key(EditKind::FramePixels, 1)),
           "histories of frames before the deleted ones still stay");
    expect(!histories.canUndo(key(EditKind::FramePixels, 3)) &&
               !histories.canUndo(key(EditKind::FramePixels, 6)),
           "histories dropped by the deletion stay dropped after its undo");
    expect(document.colorizedPixel(6, Resolution::Sd, 1, 1) == 0x3333,
           "the edit made in between is on the frame it was made on");
    expect(document.frameCount() == before.frames.size(), "all frames are back");

    // The same holds for a redo.
    pushApplied(histories, document, framePixel(5, 0x5555));
    expect(histories.redo(structureHistoryKey(), document) != nullptr, "the deletion is redone");
    expect(!histories.canUndo(key(EditKind::FramePixels, 5)),
           "a redo drops the histories of the frames it removes or renumbers");
    expect(histories.undo(structureHistoryKey(), document) != nullptr, "and undone again");
    expect(document.colorizedPixel(5, Resolution::Sd, 1, 1) == 0x5555,
           "a frame deleted by the redo comes back as it was deleted, edit included");
}

void testRecordDeletionDropsHistoriesThatNameIt()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    UndoHistories histories;
    const auto key = [](EditKind kind, std::size_t index, Resolution resolution = Resolution::Sd) {
        return HistoryKey{kind, index, resolution};
    };
    const auto attribute = [](std::size_t frame, FrameAttribute which, uint32_t from,
                              uint32_t to) {
        return std::make_unique<FrameAttributeEditCommand>(frame, which, from, to);
    };
    const auto pixels = [](EditKind kind, std::size_t index) {
        return std::make_unique<PixelEditCommand>(
            kind, index, Resolution::Sd,
            std::vector<colorizing::edit::PixelChange>{{1, 1, 0, 0x0101}});
    };

    // Backgrounds: frame 4 uses 0, frame 1 uses 2.
    pushApplied(histories, document, pixels(EditKind::BackgroundImage, 0));
    pushApplied(histories, document, pixels(EditKind::BackgroundImage, 2));
    pushApplied(histories, document, attribute(4, FrameAttribute::BackgroundId, 0, 2));
    pushApplied(histories, document, attribute(1, FrameAttribute::BackgroundId, 2, 0));
    pushApplied(histories, document, attribute(0, FrameAttribute::DurationMs, 100, 33));
    pushStructure(histories, colorizing::edit::deleteBackground(document, 1));
    expect(histories.canUndo(key(EditKind::BackgroundImage, 0)),
           "the history of a background before the deleted one stays");
    expect(!histories.canUndo(key(EditKind::BackgroundImage, 2)),
           "the history of a renumbered background is dropped");
    expect(!histories.canUndo(key(EditKind::FrameAttributes, 4)) &&
               !histories.canUndo(key(EditKind::FrameAttributes, 1)),
           "a frame history that would write a renumbered background ID is dropped");
    expect(histories.canUndo(key(EditKind::FrameAttributes, 0)),
           "a frame history without background IDs stays");
    expect(document.backgroundId(4) == 1 && document.backgroundId(1) == 0,
           "the frames follow the renumbering");

    // The deletion of a background in use, a new assignment on the frame that lost it, then
    // the undo: the background is assigned again and the assignment's history is dropped.
    pushStructure(histories, colorizing::edit::deleteBackground(document, 0));
    expect(document.backgroundId(1) == kNoBackgroundId, "the frame lost its background");
    pushApplied(histories, document,
                attribute(1, FrameAttribute::BackgroundId, kNoBackgroundId, 0));
    expect(histories.undo(structureHistoryKey(), document) != nullptr, "the deletion is undone");
    expect(document.backgroundId(1) == 0 && document.backgroundId(4) == 1,
           "the assignments are as before the deletion");
    expect(!histories.canUndo(key(EditKind::FrameAttributes, 1)),
           "the assignment made in between cannot be undone against the old numbers");

    // Comparison masks: frame 1 uses 2, frame 2 uses 1.
    const auto maskPixels = [](std::size_t mask) {
        return std::make_unique<MaskEditCommand>(
            EditKind::ComparisonMask, mask, Resolution::Sd,
            std::vector<colorizing::edit::MaskPixelChange>{{3, 3, 0, 1}});
    };
    pushApplied(histories, document, maskPixels(0));
    pushApplied(histories, document, maskPixels(2));
    pushApplied(histories, document, attribute(5, FrameAttribute::ComparisonMaskId, 0, 2));
    pushApplied(histories, document, attribute(6, FrameAttribute::ShapeComparisonMode, 0, 1));
    pushApplied(histories, document, attribute(2, FrameAttribute::TriggerId, 7002, 5));
    pushStructure(histories, colorizing::edit::deleteComparisonMask(document, 1));
    expect(histories.canUndo(key(EditKind::ComparisonMask, 0)) &&
               !histories.canUndo(key(EditKind::ComparisonMask, 2)),
           "mask histories from the deleted mask on are dropped");
    expect(!histories.canUndo(key(EditKind::FrameAttributes, 5)),
           "a frame history that would write a renumbered mask ID is dropped");
    expect(histories.canUndo(key(EditKind::FrameAttributes, 6)),
           "a frame history without mask IDs stays");
    expect(!histories.canUndo(key(EditKind::FrameAttributes, 2)),
           "the settings history of a frame that lost its mask is dropped");
    expect(document.comparisonMaskId(2) == kNoComparisonMaskId &&
               document.comparisonMaskId(5) == 1 && document.comparisonMaskId(1) == 1,
           "the frames follow the mask renumbering");

    // Sprites.
    pushApplied(histories, document, pixels(EditKind::SpriteImage, 0));
    pushApplied(histories, document, pixels(EditKind::SpriteImage, 1));
    pushApplied(histories, document, pixels(EditKind::SpriteImage, 2));
    pushApplied(histories, document, colorizing::edit::createSpriteHd(document, 2));
    pushStructure(histories, colorizing::edit::deleteSprite(document, 1));
    expect(histories.canUndo(key(EditKind::SpriteImage, 0)) &&
               !histories.canUndo(key(EditKind::SpriteImage, 1)) &&
               !histories.canUndo(key(EditKind::SpriteImage, 2)) &&
               !histories.canUndo(key(EditKind::SpriteAttributes, 2)),
           "sprite histories from the deleted sprite on are dropped");
    expect(histories.canUndo(key(EditKind::ComparisonMask, 0)) &&
               histories.canUndo(key(EditKind::FrameAttributes, 6)),
           "a sprite deletion leaves mask and frame histories alone");
}

void testAddedRecordHistories()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    UndoHistories histories;
    const HistoryKey spriteKey{EditKind::SpriteImage, 3, Resolution::Sd};
    pushApplied(histories, document, framePixel(8, 0x0808));
    pushStructure(histories, colorizing::edit::addSprite(document, "new", 0));
    expect(histories.canUndo(HistoryKey{EditKind::FramePixels, 8, Resolution::Sd}),
           "adding a sprite drops nothing");
    pushApplied(histories, document,
                std::make_unique<PixelEditCommand>(
                    EditKind::SpriteImage, 3, Resolution::Sd,
                    std::vector<colorizing::edit::PixelChange>{{1, 1, 0, 0x7777}}));
    expect(histories.undo(structureHistoryKey(), document) != nullptr, "the addition is undone");
    expect(document.spriteCount() == 3, "the sprite is gone");
    expect(!histories.canUndo(spriteKey),
           "the history of a sprite that no longer exists is dropped");
    expect(histories.redo(structureHistoryKey(), document) != nullptr, "the addition is redone");
    expect(document.spriteColorizedPixel(3, Resolution::Sd, 1, 1) == 0x7777,
           "the sprite comes back as it was when the addition was undone");
    expect(!histories.canUndo(spriteKey), "its dropped history stays dropped");

    // Structure commands are undone in reverse order and restore the project as it was.
    ProjectDocument fresh = makeDocument(PlaneLayout::NativeExtra);
    UndoHistories structure;
    const ProjectSnapshot before = fresh.toSnapshot();
    pushStructure(structure, colorizing::edit::deleteSprite(fresh, 0));
    pushStructure(structure, colorizing::edit::deleteFrames(fresh, {0, 4, 8}));
    pushStructure(structure, colorizing::edit::addBackgroundFromFrame(fresh, 2));
    pushStructure(structure, colorizing::edit::deleteComparisonMask(fresh, 1));
    pushStructure(structure, colorizing::edit::addSprite(fresh, "another", 1));
    pushStructure(structure, colorizing::edit::deleteBackground(fresh, 0));
    pushStructure(structure, colorizing::edit::addComparisonMask(fresh));
    pushStructure(structure, colorizing::edit::deleteFrames(fresh, {1}));
    const ProjectSnapshot after = fresh.toSnapshot();
    expectSavesAndRenders(fresh, "256x64, after eight structure commands");
    int undone = 0;
    while (structure.undo(structureHistoryKey(), fresh) != nullptr) {
        ++undone;
    }
    expect(undone == 8, "eight structure commands are undone");
    expectSame(fresh, before, "256x64, all structure commands undone");
    while (structure.redo(structureHistoryKey(), fresh) != nullptr) {
    }
    expectSame(fresh, after, "256x64, all structure commands redone");
}

// Random edits, structure commands, undos and redos. Every record carries a mark, and each
// command remembers the marks of the records it names: whenever the histories run a
// command, in either direction, those records must still carry these marks.
void testNoStaleCommandRunsAfterRenumbering()
{
    ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, 12);
    snapshot.comparisonMasks.assign(4, colorizing::document::makeIndexedFrame(
                                           colorizing::render::kSdFrameSize, 0));
    for (std::size_t i = 0; i < 4; ++i) {
        fixtures::addBackground(snapshot);
        snapshot.sprites.push_back(fixtures::emptySprite());
    }
    for (std::size_t f = 0; f < snapshot.frames.size(); ++f) {
        fixtures::assignBackground(snapshot, f, f % 4);
        snapshot.frames[f].comparisonMaskId = static_cast<uint8_t>(f % 4);
    }
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);

    uint32_t nextMark = 1;
    const auto mark = [&nextMark]() { return nextMark++; };
    // The marks live where the test's edits do not write.
    const auto frameMark = [&](std::size_t f) { return document.triggerId(f); };
    const auto backgroundMark = [&](std::size_t b) {
        return static_cast<uint32_t>(document.backgroundPixel(b, Resolution::Sd, 0, 0));
    };
    const auto maskMark = [&](std::size_t m) {
        return static_cast<uint32_t>(document.comparisonMaskPixel(m, 0, 0));
    };
    const auto spriteMark = [&](std::size_t s) {
        return static_cast<uint32_t>(std::stoul(document.sprite(s).name));
    };
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        document.setTriggerId(f, mark());
    }
    for (std::size_t i = 0; i < 4; ++i) {
        document.setBackgroundPixel(i, Resolution::Sd, 0, 0, static_cast<uint16_t>(mark()));
        document.setComparisonMaskPixel(i, 0, 0, static_cast<uint8_t>(mark()));
        document.setSpriteName(i, std::to_string(mark()));
    }
    const auto markOf = [&](RecordKind kind, std::size_t index) -> std::optional<uint32_t> {
        switch (kind) {
            case RecordKind::Frame:
                return index < document.frameCount() ? std::optional(frameMark(index))
                                                     : std::nullopt;
            case RecordKind::Background:
                return index < document.backgroundCount() ? std::optional(backgroundMark(index))
                                                          : std::nullopt;
            case RecordKind::ComparisonMask:
                return index < document.comparisonMaskCount() ? std::optional(maskMark(index))
                                                              : std::nullopt;
            case RecordKind::Sprite:
                return index < document.spriteCount() ? std::optional(spriteMark(index))
                                                      : std::nullopt;
        }
        return std::nullopt;
    };

    UndoHistories histories;
    std::map<const EditCommand*, std::vector<std::optional<uint32_t>>> marksOfCommand;
    std::set<HistoryKey> keys;
    const auto record = [&](std::unique_ptr<EditCommand> command) {
        std::vector<std::optional<uint32_t>> marks;
        for (const auto& reference : command->recordReferences()) {
            marks.push_back(markOf(reference.kind, reference.index));
            expect(marks.back().has_value(), "a new command names records that exist");
        }
        const EditCommand* pointer = command.get();
        const HistoryKey key = historyKey(command->target());
        command->apply(document);
        histories.push(key, std::move(command));
        marksOfCommand[pointer] = std::move(marks);
        keys.insert(key);
    };
    // For a command the histories just ran again. Its edit leaves the marks alone.
    std::size_t checked = 0;
    const auto checkRan = [&](const EditCommand* command) {
        if (!command || command->target().kind == EditKind::ProjectStructure) {
            return;
        }
        const auto references = command->recordReferences();
        const auto& marks = marksOfCommand.at(command);
        for (std::size_t i = 0; i < references.size(); ++i) {
            expect(markOf(references[i].kind, references[i].index) == marks[i],
                   "a command ran against the record it was made for (step " +
                       std::to_string(checked) + ")");
        }
        ++checked;
    };

    uint32_t state = 12345;
    const auto random = [&state](std::size_t bound) {
        state = state * 1664525u + 1013904223u;
        return static_cast<std::size_t>((state >> 8) % bound);
    };
    std::size_t structureCommands = 0;
    for (int step = 0; step < 20000; ++step) {
        const std::size_t action = random(100);
        if (action < 30) {
            const std::size_t f = random(document.frameCount());
            record(std::make_unique<PixelEditCommand>(
                EditKind::FramePixels, f, Resolution::Sd,
                std::vector<colorizing::edit::PixelChange>{
                    {2, 2, document.colorizedPixel(f, Resolution::Sd, 2, 2),
                     static_cast<uint16_t>(step)}}));
        } else if (action < 40 && document.backgroundCount() > 0) {
            const std::size_t b = random(document.backgroundCount());
            record(std::make_unique<PixelEditCommand>(
                EditKind::BackgroundImage, b, Resolution::Sd,
                std::vector<colorizing::edit::PixelChange>{
                    {3, 3, document.backgroundPixel(b, Resolution::Sd, 3, 3),
                     static_cast<uint16_t>(step)}}));
        } else if (action < 48 && document.comparisonMaskCount() > 0) {
            const std::size_t m = random(document.comparisonMaskCount());
            record(std::make_unique<MaskEditCommand>(
                EditKind::ComparisonMask, m, Resolution::Sd,
                std::vector<colorizing::edit::MaskPixelChange>{
                    {4, 4, document.comparisonMaskPixel(m, 4, 4),
                     static_cast<uint8_t>(step % 2)}}));
        } else if (action < 56 && document.spriteCount() > 0) {
            const std::size_t s = random(document.spriteCount());
            record(std::make_unique<PixelEditCommand>(
                EditKind::SpriteImage, s, Resolution::Sd,
                std::vector<colorizing::edit::PixelChange>{
                    {5, 5, document.spriteColorizedPixel(s, Resolution::Sd, 5, 5),
                     static_cast<uint16_t>(step)}}));
        } else if (action < 64 && document.backgroundCount() > 0) {
            const std::size_t f = random(document.frameCount());
            const uint32_t to = static_cast<uint32_t>(random(document.backgroundCount()));
            if (document.backgroundId(f) != to) {
                record(std::make_unique<FrameAttributeEditCommand>(
                    f, FrameAttribute::BackgroundId, document.backgroundId(f), to));
            }
        } else if (action < 72 && document.comparisonMaskCount() > 0) {
            const std::size_t f = random(document.frameCount());
            const uint32_t to = static_cast<uint32_t>(random(document.comparisonMaskCount()));
            if (document.comparisonMaskId(f) != to) {
                record(std::make_unique<FrameAttributeEditCommand>(
                    f, FrameAttribute::ComparisonMaskId, document.comparisonMaskId(f), to));
            }
        } else if (action < 75) {
            std::unique_ptr<StructureCommand> command;
            const std::size_t which = random(8);
            if (which == 0 && document.frameCount() > 3) {
                command = colorizing::edit::deleteFrames(
                    document, {random(document.frameCount()), random(document.frameCount())});
            } else if (which == 1 && document.spriteCount() > 0) {
                command = colorizing::edit::deleteSprite(document, random(document.spriteCount()));
            } else if (which == 2 && document.backgroundCount() > 0) {
                command = colorizing::edit::deleteBackground(document,
                                                             random(document.backgroundCount()));
            } else if (which == 3 && document.comparisonMaskCount() > 0) {
                command = colorizing::edit::deleteComparisonMask(
                    document, random(document.comparisonMaskCount()));
            } else if (which == 4 && document.spriteCount() < 8) {
                command = colorizing::edit::addSprite(document, std::to_string(mark()), 0);
            } else if (which == 5 && document.backgroundCount() < 8) {
                command = colorizing::edit::addBackgroundFromFrame(document, 0);
                document.setBackgroundPixel(command->recordIndices().front(), Resolution::Sd, 0,
                                            0, static_cast<uint16_t>(mark()));
            } else if (which == 6 && document.comparisonMaskCount() < 8) {
                command = colorizing::edit::addComparisonMask(document);
                document.setComparisonMaskPixel(command->recordIndices().front(), 0, 0,
                                                static_cast<uint8_t>(mark()));
            }
            if (command) {
                histories.push(structureHistoryKey(), std::move(command));
                keys.insert(structureHistoryKey());
                ++structureCommands;
            }
        } else if (!keys.empty()) {
            auto key = keys.begin();
            std::advance(key, static_cast<std::ptrdiff_t>(random(keys.size())));
            // Structure commands are undone more often than they would be by chance.
            const HistoryKey chosen = random(10) == 0 ? structureHistoryKey() : *key;
            checkRan(action < 92 ? histories.undo(chosen, document)
                                 : histories.redo(chosen, document));
        }
        if (step % 1000 == 0) {
            expectConsistentReferences(document, "random edits, step " + std::to_string(step));
        }
    }
    expectConsistentReferences(document, "random edits, at the end");
    expect(structureCommands > 100 && checked > 500,
           "the random run had structure commands and ran other commands again: " +
               std::to_string(structureCommands) + " and " + std::to_string(checked));
}

// What a deletion keeps for its undo: the records themselves, moved, not copied.
void testDeletedFramesAreMovedNotCopied()
{
    ProjectDocument document = makeDocument(PlaneLayout::NativeOriginal);
    const uint16_t* pixelsBefore =
        document.colorizedFrame(4, Resolution::Sd).pixels.data();
    const uint16_t* stayingBefore =
        document.colorizedFrame(6, Resolution::Sd).pixels.data();
    colorizing::document::RemovedFrames removed = document.removeFrames({4});
    expect(removed.frames.size() == 1 &&
               removed.frames[0].colorizedSd.pixels.data() == pixelsBefore,
           "a removed frame is handed over with its pixel buffer");
    expect(document.colorizedFrame(5, Resolution::Sd).pixels.data() == stayingBefore,
           "the frames behind it move down with their pixel buffers");
    document.insertFrames(std::move(removed));
    expect(document.colorizedFrame(4, Resolution::Sd).pixels.data() == pixelsBefore &&
               document.colorizedFrame(6, Resolution::Sd).pixels.data() == stayingBefore,
           "and back");
}

// insertFrames() checks every block of every frame against the document before it changes
// anything.
void testInsertFramesRejectsFramesThatDoNotFit(PlaneLayout layout)
{
    const std::string what = layoutName(layout) + ", inserting a frame that does not fit";
    ProjectDocument document = makeDocument(layout);
    // Frame 4 has a background and sprites; it gets frame 2's dynamic data too, so that every
    // kind of block is there.
    const std::size_t source = 4;
    expect(document.backgroundData(source) && !document.frameSpriteReferences(source).empty() &&
               document.dynamicData(2),
           what + ": the fixture has the blocks");
    const auto dynamic = document.dynamicData(2);
    colorizing::document::RemovedFrames template_ = document.removeFrames({source});
    template_.frames[0].dynamicData = dynamic;
    const ProjectSnapshot before = document.toSnapshot();
    const bool extra = !document.geometry().discardsExtraPlane();
    const Size sd = document.geometry().planeSize(Resolution::Sd);
    const Size hd = document.geometry().planeSize(Resolution::Hd);
    const Size wrong{sd.width, sd.height / 2};

    using Frame = colorizing::document::FrameRecord;
    std::vector<std::pair<std::string, std::function<void(Frame&)>>> misfits{
        {"original", [&](Frame& f) { f.originalSd = makeIndexedFrame(wrong); }},
        {"original pixel count", [](Frame& f) { f.originalSd.pixels.pop_back(); }},
        {"colorized frame", [&](Frame& f) { f.colorizedSd = makeRgb565Frame(wrong); }},
        {"color rotations", [](Frame& f) { f.colorRotationsSd.resize(3); }},
        {"dynamic mask", [&](Frame& f) { f.dynamicData->maskSd = makeIndexedFrame(wrong); }},
        {"dynamic colors", [](Frame& f) { f.dynamicData->colorsSd.pop_back(); }},
        {"dynamic shadow directions",
         [](Frame& f) { f.dynamicData->shadowDirectionsSd.assign(5, 0); }},
        {"background mask",
         [&](Frame& f) { f.backgroundData->maskSd = makeIndexedFrame(wrong); }},
        {"sprite slots", [](Frame& f) { f.spriteReferences.resize(3); }},
    };
    if (extra) {
        misfits.push_back({"extra-plane frame",
                           [&](Frame& f) { f.colorizedHd = makeRgb565Frame(sd); }});
        misfits.push_back({"inactive extra-plane",
                           [&](Frame& f) {
                               f.colorizedHd = makeRgb565Frame(hd);
                               f.inactiveHd = makeRgb565Frame(hd);
                           }});
        misfits.push_back({"extra-plane color rotations",
                           [](Frame& f) { f.colorRotationsHd.clear(); }});
        misfits.push_back({"extra-plane dynamic mask",
                           [&](Frame& f) { f.dynamicData->maskHd = makeIndexedFrame(sd); }});
        misfits.push_back({"extra-plane dynamic colors",
                           [](Frame& f) { f.dynamicData->colorsHd.clear(); }});
        misfits.push_back({"extra-plane background mask",
                           [&](Frame& f) { f.backgroundData->maskHd = makeIndexedFrame(sd); }});
    } else {
        misfits.push_back({"extra-plane frame",
                           [&](Frame& f) { f.colorizedHd = makeRgb565Frame(hd); }});
        misfits.push_back({"extra-plane color rotations",
                           [](Frame& f) { f.colorRotationsHd.assign(256, 0); }});
        misfits.push_back({"extra-plane dynamic data",
                           [&](Frame& f) { f.dynamicData->maskHd = makeIndexedFrame(hd); }});
        misfits.push_back({"extra-plane background mask",
                           [&](Frame& f) { f.backgroundData->maskHd = makeIndexedFrame(hd); }});
    }
    for (const auto& [block, damage] : misfits) {
        colorizing::document::RemovedFrames removed = template_;
        damage(removed.frames[0]);
        bool threw = false;
        try {
            document.insertFrames(std::move(removed));
        } catch (const std::invalid_argument& error) {
            threw = std::string(error.what()).find("frame " + std::to_string(source) +
                                                   " to insert: ") == 0;
        }
        expect(threw, what + ": " + block + " is refused with a message that names the frame");
        expectSame(document, before, what + ": " + block + " changes nothing");
    }
    document.insertFrames(std::move(template_));
    expect(document.frameCount() == before.frames.size() + 1, what + ": a fitting frame goes in");
}

}  // namespace

void runStructureCommandTests()
{
    for (PlaneLayout layout : kLayouts) {
        testDeleteFrames(layout, {0});
        testDeleteFrames(layout, {8});
        testDeleteFrames(layout, {3});
        testDeleteFrames(layout, {2, 5, 6});
        testDeleteFrames(layout, {0, 1, 2, 3, 4, 5, 6, 7});
        testDeleteFrames(layout, {1, 2, 3, 4, 5, 6, 7, 8});
        testDeleteFrames(layout, {0, 2, 4, 6, 8});
        testInsertFramesRejectsFramesThatDoNotFit(layout);
        testAddSprite(layout);
        for (std::size_t sprite = 0; sprite < 3; ++sprite) {
            testDeleteSprite(layout, sprite);
        }
        testAddBackground(layout);
        for (std::size_t background = 0; background < 3; ++background) {
            testDeleteBackground(layout, background);
        }
        testAddComparisonMask(layout);
        testAddComparisonMaskForFrames(layout);
        for (std::size_t mask = 0; mask < 3; ++mask) {
            testDeleteComparisonMask(layout, mask);
        }
    }
    testDeleteFramesRejects();
    testFrameDeletionSections();
    testFramesShorterThan();
    testSpriteLimit();
    testBackgroundLimit();
    testComparisonMaskLimit();
    testFrameDeletionDropsFrameHistories();
    testRecordDeletionDropsHistoriesThatNameIt();
    testAddedRecordHistories();
    testNoStaleCommandRunsAfterRenumbering();
    testDeletedFramesAreMovedNotCopied();
}
