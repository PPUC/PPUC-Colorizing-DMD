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
using colorizing::document::FrameHdParts;
using colorizing::document::ProjectCreateOptions;
using colorizing::document::kNoTriggerId;
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
using colorizing::edit::BackgroundHdCreateCommand;
using colorizing::edit::DynamicColorEditCommand;
using colorizing::edit::DynamicShadowEditCommand;
using colorizing::edit::EditCommand;
using colorizing::edit::EditKind;
using colorizing::edit::EditTarget;
using colorizing::edit::FrameAttribute;
using colorizing::edit::FrameAttributeEditCommand;
using colorizing::edit::FrameHdCommand;
using colorizing::edit::HistoryKey;
using colorizing::edit::MaskEditCommand;
using colorizing::edit::PaletteEditCommand;
using colorizing::edit::PixelEditCommand;
using colorizing::edit::ReplacedData;
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

// The dynamic data of an ordinary project's HD plane is stored, not edited: the runtime takes
// the zones, colors and shadows of the SD plane also for the HD picture. No command reaches
// it, so no history can hold one.
void testNoDynamicCommandsOnTheHdPlaneOfAnOrdinaryProject()
{
    ProjectDocument document = makeDocument();
    document.markClean();
    expect(document.dynamicPlane() == Resolution::Sd, "an ordinary project's dynamic plane is SD");

    const MaskEditCommand mask(EditKind::DynamicMask, 1, Resolution::Hd, {{4, 2, 255, 3}});
    const DynamicColorEditCommand color(1, Resolution::Hd, 2, 3, 0, 0x1234);
    const DynamicShadowEditCommand shadow(1, Resolution::Hd, 0, true, true, {0, 1, 0, 0x4321});
    UndoHistories histories;
    for (const EditCommand* command :
         {static_cast<const EditCommand*>(&mask), static_cast<const EditCommand*>(&color),
          static_cast<const EditCommand*>(&shadow)}) {
        expectThrows<std::logic_error>([&]() { command->apply(document); },
                                       "an HD dynamic command cannot be applied");
        expectThrows<std::logic_error>([&]() { command->revert(document); },
                                       "an HD dynamic command cannot be reverted");
    }
    expect(!document.isDirty() && document.dynamicMaskPixel(1, Resolution::Hd, 4, 2) == 255 &&
               document.dynamicColor(1, Resolution::Hd, 2, 3) == 0 &&
               document.dynamicShadowDirection(1, Resolution::Hd, 0) == 0,
           "the stored HD dynamic data is unchanged");
    // The way an edit gets into a history: applied first, pushed if that worked.
    for (EditKind kind : {EditKind::DynamicMask, EditKind::DynamicColor, EditKind::DynamicShadow}) {
        expect(!histories.canUndo(HistoryKey{kind, 1, Resolution::Hd}),
               "no HD dynamic history has an entry");
    }
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
    document.setDynamicShadowColor(1, Resolution::Sd, 2, 0x0f0f);

    const DynamicShadowEditCommand direction(1,
                                             Resolution::Sd,
                                             2,
                                             true,
                                             false,
                                             {0x01, 0x09, 0, 0xffff});
    expectTarget(direction, EditKind::DynamicShadow, 1, Resolution::Sd, {1}, "shadow");

    direction.apply(document);
    expect(document.dynamicShadowDirection(1, Resolution::Sd, 2) == 0x09,
           "shadow direction apply");
    expect(document.dynamicShadowColor(1, Resolution::Sd, 2) == 0x0f0f,
           "direction-only command leaves color alone");
    expect(document.dynamicShadowDirection(1, Resolution::Hd, 2) == 0,
           "shadow other resolution untouched");
    expect(document.dynamicShadowDirection(0, Resolution::Sd, 2) == 0,
           "shadow other frame untouched");
    direction.revert(document);
    expect(document.dynamicShadowDirection(1, Resolution::Sd, 2) == 0x01,
           "shadow direction revert");

    const DynamicShadowEditCommand color(1,
                                         Resolution::Sd,
                                         2,
                                         false,
                                         true,
                                         {0x7f, 0x7f, 0x0f0f, 0x2222});
    expectTarget(color, EditKind::DynamicShadow, 1, Resolution::Sd, {1}, "shadow color");
    color.apply(document);
    expect(document.dynamicShadowColor(1, Resolution::Sd, 2) == 0x2222, "shadow color apply");
    expect(document.dynamicShadowDirection(1, Resolution::Sd, 2) == 0x01,
           "color-only command leaves direction alone");
    expect(document.dynamicShadowColor(0, Resolution::Sd, 2) == 0,
           "shadow color other frame untouched");
    color.revert(document);
    expect(document.dynamicShadowColor(1, Resolution::Sd, 2) == 0x0f0f, "shadow color revert");
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

void testBackgroundHdCreateCommand()
{
    ProjectDocument document = makeDocument();
    document.removeBackgroundHdFrame(0);
    UndoHistories histories;
    const HistoryKey key = commit(histories,
                                  document,
                                  std::make_unique<BackgroundHdCreateCommand>(
                                      0,
                                      makeRgb565Frame(kHdFrameSize, 0x0abc)));
    expect(key == HistoryKey{EditKind::BackgroundImage, 0, Resolution::Hd},
           "HD creation shares the history of HD background pixel edits");
    expect(document.backgroundPixel(0, Resolution::Hd, 5, 6) == 0x0abc, "background HD apply");
    expect(document.backgroundPixel(0, Resolution::Sd, 5, 6) == 0x5050,
           "background HD creation leaves SD alone");

    commit(histories,
           document,
           std::make_unique<PixelEditCommand>(
               EditKind::BackgroundImage,
               0,
               Resolution::Hd,
               std::vector<colorizing::edit::PixelChange>{{5, 6, 0x0abc, 0x0def}}));
    histories.undo(key, document);
    expect(document.backgroundPixel(0, Resolution::Hd, 5, 6) == 0x0abc,
           "pixel edit on the created HD image undoes first");
    histories.undo(key, document);
    expect(!document.background(0).colorizedHd.has_value(), "background HD revert removes HD");
    histories.redo(key, document);
    histories.redo(key, document);
    expect(document.backgroundPixel(0, Resolution::Hd, 5, 6) == 0x0def,
           "background HD and its pixel edit redo in order");
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

    const DynamicShadowEditCommand shadow(5, Resolution::Sd, 0, true, false, {0, 1, 0, 0});
    expect(historyKey(shadow.target()) == HistoryKey{EditKind::DynamicShadow, 5, Resolution::Sd},
           "shadow history belongs to its frame");
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

// The editor records one command per selected frame, so each frame undoes on its own.
void testMultiFrameShadowEditHasOneHistoryPerFrame()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    std::vector<HistoryKey> keys;
    for (std::size_t frameIndex = 0; frameIndex < 2; ++frameIndex) {
        keys.push_back(commit(histories,
                              document,
                              std::make_unique<DynamicShadowEditCommand>(
                                  frameIndex,
                                  Resolution::Sd,
                                  3,
                                  true,
                                  false,
                                  colorizing::edit::DynamicShadowChange{0, 0x04, 0, 0})));
    }
    expect(!(keys[0] == keys[1]), "each frame owns its shadow history");

    histories.undo(keys[1], document);
    expect(document.dynamicShadowDirection(1, Resolution::Sd, 3) == 0, "frame 1 shadow undone");
    expect(document.dynamicShadowDirection(0, Resolution::Sd, 3) == 0x04,
           "undo on frame 1 leaves frame 0 alone");
    expect(histories.canUndo(keys[0]) && !histories.canUndo(keys[1]),
           "frame 0 keeps its own undo step");
}

void testHistoryClearKey()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey sd = commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 1));
    const HistoryKey hd = commit(histories, document, makeFramePixel(0, Resolution::Hd, 1, 0, 2));
    commit(histories, document, makeFramePixel(0, Resolution::Hd, 2, 0, 3));
    histories.undo(hd, document);
    const HistoryKey otherFrame =
        commit(histories, document, makeFramePixel(1, Resolution::Hd, 1, 0, 4));

    histories.clear(hd);
    expect(!histories.canUndo(hd) && !histories.canRedo(hd), "key clear drops undo and redo");
    expect(histories.undo(hd, document) == nullptr && histories.redo(hd, document) == nullptr,
           "cleared key has nothing to undo or redo");
    expect(histories.canUndo(sd), "key clear keeps the other resolution");
    expect(histories.canUndo(otherFrame), "key clear keeps other indices");
    expect(document.colorizedPixel(0, Resolution::Hd, 1, 0) == 2,
           "key clear leaves the document alone");

    histories.clear(HistoryKey{EditKind::FramePixels, 7, Resolution::Sd});
    expect(histories.canUndo(sd) && histories.canUndo(otherFrame), "clearing an unused key");

    commit(histories, document, makeFramePixel(0, Resolution::Hd, 3, 0, 5));
    expect(histories.canUndo(hd), "cleared key records new commands");
}

void testHistoryClearKindAndIndex()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey sd = commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 1));
    const HistoryKey hd = commit(histories, document, makeFramePixel(0, Resolution::Hd, 1, 0, 2));
    const HistoryKey otherFrame =
        commit(histories, document, makeFramePixel(1, Resolution::Sd, 1, 0, 3));
    const HistoryKey mask = commit(
        histories,
        document,
        std::make_unique<MaskEditCommand>(
            EditKind::DynamicMask,
            0,
            Resolution::Sd,
            std::vector<colorizing::edit::MaskPixelChange>{{1, 0, 255, 4}}));
    histories.undo(sd, document);

    histories.clear(EditKind::FramePixels, 0);
    expect(!histories.canUndo(sd) && !histories.canRedo(sd) && !histories.canUndo(hd),
           "kind and index clear drops both resolutions");
    expect(histories.canUndo(otherFrame), "kind and index clear keeps other indices");
    expect(histories.canUndo(mask), "kind and index clear keeps other kinds");
}

void testFrameAttributeCommand()
{
    ProjectDocument document = makeDocument();
    document.setComparisonMaskId(1, 255);
    document.setTriggerId(1, kNoTriggerId);
    document.setFrameDurationMs(1, 40);
    document.markClean();

    struct Case {
        FrameAttribute attribute;
        uint32_t oldValue;
        uint32_t newValue;
    };
    const std::vector<Case> cases{
        {FrameAttribute::ComparisonMaskId, 255, 0},
        {FrameAttribute::ShapeComparisonMode, 0, 1},
        {FrameAttribute::BackgroundId, 0, 0xffff},
        {FrameAttribute::TriggerId, kNoTriggerId, 65432},
        {FrameAttribute::DurationMs, 40, 250},
    };
    for (const Case& item : cases) {
        const std::string name =
            "frame attribute " + std::to_string(static_cast<int>(item.attribute));
        expect(colorizing::edit::frameAttributeValue(document, 1, item.attribute) ==
                   item.oldValue,
               name + " initial value");
        const FrameAttributeEditCommand command(1, item.attribute, item.oldValue, item.newValue);
        expectTarget(command, EditKind::FrameAttributes, 1, Resolution::Sd, {1}, name);
        expect(command.attribute() == item.attribute, name + " attribute");
        expect(command.replacedData().empty(), name + " replaces no data");

        document.markClean();
        command.apply(document);
        expect(colorizing::edit::frameAttributeValue(document, 1, item.attribute) ==
                   item.newValue,
               name + " apply");
        expect(document.isDirty(), name + " apply marks dirty");
        expect(colorizing::edit::frameAttributeValue(document, 0, item.attribute) !=
                       item.newValue ||
                   item.attribute == FrameAttribute::ComparisonMaskId,
               name + " leaves the other frame alone");
        command.revert(document);
        expect(colorizing::edit::frameAttributeValue(document, 1, item.attribute) ==
                   item.oldValue,
               name + " revert");
    }
    expect(document.comparisonMaskId(0) == 0 && document.shapeComparisonMode(0) == 0 &&
               document.backgroundId(0) == 0 && document.frameDurationMs(0) == 0,
           "frame attribute commands leave the other frame alone");

    // A mode from a file that the editor does not offer has to survive an undo.
    document.restoreShapeComparisonMode(0, 3);
    const FrameAttributeEditCommand legacyMode(0, FrameAttribute::ShapeComparisonMode, 3, 1);
    legacyMode.apply(document);
    expect(document.shapeComparisonMode(0) == 1, "legacy shape mode apply");
    legacyMode.revert(document);
    expect(document.shapeComparisonMode(0) == 3, "legacy shape mode revert");
    const FrameAttributeEditCommand toLegacyMode(0, FrameAttribute::ShapeComparisonMode, 1, 3);
    expectThrows<std::invalid_argument>([&]() { toLegacyMode.apply(document); },
                                        "a legacy shape mode cannot be newly set");

    expectThrows<std::invalid_argument>(
        []() { FrameAttributeEditCommand(0, FrameAttribute::ComparisonMaskId, 0, 256); },
        "comparison mask ID range");
    expectThrows<std::invalid_argument>(
        []() { FrameAttributeEditCommand(0, FrameAttribute::BackgroundId, 0x10000, 0); },
        "background ID range");

    ProjectDocument plain = ProjectDocument::createEmpty(ProjectCreateOptions{"plain", 1, false});
    const FrameAttributeEditCommand background(0, FrameAttribute::BackgroundId, 0xffff, 0);
    expectThrows<std::logic_error>([&]() { background.apply(plain); },
                                   "background ID needs background data");
    expectThrows<std::out_of_range>(
        [&]() { FrameAttributeEditCommand(4, FrameAttribute::DurationMs, 0, 1).apply(plain); },
        "frame attribute frame bounds");
}

bool replaces(const EditCommand& command, EditKind kind, std::size_t index)
{
    for (const ReplacedData& replaced : command.replacedData()) {
        if (replaced.kind == kind && replaced.index == index) {
            if (replaced.resolution != Resolution::Hd) {
                fail("only HD data is replaced");
            }
            return true;
        }
    }
    return false;
}

void testFrameHdCommands()
{
    ProjectDocument document = makeDocument();
    document.setColorizedPixel(0, Resolution::Sd, 4, 2, 0x1234);
    document.setColorizedPixel(0, Resolution::Hd, 9, 5, 0x4321);

    expect(colorizing::edit::createFrameHd(document, 0, {}) == nullptr,
           "Create HD does nothing for a frame with HD");
    const std::unique_ptr<EditCommand> remove = colorizing::edit::removeFrameHd(document, 0);
    expect(remove != nullptr && !document.hasHdFrame(0), "Remove HD is applied when returned");
    expectTarget(*remove, EditKind::FrameAttributes, 0, Resolution::Sd, {0}, "remove HD");
    expect(replaces(*remove, EditKind::FramePixels, 0) &&
               !replaces(*remove, EditKind::DynamicMask, 0) &&
               !replaces(*remove, EditKind::BackgroundMask, 0) &&
               !replaces(*remove, EditKind::BackgroundImage, 0) &&
               !replaces(*remove, EditKind::FramePixels, 1),
           "Remove HD replaces only the frame's HD pixels");
    expect(colorizing::edit::removeFrameHd(document, 0) == nullptr,
           "Remove HD does nothing for a frame without HD");
    remove->revert(document);
    expect(document.hasHdFrame(0) && document.colorizedPixel(0, Resolution::Hd, 9, 5) == 0x4321 &&
               document.colorizedPixel(0, Resolution::Hd, 8, 4) == 0,
           "undoing Remove HD restores the HD pixels that were removed");
    remove->apply(document);
    expect(!document.hasHdFrame(0) && document.hasHdFrame(1), "redoing Remove HD");

    // Give every related HD part a value that differs from what the upscale produces. The
    // HD dynamic data of an ordinary project can only come from a file: it is not editable.
    document.setDynamicMaskPixel(0, Resolution::Sd, 3, 2, 7);
    document.setDynamicColor(0, Resolution::Sd, 2, 3, 0x3333);
    document.setDynamicShadowDirection(0, Resolution::Sd, 1, 0x10);
    document.setDynamicShadowColor(0, Resolution::Sd, 1, 0x00ff);
    {
        colorizing::document::ProjectSnapshot stored = document.toSnapshot();
        colorizing::document::FrameDynamicData& dynamic = *stored.frames[0].dynamicData;
        dynamic.maskHd.pixels[40 * 256 + 100] = 5;
        dynamic.colorsHd[2 * stored.colorCount + 3] = 0x2222;
        dynamic.shadowDirectionsHd.resize(colorizing::document::kMaxDynamicSetsPerFrame, 0);
        dynamic.shadowColorsHd.resize(colorizing::document::kMaxDynamicSetsPerFrame, 0);
        dynamic.shadowDirectionsHd[1] = 0x02;
        dynamic.shadowColorsHd[1] = 0xff00;
        document = ProjectDocument::fromSnapshot(stored);
    }
    const auto expectStoredHdDynamics = [&](const std::string& name) {
        expect(document.dynamicMaskPixel(0, Resolution::Hd, 6, 4) == 255 &&
                   document.dynamicMaskPixel(0, Resolution::Hd, 100, 40) == 5 &&
                   document.dynamicColor(0, Resolution::Hd, 2, 3) == 0x2222 &&
                   document.dynamicShadowDirection(0, Resolution::Hd, 1) == 0x02 &&
                   document.dynamicShadowColor(0, Resolution::Hd, 1) == 0xff00,
               name + ": the stored HD dynamic data is left as it is");
    };
    document.setBackgroundMaskPixel(0, Resolution::Sd, 5, 4, 1);
    document.setBackgroundMaskPixel(0, Resolution::Hd, 200, 50, 1);
    document.setBackgroundPixel(0, Resolution::Sd, 7, 6, 0x4444);
    document.setBackgroundPixel(0, Resolution::Hd, 30, 20, 0x7777);

    const std::unique_ptr<EditCommand> create =
        colorizing::edit::createFrameHd(document, 0, FrameHdParts{true, true});
    const auto expectCreated = [&](const std::string& name) {
        expect(document.hasHdFrame(0) &&
                   document.colorizedPixel(0, Resolution::Hd, 9, 5) == 0x1234 &&
                   document.colorizedPixel(0, Resolution::Hd, 8, 4) == 0x1234,
               name + ": HD frame upscaled from SD");
        expectStoredHdDynamics(name);
        expect(document.backgroundMaskPixel(0, Resolution::Hd, 10, 8) == 1 &&
                   document.backgroundMaskPixel(0, Resolution::Hd, 200, 50) == 0,
               name + ": HD background mask upscaled from SD");
        expect(document.backgroundPixel(0, Resolution::Hd, 14, 12) == 0x4444 &&
                   document.backgroundPixel(0, Resolution::Hd, 30, 20) == 0x5050,
               name + ": HD background image upscaled from SD");
    };
    expect(create != nullptr, "Create HD returns its command");
    expectCreated("Create HD");
    expectTarget(*create, EditKind::FrameAttributes, 0, Resolution::Sd, {0}, "create HD");
    expect(replaces(*create, EditKind::FramePixels, 0) &&
               replaces(*create, EditKind::BackgroundMask, 0) &&
               replaces(*create, EditKind::BackgroundImage, 0),
           "Create HD replaces every HD part it upscaled");
    expect(!replaces(*create, EditKind::DynamicMask, 0) &&
               !replaces(*create, EditKind::DynamicColor, 0) &&
               !replaces(*create, EditKind::DynamicShadow, 0),
           "Create HD replaces no dynamic data: no dynamic history is dropped for it");

    create->revert(document);
    expect(!document.hasHdFrame(0), "undoing Create HD removes the HD frame");
    expectStoredHdDynamics("undoing Create HD");
    expect(document.backgroundMaskPixel(0, Resolution::Hd, 10, 8) == 0 &&
               document.backgroundMaskPixel(0, Resolution::Hd, 200, 50) == 1,
           "undoing Create HD restores the HD background mask");
    expect(document.backgroundPixel(0, Resolution::Hd, 14, 12) == 0x6060 &&
               document.backgroundPixel(0, Resolution::Hd, 30, 20) == 0x7777,
           "undoing Create HD restores the HD background image");
    expect(document.dynamicMaskPixel(0, Resolution::Sd, 3, 2) == 7 &&
               document.colorizedPixel(0, Resolution::Sd, 4, 2) == 0x1234 &&
               document.backgroundPixel(0, Resolution::Sd, 7, 6) == 0x4444,
           "undoing Create HD leaves SD data alone");
    create->apply(document);
    expectCreated("redone Create HD");
    create->revert(document);

    // Only the parts that were asked for are touched, and a background that had no HD image
    // has none again after the undo.
    document.removeBackgroundHdFrame(0);
    const std::unique_ptr<EditCommand> frameOnly =
        colorizing::edit::createFrameHd(document, 0, {});
    expect(document.hasHdFrame(0) && !document.background(0).colorizedHd &&
               document.dynamicMaskPixel(0, Resolution::Hd, 100, 40) == 5 &&
               document.backgroundMaskPixel(0, Resolution::Hd, 200, 50) == 1,
           "Create HD without options leaves the related HD data alone");
    expect(!replaces(*frameOnly, EditKind::DynamicMask, 0) &&
               !replaces(*frameOnly, EditKind::BackgroundImage, 0),
           "Create HD without options replaces only the frame's HD pixels");
    frameOnly->revert(document);
    const std::unique_ptr<EditCommand> withBackground =
        colorizing::edit::createFrameHd(document, 0, FrameHdParts{false, true});
    expect(document.background(0).colorizedHd.has_value() &&
               document.backgroundPixel(0, Resolution::Hd, 14, 12) == 0x4444,
           "Create HD gives the assigned background an HD image");
    withBackground->revert(document);
    expect(!document.hasHdFrame(0) && !document.background(0).colorizedHd,
           "undoing Create HD removes the background HD image it created");
    withBackground->apply(document);
    expect(document.background(0).colorizedHd.has_value(), "redoing Create HD with background");

    ProjectDocument plain = ProjectDocument::createEmpty(ProjectCreateOptions{"plain", 1, false});
    plain.markClean();
    const FrameHdParts backgroundMaskOnly{true, false};
    expectThrows<std::logic_error>(
        [&]() { (void)colorizing::edit::createFrameHd(plain, 0, backgroundMaskOnly); },
        "Create HD with a background mask the frame does not have");
    expect(!plain.hasHdFrame(0) && !plain.isDirty(), "a failed Create HD changes nothing");
}

void testSpriteHdCommands()
{
    ProjectDocument document = makeDocument();
    document.setSpriteOriginalPixel(0, 0, 0, 1);
    document.setSpriteOriginalPixel(0, 1, 0, 1);
    document.setSpriteColorizedPixel(0, Resolution::Sd, 1, 0, 0x1111);
    document.setSpriteColorizedPixel(0, Resolution::Hd, 0, 0, 0x2222);

    expect(colorizing::edit::createSpriteHd(document, 0) == nullptr,
           "Create HD does nothing for a sprite with HD");
    const std::unique_ptr<EditCommand> remove = colorizing::edit::removeSpriteHd(document, 0);
    expect(remove != nullptr && !document.hasHdSprite(0), "Remove HD sprite is applied");
    expectTarget(*remove, EditKind::SpriteAttributes, 0, Resolution::Sd, {}, "remove sprite HD");
    expect(replaces(*remove, EditKind::SpriteImage, 0) && remove->replacedData().size() == 1,
           "Remove HD sprite replaces the sprite's HD pixels");
    expect(colorizing::edit::removeSpriteHd(document, 0) == nullptr,
           "Remove HD does nothing for a sprite without HD");

    const std::unique_ptr<EditCommand> create = colorizing::edit::createSpriteHd(document, 0);
    expect(create != nullptr && document.hasHdSprite(0) &&
               document.spriteColorizedPixel(0, Resolution::Hd, 2, 0) == 0x1111 &&
               document.spriteColorizedPixel(0, Resolution::Hd, 0, 0) == 0,
           "Create HD sprite upscales the SD sprite");
    create->revert(document);
    expect(!document.hasHdSprite(0), "undoing Create HD sprite removes HD");
    remove->revert(document);
    expect(document.hasHdSprite(0) &&
               document.spriteColorizedPixel(0, Resolution::Hd, 0, 0) == 0x2222 &&
               document.spriteColorizedPixel(0, Resolution::Hd, 2, 0) == 0,
           "undoing Remove HD sprite restores the HD pixels from before Create HD");
    remove->apply(document);
    create->apply(document);
    expect(document.hasHdSprite(0) &&
               document.spriteColorizedPixel(0, Resolution::Hd, 2, 0) == 0x1111,
           "redoing Create HD sprite");
}

std::unique_ptr<EditCommand> makePaletteColor(uint16_t oldColor, uint16_t newColor)
{
    return std::make_unique<PaletteEditCommand>(
        0,
        std::vector<colorizing::edit::PaletteColorChange>{{2, oldColor, newColor}});
}

void testHistoryUndoLatestIsChronological()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey frame =
        commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 0x0001));
    const HistoryKey palette = commit(histories, document, makePaletteColor(0, 0x0002));
    commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0x0001, 0x0003));
    const HistoryKey other =
        commit(histories, document, makeFramePixel(1, Resolution::Sd, 1, 0, 0x0004));
    // Listed with the palette first, as a context that used to prefer it would.
    const std::vector<HistoryKey> keys{palette, frame};
    const auto state = [&]() {
        return std::vector<uint16_t>{document.colorizedPixel(0, Resolution::Sd, 1, 0),
                                     document.paletteColor(0, 2),
                                     document.colorizedPixel(1, Resolution::Sd, 1, 0)};
    };
    using State = std::vector<uint16_t>;

    expect(histories.canUndoAny(keys) && !histories.canRedoAny(keys) &&
               !histories.canUndoAny({}) &&
               !histories.canUndoAny({HistoryKey{EditKind::Palette, 9, Resolution::Sd}}),
           "can undo any of the given histories");
    expect(histories.undoLatest({}, document) == nullptr &&
               histories.redoLatest(keys, document) == nullptr,
           "nothing to undo without histories and nothing to redo before an undo");

    const EditCommand* undone = histories.undoLatest(keys, document);
    expect(undone != nullptr && undone->target().kind == EditKind::FramePixels &&
               state() == State{0x0001, 0x0002, 0x0004},
           "the latest command of the given histories is undone first");
    expect(histories.canRedoAny(keys), "can redo after an undo");
    undone = histories.undoLatest(keys, document);
    expect(undone != nullptr && undone->target().kind == EditKind::Palette &&
               state() == State{0x0001, 0, 0x0004},
           "the palette edit made between two frame edits is undone second");
    histories.undoLatest(keys, document);
    expect(state() == State{0, 0, 0x0004} && !histories.canUndoAny(keys),
           "the first command is undone last and other histories are left alone");
    expect(histories.undoLatest(keys, document) == nullptr && histories.canUndo(other),
           "undo past the start of the given histories");

    const EditCommand* redone = histories.redoLatest(keys, document);
    expect(redone != nullptr && redone->target().kind == EditKind::FramePixels &&
               state() == State{0x0001, 0, 0x0004},
           "redo replays the first command first");
    redone = histories.redoLatest(keys, document);
    expect(redone != nullptr && redone->target().kind == EditKind::Palette &&
               state() == State{0x0001, 0x0002, 0x0004},
           "redo replays the commands in their original order");
    histories.redoLatest(keys, document);
    expect(state() == State{0x0003, 0x0002, 0x0004} && !histories.canRedoAny(keys),
           "redo replays the last command last");
    expect(histories.redoLatest(keys, document) == nullptr, "redo past the end");

    // An undo followed by a redo, and a redo followed by an undo, each restore the state.
    histories.undoLatest(keys, document);
    histories.undoLatest(keys, document);
    const State twoUndone = state();
    histories.undoLatest(keys, document);
    histories.redoLatest(keys, document);
    expect(state() == twoUndone, "undo then redo is a no-op");
    histories.redoLatest(keys, document);
    histories.undoLatest(keys, document);
    expect(state() == twoUndone, "redo then undo is a no-op");

    // A command redone after a newer edit counts as the latest one.
    commit(histories, document, makeFramePixel(0, Resolution::Sd, 2, 0, 0x0005));
    expect(histories.canRedo(palette) && !histories.canRedo(frame),
           "a push clears only its own redo stack");
    histories.redoLatest(keys, document);
    expect(document.paletteColor(0, 2) == 0x0002, "the palette edit is redone");
    undone = histories.undoLatest(keys, document);
    expect(undone != nullptr && undone->target().kind == EditKind::Palette &&
               document.paletteColor(0, 2) == 0 &&
               document.colorizedPixel(0, Resolution::Sd, 2, 0) == 0x0005,
           "the command redone last is undone first");

    // A single listed history behaves like undo and redo on that key.
    histories.undoLatest({other}, document);
    expect(document.colorizedPixel(1, Resolution::Sd, 1, 0) == 0 &&
               document.colorizedPixel(0, Resolution::Sd, 2, 0) == 0x0005,
           "histories that are not listed are not touched");
}

void testReplacingCommandDropsHistories()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    const HistoryKey frameSd =
        commit(histories, document, makeFramePixel(0, Resolution::Sd, 1, 0, 1));
    const HistoryKey frameHd =
        commit(histories, document, makeFramePixel(0, Resolution::Hd, 1, 0, 2));
    const HistoryKey otherHd =
        commit(histories, document, makeFramePixel(1, Resolution::Hd, 1, 0, 3));
    const HistoryKey attributes{EditKind::FrameAttributes, 0, Resolution::Sd};
    const std::vector<HistoryKey> keys{frameSd, frameHd, attributes};

    std::unique_ptr<EditCommand> remove = colorizing::edit::removeFrameHd(document, 0);
    expect(historyKey(remove->target()) == attributes, "Remove HD belongs to the frame attributes");
    histories.push(attributes, std::move(remove));
    expect(!histories.canUndo(frameHd) && histories.canUndo(frameSd) &&
               histories.canUndo(otherHd) && histories.canUndo(attributes),
           "pushing Remove HD drops the frame's HD pixel history only");

    const EditCommand* undone = histories.undoLatest(keys, document);
    expect(undone != nullptr && undone->target().kind == EditKind::FrameAttributes &&
               document.hasHdFrame(0) && document.colorizedPixel(0, Resolution::Hd, 1, 0) == 2,
           "Remove HD is undone as the latest command and restores the HD pixels");

    commit(histories, document, makeFramePixel(0, Resolution::Hd, 1, 2, 4));
    expect(histories.canUndo(frameHd), "HD edits after the undo are recorded");
    histories.redoLatest({attributes}, document);
    expect(!document.hasHdFrame(0) && !histories.canUndo(frameHd) &&
               !histories.canRedo(frameHd),
           "redoing Remove HD drops the HD pixel history again");
    histories.undo(attributes, document);
    expect(document.hasHdFrame(0) && !histories.canUndo(frameHd) && histories.canRedo(attributes),
           "undoing Remove HD keeps the HD pixel history dropped");

    const FrameHdCommand unchanged(0,
                                   document.frameHdState(0, {}),
                                   document.frameHdState(0, {}));
    expect(unchanged.replacedData().size() == 1, "a frame HD command always replaces HD pixels");
}

}  // namespace

void runEditCommandTests()
{
    testFramePixelCommand();
    testBackgroundImageCommand();
    testSpriteImageCommand();
    testComparisonMaskCommand();
    testDynamicMaskCommand();
    testNoDynamicCommandsOnTheHdPlaneOfAnOrdinaryProject();
    testBackgroundMaskCommand();
    testDynamicColorCommand();
    testDynamicShadowCommand();
    testPaletteCommand();
    testCommandKindValidation();
    testBackgroundHdCreateCommand();
    testHistoryKeys();
    testHistoryUndoRedoRoundTrip();
    testHistoryPushClearsOnlyOwnRedo();
    testHistoriesAreIndependent();
    testHistoryClear();
    testHistoryClearKey();
    testHistoryClearKindAndIndex();
    testMultiFrameShadowEditHasOneHistoryPerFrame();
    testFrameAttributeCommand();
    testFrameHdCommands();
    testSpriteHdCommands();
    testHistoryUndoLatestIsChronological();
    testReplacingCommandDropsHistories();
}
