#include "edit/EditCommand.h"
#include "edit/UndoHistories.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "render/CanvasLayout.h"

using colorizing::document::BackgroundRecord;
using colorizing::document::FrameBackgroundData;
using colorizing::document::FrameDynamicData;
using colorizing::document::PaletteRecord;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::SpriteDetectionArea;
using colorizing::document::SpriteRecord;
using colorizing::document::kMaxDynamicSetsPerFrame;
using colorizing::document::kPaletteColorCount;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::edit::DynamicColorEditCommand;
using colorizing::edit::DynamicShadowEditCommand;
using colorizing::edit::EditCommand;
using colorizing::edit::EditKind;
using colorizing::edit::EditTarget;
using colorizing::edit::HistoryKey;
using colorizing::edit::MaskEditCommand;
using colorizing::edit::PaletteEditCommand;
using colorizing::edit::PixelEditCommand;
using colorizing::edit::UndoHistories;
using colorizing::edit::historyKey;
using colorizing::render::Resolution;
using colorizing::render::kHdFrameSize;
using colorizing::render::kSdFrameSize;

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

// Two HD frames with dynamic and background data, one comparison mask, background, sprite and
// palette, so every command kind has something to edit.
ProjectDocument makeDocument()
{
    const FrameDynamicData dynamicData{
        makeIndexedFrame(kSdFrameSize, 255),
        makeIndexedFrame(kHdFrameSize, 255),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0),
    };
    ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "edit_commands";
    for (int frame = 0; frame < 2; ++frame) {
        snapshot.frames.push_back(ProjectFrameSnapshot{
            makeIndexedFrame(kSdFrameSize, 0),
            makeRgb565Frame(kSdFrameSize, 0),
            makeRgb565Frame(kHdFrameSize, 0),
            0,
            0,
            dynamicData,
            FrameBackgroundData{
                0,
                makeIndexedFrame(kSdFrameSize, 0),
                makeIndexedFrame(kHdFrameSize, 0),
            },
        });
    }
    snapshot.comparisonMasks.push_back(makeIndexedFrame(kSdFrameSize, 0));
    snapshot.backgrounds.push_back(BackgroundRecord{
        makeRgb565Frame(kSdFrameSize, 0x5050),
        makeRgb565Frame(kHdFrameSize, 0x6060),
    });
    snapshot.sprites.push_back(SpriteRecord{
        true,
        makeIndexedFrame({256, 64}, 255),
        makeRgb565Frame({256, 64}, 0),
        makeIndexedFrame({256, 64}, 255),
        makeRgb565Frame({256, 64}, 0),
        {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
        {0xffff, 0xffff, 0xffff, 0xffff},
        {{SpriteDetectionArea{}, SpriteDetectionArea{}, SpriteDetectionArea{},
          SpriteDetectionArea{}}},
    });
    PaletteRecord palette;
    palette.colors.assign(kPaletteColorCount, 0);
    palette.colors[1] = 0x1111;
    snapshot.palettes.push_back(palette);

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

void expectTarget(const EditCommand& command,
                  EditKind kind,
                  std::size_t index,
                  Resolution resolution,
                  const std::vector<std::size_t>& frameIndices,
                  const std::string& name)
{
    const EditTarget target = command.target();
    expect(target.kind == kind, name + " target kind");
    expect(target.index == index, name + " target index");
    expect(target.resolution == resolution, name + " target resolution");
    expect(target.frameIndices == frameIndices, name + " target frames");
}

void testFramePixelCommand()
{
    ProjectDocument document = makeDocument();
    document.setColorizedPixel(1, Resolution::Hd, 3, 2, 0x0101);
    document.markClean();

    const PixelEditCommand command(EditKind::FramePixels,
                                   1,
                                   Resolution::Hd,
                                   {{3, 2, 0x0101, 0x1234}, {200, 60, 0, 0x4321}});
    expectTarget(command, EditKind::FramePixels, 1, Resolution::Hd, {1}, "frame pixel");

    command.apply(document);
    expect(document.colorizedPixel(1, Resolution::Hd, 3, 2) == 0x1234, "frame pixel apply");
    expect(document.colorizedPixel(1, Resolution::Hd, 200, 60) == 0x4321,
           "frame pixel apply second change");
    expect(document.colorizedPixel(0, Resolution::Hd, 3, 2) == 0, "other frame untouched");
    expect(document.colorizedPixel(1, Resolution::Sd, 3, 2) == 0, "other resolution untouched");
    expect(document.isDirty(), "frame pixel apply marks dirty");

    command.revert(document);
    expect(document.colorizedPixel(1, Resolution::Hd, 3, 2) == 0x0101, "frame pixel revert");
    expect(document.colorizedPixel(1, Resolution::Hd, 200, 60) == 0,
           "frame pixel revert second change");
}

void testBackgroundImageCommand()
{
    ProjectDocument document = makeDocument();
    const PixelEditCommand command(EditKind::BackgroundImage,
                                   0,
                                   Resolution::Sd,
                                   {{5, 6, 0x5050, 0x0abc}});
    expectTarget(command, EditKind::BackgroundImage, 0, Resolution::Sd, {}, "background image");

    command.apply(document);
    expect(document.backgroundPixel(0, Resolution::Sd, 5, 6) == 0x0abc, "background apply");
    expect(document.backgroundPixel(0, Resolution::Hd, 5, 6) == 0x6060,
           "background other resolution untouched");
    command.revert(document);
    expect(document.backgroundPixel(0, Resolution::Sd, 5, 6) == 0x5050, "background revert");
}

void testSpriteImageCommand()
{
    ProjectDocument document = makeDocument();
    const PixelEditCommand command(EditKind::SpriteImage,
                                   0,
                                   Resolution::Hd,
                                   {{8, 4, 0, 0x2222}});
    expectTarget(command, EditKind::SpriteImage, 0, Resolution::Hd, {}, "sprite image");

    command.apply(document);
    expect(document.spriteColorizedPixel(0, Resolution::Hd, 8, 4) == 0x2222, "sprite apply");
    expect(document.spriteColorizedPixel(0, Resolution::Sd, 8, 4) == 0,
           "sprite other resolution untouched");
    command.revert(document);
    expect(document.spriteColorizedPixel(0, Resolution::Hd, 8, 4) == 0, "sprite revert");
}

void testComparisonMaskCommand()
{
    ProjectDocument document = makeDocument();
    const MaskEditCommand command(EditKind::ComparisonMask, 0, Resolution::Hd, {{4, 2, 0, 1}});
    expectTarget(command, EditKind::ComparisonMask, 0, Resolution::Sd, {}, "comparison mask");

    command.apply(document);
    expect(document.comparisonMaskPixel(0, 4, 2) == 1, "comparison mask apply");
    command.revert(document);
    expect(document.comparisonMaskPixel(0, 4, 2) == 0, "comparison mask revert");
}

void testDynamicMaskCommand()
{
    ProjectDocument document = makeDocument();
    const MaskEditCommand command(EditKind::DynamicMask, 1, Resolution::Sd, {{4, 2, 255, 3}});
    expectTarget(command, EditKind::DynamicMask, 1, Resolution::Sd, {1}, "dynamic mask");

    command.apply(document);
    expect(document.dynamicMaskPixel(1, Resolution::Sd, 4, 2) == 3, "dynamic mask apply");
    expect(document.dynamicMaskPixel(0, Resolution::Sd, 4, 2) == 255,
           "dynamic mask other frame untouched");
    expect(document.dynamicMaskPixel(1, Resolution::Hd, 4, 2) == 255,
           "dynamic mask other resolution untouched");
    command.revert(document);
    expect(document.dynamicMaskPixel(1, Resolution::Sd, 4, 2) == 255, "dynamic mask revert");
}

void testBackgroundMaskCommand()
{
    ProjectDocument document = makeDocument();
    const MaskEditCommand command(EditKind::BackgroundMask, 0, Resolution::Hd, {{9, 7, 0, 1}});
    expectTarget(command, EditKind::BackgroundMask, 0, Resolution::Hd, {0}, "background mask");

    command.apply(document);
    expect(document.backgroundMaskPixel(0, Resolution::Hd, 9, 7) == 1, "background mask apply");
    expect(document.backgroundMaskPixel(1, Resolution::Hd, 9, 7) == 0,
           "background mask other frame untouched");
    command.revert(document);
    expect(document.backgroundMaskPixel(0, Resolution::Hd, 9, 7) == 0, "background mask revert");
}

void testDynamicColorCommand()
{
    ProjectDocument document = makeDocument();
    const DynamicColorEditCommand command(1, Resolution::Sd, 2, 3, 0, 0x7777);
    expectTarget(command, EditKind::DynamicColor, 1, Resolution::Sd, {1}, "dynamic color");

    command.apply(document);
    expect(document.dynamicColor(1, Resolution::Sd, 2, 3) == 0x7777, "dynamic color apply");
    expect(document.dynamicColor(1, Resolution::Hd, 2, 3) == 0,
           "dynamic color other resolution untouched");
    command.revert(document);
    expect(document.dynamicColor(1, Resolution::Sd, 2, 3) == 0, "dynamic color revert");
}

void testDynamicShadowCommand()
{
    ProjectDocument document = makeDocument();
    document.setDynamicShadowDirection(1, Resolution::Sd, 2, 0x01);
    document.setDynamicShadowColor(0, Resolution::Sd, 2, 0x0f0f);

    const DynamicShadowEditCommand direction(0,
                                             Resolution::Sd,
                                             2,
                                             true,
                                             false,
                                             {{0, 0x00, 0x08, 0x0f0f, 0xffff},
                                              {1, 0x01, 0x09, 0, 0xffff}});
    expectTarget(direction, EditKind::DynamicShadow, 0, Resolution::Sd, {0, 1}, "shadow");

    direction.apply(document);
    expect(document.dynamicShadowDirection(0, Resolution::Sd, 2) == 0x08,
           "shadow direction apply on first frame");
    expect(document.dynamicShadowDirection(1, Resolution::Sd, 2) == 0x09,
           "shadow direction apply on second frame");
    expect(document.dynamicShadowColor(0, Resolution::Sd, 2) == 0x0f0f,
           "direction-only command leaves color alone");
    expect(document.dynamicShadowDirection(0, Resolution::Hd, 2) == 0,
           "shadow other resolution untouched");
    direction.revert(document);
    expect(document.dynamicShadowDirection(0, Resolution::Sd, 2) == 0x00,
           "shadow direction revert on first frame");
    expect(document.dynamicShadowDirection(1, Resolution::Sd, 2) == 0x01,
           "shadow direction revert on second frame");

    // The owner frame need not be among the changed frames.
    const DynamicShadowEditCommand color(0,
                                         Resolution::Sd,
                                         2,
                                         false,
                                         true,
                                         {{1, 0x7f, 0x7f, 0, 0x2222}});
    expectTarget(color, EditKind::DynamicShadow, 0, Resolution::Sd, {1}, "shadow color");
    color.apply(document);
    expect(document.dynamicShadowColor(1, Resolution::Sd, 2) == 0x2222, "shadow color apply");
    expect(document.dynamicShadowDirection(1, Resolution::Sd, 2) == 0x01,
           "color-only command leaves direction alone");
    color.revert(document);
    expect(document.dynamicShadowColor(1, Resolution::Sd, 2) == 0, "shadow color revert");
}

void testPaletteCommand()
{
    ProjectDocument document = makeDocument();
    const PaletteEditCommand command(0, {{1, 0x1111, 0x2222}, {5, 0, 0x3333}});
    expectTarget(command, EditKind::Palette, 0, Resolution::Sd, {}, "palette");

    command.apply(document);
    expect(document.paletteColor(0, 1) == 0x2222, "palette apply");
    expect(document.paletteColor(0, 5) == 0x3333, "palette apply second change");
    command.revert(document);
    expect(document.paletteColor(0, 1) == 0x1111, "palette revert");
    expect(document.paletteColor(0, 5) == 0, "palette revert second change");
}

void testCommandKindValidation()
{
    expectThrows<std::invalid_argument>(
        []() { PixelEditCommand(EditKind::DynamicMask, 0, Resolution::Sd, {}); },
        "pixel command with mask kind");
    expectThrows<std::invalid_argument>(
        []() { MaskEditCommand(EditKind::FramePixels, 0, Resolution::Sd, {}); },
        "mask command with pixel kind");

    ProjectDocument document = makeDocument();
    const PixelEditCommand outOfRange(EditKind::FramePixels, 7, Resolution::Sd, {{0, 0, 0, 1}});
    expectThrows<std::out_of_range>([&]() { outOfRange.apply(document); },
                                    "command on missing frame");
}

std::unique_ptr<EditCommand> makeFramePixel(std::size_t frameIndex,
                                            Resolution resolution,
                                            int x,
                                            uint16_t oldColor,
                                            uint16_t newColor)
{
    return std::make_unique<PixelEditCommand>(
        EditKind::FramePixels,
        frameIndex,
        resolution,
        std::vector<colorizing::edit::PixelChange>{{x, 0, oldColor, newColor}});
}

// Applies the command and records it, the way the editor commits an edit.
HistoryKey commit(UndoHistories& histories,
                  ProjectDocument& document,
                  std::unique_ptr<EditCommand> command)
{
    command->apply(document);
    const HistoryKey key = historyKey(command->target());
    histories.push(key, std::move(command));
    return key;
}

void testHistoryKeys()
{
    const HistoryKey frameSd{EditKind::FramePixels, 3, Resolution::Sd};
    const HistoryKey frameHd{EditKind::FramePixels, 3, Resolution::Hd};
    const HistoryKey otherFrame{EditKind::FramePixels, 4, Resolution::Sd};
    const HistoryKey dynamicMask{EditKind::DynamicMask, 3, Resolution::Sd};
    expect(frameSd == HistoryKey{EditKind::FramePixels, 3, Resolution::Sd}, "equal keys");
    expect(!(frameSd == frameHd) && (frameSd < frameHd) != (frameHd < frameSd),
           "resolution separates keys");
    expect(!(frameSd == otherFrame) && (frameSd < otherFrame) != (otherFrame < frameSd),
           "index separates keys");
    expect(!(frameSd == dynamicMask) && (frameSd < dynamicMask) != (dynamicMask < frameSd),
           "kind separates keys");
    expect(!(frameSd < frameSd), "key ordering is irreflexive");

    const DynamicShadowEditCommand shadow(5, Resolution::Hd, 0, true, false, {{9, 0, 1, 0, 0}});
    expect(historyKey(shadow.target()) == HistoryKey{EditKind::DynamicShadow, 5, Resolution::Hd},
           "shadow history belongs to the owner frame");
}

void testHistoryUndoRedoRoundTrip()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey key{EditKind::FramePixels, 0, Resolution::Sd};

    expect(!histories.canUndo(key) && !histories.canRedo(key), "empty history");
    expect(histories.undo(key, document) == nullptr, "undo on empty history");
    expect(histories.redo(key, document) == nullptr, "redo on empty history");

    expect(commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 0x0011)) == key,
           "frame pixel command key");
    commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0x0011, 0x0022));
    expect(histories.canUndo(key) && !histories.canRedo(key), "history after two edits");

    const EditCommand* undone = histories.undo(key, document);
    expect(undone != nullptr && undone->target().kind == EditKind::FramePixels,
           "undo returns the reverted command");
    expect(document.colorizedPixel(0, Resolution::Sd, 1, 0) == 0x0011, "first undo");
    expect(histories.canUndo(key) && histories.canRedo(key), "history after first undo");

    histories.undo(key, document);
    expect(document.colorizedPixel(0, Resolution::Sd, 1, 0) == 0, "second undo");
    expect(!histories.canUndo(key) && histories.canRedo(key), "history fully undone");
    expect(histories.undo(key, document) == nullptr, "undo past the start");

    expect(histories.redo(key, document) != nullptr, "redo returns the applied command");
    expect(document.colorizedPixel(0, Resolution::Sd, 1, 0) == 0x0011, "first redo");
    histories.redo(key, document);
    expect(document.colorizedPixel(0, Resolution::Sd, 1, 0) == 0x0022, "second redo");
    expect(histories.canUndo(key) && !histories.canRedo(key), "history fully redone");
    expect(histories.redo(key, document) == nullptr, "redo past the end");
}

void testHistoryPushClearsOnlyOwnRedo()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey sd = commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 1));
    const HistoryKey hd = commit(histories, document, makeFramePixel(0, Resolution::Hd, 1, 0, 2));
    histories.undo(sd, document);
    histories.undo(hd, document);
    expect(histories.canRedo(sd) && histories.canRedo(hd), "both histories have redo");

    commit(histories, document, makeFramePixel(0, Resolution::Sd, 2, 0, 3));
    expect(!histories.canRedo(sd), "push clears the key's redo stack");
    expect(histories.canUndo(sd), "push adds to the key's undo stack");
    expect(histories.canRedo(hd), "push leaves other redo stacks alone");
    expect(histories.redo(sd, document) == nullptr, "cleared redo cannot be reapplied");
    expect(document.colorizedPixel(0, Resolution::Sd, 1, 0) == 0, "cleared redo stays undone");

    expectThrows<std::invalid_argument>([&]() { histories.push(sd, nullptr); },
                                        "null command push");
}

void testHistoriesAreIndependent()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey frame0 =
        commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 0x0aaa));
    const HistoryKey frame1 =
        commit(histories, document, makeFramePixel(1, Resolution::Sd, 1, 0, 0x0bbb));
    const HistoryKey mask = commit(
        histories,
        document,
        std::make_unique<MaskEditCommand>(
            EditKind::DynamicMask,
            0,
            Resolution::Sd,
            std::vector<colorizing::edit::MaskPixelChange>{{1, 0, 255, 4}}));
    const HistoryKey palette = commit(
        histories,
        document,
        std::make_unique<PaletteEditCommand>(
            0,
            std::vector<colorizing::edit::PaletteColorChange>{{1, 0x1111, 0x2222}}));
    expect(!(frame0 == mask), "same frame, different kind, different history");

    const HistoryKey untouched{EditKind::FramePixels, 0, Resolution::Hd};
    expect(!histories.canUndo(untouched), "unused key has no history");

    histories.undo(frame0, document);
    expect(document.colorizedPixel(0, Resolution::Sd, 1, 0) == 0, "frame 0 undone");
    expect(document.colorizedPixel(1, Resolution::Sd, 1, 0) == 0x0bbb, "frame 1 kept");
    expect(document.dynamicMaskPixel(0, Resolution::Sd, 1, 0) == 4, "dynamic mask kept");
    expect(document.paletteColor(0, 1) == 0x2222, "palette kept");
    expect(!histories.canUndo(frame0) && histories.canUndo(frame1) && histories.canUndo(mask) &&
               histories.canUndo(palette),
           "only frame 0 history moved");

    histories.undo(palette, document);
    expect(document.paletteColor(0, 1) == 0x1111, "palette undone");
    expect(histories.canRedo(palette) && !histories.canRedo(frame1) && !histories.canRedo(mask),
           "only palette history has redo");
}

void testHistoryClear()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey sd = commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 1));
    const HistoryKey hd = commit(histories, document, makeFramePixel(0, Resolution::Hd, 1, 0, 2));
    histories.undo(hd, document);

    histories.clear();
    expect(!histories.canUndo(sd) && !histories.canRedo(sd), "clear drops undo stacks");
    expect(!histories.canUndo(hd) && !histories.canRedo(hd), "clear drops redo stacks");
    expect(histories.undo(sd, document) == nullptr, "undo after clear");
    expect(document.colorizedPixel(0, Resolution::Sd, 1, 0) == 1,
           "clear leaves the document alone");
}

}  // namespace

void runEditCommandTests()
{
    testFramePixelCommand();
    testBackgroundImageCommand();
    testSpriteImageCommand();
    testComparisonMaskCommand();
    testDynamicMaskCommand();
    testBackgroundMaskCommand();
    testDynamicColorCommand();
    testDynamicShadowCommand();
    testPaletteCommand();
    testCommandKindValidation();
    testHistoryKeys();
    testHistoryUndoRedoRoundTrip();
    testHistoryPushClearsOnlyOwnRedo();
    testHistoriesAreIndependent();
    testHistoryClear();
}
