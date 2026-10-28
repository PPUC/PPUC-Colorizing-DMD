#include "edit/StateCommands.h"
#include "edit/SpriteAssignment.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"

using colorizing::document::CompactArray;
using colorizing::document::FrameSpriteReference;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectSnapshot;
using colorizing::document::SpriteDetectionArea;
using colorizing::document::kNoSpriteId;
using colorizing::edit::CommandGroup;
using colorizing::edit::EditCommand;
using colorizing::edit::EditKind;
using colorizing::edit::EditTarget;
using colorizing::edit::FrameSpriteSlotsCommand;
using colorizing::edit::RecordReference;
using colorizing::edit::SpriteState;
using colorizing::edit::UndoHistories;
using colorizing::render::PlaneLayout;
using colorizing::render::Resolution;
namespace edit = colorizing::edit;
namespace fixtures = colorizing::runtime_fixtures;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: state commands: " << message << '\n';
        std::exit(1);
    }
}

bool sameSlot(const FrameSpriteReference& a, const FrameSpriteReference& b)
{
    return a.spriteId == b.spriteId && a.minX == b.minX && a.minY == b.minY &&
           a.maxX == b.maxX && a.maxY == b.maxY;
}

bool sameSlots(const std::vector<FrameSpriteReference>& a,
               const std::vector<FrameSpriteReference>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!sameSlot(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

// Two frames, two sprites; frame 0 holds sprite 0 in slot 0.
ProjectDocument makeDocument()
{
    ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, 3);
    fixtures::addSprite(snapshot);
    fixtures::addSprite(snapshot);
    fixtures::setSpriteSlot(snapshot, 0, 0, 0, 1, 1, 10, 10);
    return ProjectDocument::fromSnapshot(std::move(snapshot));
}

void testCaptureIsOneStep()
{
    ProjectDocument document = makeDocument();
    const SpriteState spriteBefore = edit::spriteState(document, 1);
    const std::vector<FrameSpriteReference> slotsBefore = document.frameSpriteReferences(0);

    // What a capture into sprite 1 on frame 0 does.
    document.replaceSpriteFromFrame(1,
                                    document.originalFrame(0),
                                    document.colorizedFrame(0, Resolution::Sd),
                                    2,
                                    2,
                                    6,
                                    4);
    document.setSpriteDetectionArea(1, 0, SpriteDetectionArea{0, 0, 6, 4});
    document.setSpriteSourceFrame(1, 0);
    document.setFrameSpriteReference(0, 1, FrameSpriteReference{1, 2, 2, 7, 5});
    const SpriteState spriteAfter = edit::spriteState(document, 1);
    const std::vector<FrameSpriteReference> slotsAfter = document.frameSpriteReferences(0);

    std::vector<std::unique_ptr<EditCommand>> commands;
    commands.push_back(edit::frameSpriteSlotsCommand(document, 0, slotsBefore));
    commands.push_back(edit::spriteStateCommand(document, 1, spriteBefore));
    std::unique_ptr<EditCommand> capture = edit::groupCommands(std::move(commands));
    expect(capture && dynamic_cast<CommandGroup*>(capture.get()), "a capture is a group");
    const EditTarget target = capture->target();
    expect(target.kind == EditKind::FrameAttributes && target.index == 0,
           "in the frame's history");
    bool dropsSpritePixels = false;
    for (const edit::ReplacedData& replaced : capture->replacedData()) {
        dropsSpritePixels |= replaced.kind == EditKind::SpriteImage && replaced.index == 1;
    }
    expect(dropsSpritePixels, "the new pictures drop the sprite's pixel histories");

    capture->revert(document);
    expect(document.sprite(1).originalSd.pixels == spriteBefore.sprite.originalSd.pixels &&
               document.sprite(1).colorizedSd.pixels ==
                   spriteBefore.sprite.colorizedSd.pixels &&
               document.sprite(1).detectionAreas[0].width ==
                   spriteBefore.sprite.detectionAreas[0].width &&
               document.spriteSourceFrame(1) == spriteBefore.sourceFrame,
           "undo puts the sprite back");
    expect(sameSlots(document.frameSpriteReferences(0), slotsBefore),
           "undo puts the frame's slots back");

    capture->apply(document);
    expect(document.sprite(1).originalSd.pixels == spriteAfter.sprite.originalSd.pixels &&
               document.spriteSourceFrame(1) == 0 &&
               sameSlots(document.frameSpriteReferences(0), slotsAfter),
           "redo captures again");
    expect(document.editorMetadata().spriteSourceFrames[1] == 0,
           "the source frame is what the cRP writes");
}

void testDetectionAreaKeepsPixelHistories()
{
    ProjectDocument document = makeDocument();
    SpriteState before = edit::spriteState(document, 0);
    document.setSpriteDetectionArea(0, 1, SpriteDetectionArea{0, 0, 2, 2});
    std::unique_ptr<EditCommand> command = edit::spriteStateCommand(document, 0, before);
    expect(command && command->target().kind == EditKind::SpriteAttributes &&
               command->replacedData().empty(),
           "an area edit is a sprite change that keeps the pixel histories");
    command->revert(document);
    expect(document.sprite(0).detectionAreas[1].width == 0xffff, "undo clears the area again");

    SpriteState renamed = edit::spriteState(document, 0);
    document.setSpriteName(0, "renamed");
    std::unique_ptr<EditCommand> rename = edit::spriteStateCommand(document, 0, renamed);
    expect(rename != nullptr, "renaming is a sprite change");
    rename->revert(document);
    expect(document.sprite(0).name == renamed.sprite.name, "undo restores the name");

    expect(edit::spriteStateCommand(document, 0, edit::spriteState(document, 0)) == nullptr,
           "no command without a change");
}

void testSlotsKeepGaps()
{
    ProjectDocument document = makeDocument();
    std::vector<FrameSpriteReference> before = document.frameSpriteReferences(0);
    document.setFrameSpriteReference(0, 1, FrameSpriteReference{1, 0, 0, 5, 5});
    std::unique_ptr<EditCommand> assign = edit::frameSpriteSlotsCommand(document, 0, before);
    expect(assign != nullptr, "assigning is a slot change");

    before = document.frameSpriteReferences(0);
    document.clearFrameSpriteReference(0, 0);
    std::unique_ptr<EditCommand> remove = edit::frameSpriteSlotsCommand(document, 0, before);
    expect(document.frameSpriteReferences(0)[0].spriteId == 1, "the slots behind moved up");
    remove->revert(document);
    expect(sameSlots(document.frameSpriteReferences(0), before), "undo of a removal");
    assign->revert(document);
    expect(document.frameSpriteReferences(0)[1].spriteId == kNoSpriteId, "undo of an assign");

    // A frame without a slot list goes back to having none.
    expect(document.frameSpriteReferences(1).empty(), "frame 1 has no slots");
    document.setFrameSpriteReference(1, 0, FrameSpriteReference{0, 0, 0, 3, 3});
    std::unique_ptr<EditCommand> first = edit::frameSpriteSlotsCommand(document, 1, {});
    first->revert(document);
    expect(document.frameSpriteReferences(1).empty(), "undo leaves no slot list");

    bool namesSprite = false;
    for (const RecordReference& reference : assign->recordReferences()) {
        namesSprite |= reference.kind == edit::RecordKind::Sprite && reference.index == 1;
    }
    expect(namesSprite, "a slot change names the sprites it holds");
}

void testColorRotations()
{
    ProjectDocument document = makeDocument();
    const CompactArray<uint16_t> before = document.colorRotations(0, Resolution::Sd);
    document.setColorRotationLength(0, Resolution::Sd, 1, 3);
    document.setColorRotationColor(0, Resolution::Sd, 1, 0, 0x1234);
    std::unique_ptr<EditCommand> command =
        edit::colorRotationsCommand(document, 0, Resolution::Sd, before);
    expect(command && command->target().kind == EditKind::FrameAttributes,
           "rotations are a frame change");
    command->revert(document);
    expect(document.colorRotations(0, Resolution::Sd) == before &&
               document.colorRotationLength(0, Resolution::Sd, 1) == 0,
           "undo restores the rotations");
    command->apply(document);
    expect(document.colorRotationColor(0, Resolution::Sd, 1, 0) == 0x1234, "redo");
    expect(edit::colorRotationsCommand(document,
                                       0,
                                       Resolution::Sd,
                                       document.colorRotations(0, Resolution::Sd)) == nullptr,
           "no command without a change");
}

class ThrowingCommand final : public EditCommand {
public:
    void apply(ProjectDocument&) const override { throw std::runtime_error("apply"); }
    void revert(ProjectDocument&) const override { throw std::runtime_error("revert"); }
    [[nodiscard]] EditTarget target() const override { return {}; }
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override { return {}; }
};

void testGroupRollsBack()
{
    ProjectDocument document = makeDocument();
    const std::vector<FrameSpriteReference> before = document.frameSpriteReferences(0);
    std::vector<FrameSpriteReference> after = before;
    after[1] = FrameSpriteReference{1, 0, 0, 5, 5};
    std::vector<std::unique_ptr<EditCommand>> commands;
    commands.push_back(std::make_unique<FrameSpriteSlotsCommand>(0, before, after));
    commands.push_back(std::make_unique<ThrowingCommand>());
    const CommandGroup group(std::move(commands));
    bool threw = false;
    try {
        group.apply(document);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw && sameSlots(document.frameSpriteReferences(0), before),
           "a group that fails leaves the document as it was");
    expect(edit::groupCommands({}) == nullptr, "an empty group is no command");
}

void testFrameDeletionDropsSpriteSteps()
{
    ProjectDocument document = makeDocument();
    UndoHistories histories;
    SpriteState before = edit::spriteState(document, 1);
    document.setSpriteSourceFrame(1, 2);
    std::unique_ptr<EditCommand> command = edit::spriteStateCommand(document, 1, before);
    const edit::HistoryKey key = edit::historyKey(command->target());
    histories.push(key, std::move(command));
    expect(histories.canUndo(key), "the step is there");
    histories.push(edit::structureHistoryKey(), edit::deleteFrames(document, {1}));
    expect(!histories.canUndo(key),
           "deleting a frame before the source frame drops the step that names it");
    expect(document.spriteSourceFrame(1) == 1, "and the source frame moved up");
}

}  // namespace

// Issue #38: several sprites onto several frames, as the Win32 editor's Add Sprite did; one
// command per changed frame, undone to the slots before.
void testAddSpritesToFrames()
{
    ProjectDocument document = makeDocument();
    const std::vector<FrameSpriteReference> frame0 = document.frameSpriteReferences(0);
    const std::vector<FrameSpriteReference> frame1 = document.frameSpriteReferences(1);
    expect(edit::framesPassingSpriteWarning(document, {0, 1}, {0, 1}) == 0,
           "two sprites pass no warning");
    edit::SpritesAdded added = edit::addSpritesToFrames(document, {0, 1}, {0, 1});
    const auto& slots0 = document.frameSpriteReferences(0);
    const auto& slots1 = document.frameSpriteReferences(1);
    expect(added.added == 3 && added.kept == 1 && added.full == 0 && added.commands.size() == 2,
           "frame 0 keeps sprite 0 and gets sprite 1, frame 1 gets both");
    expect(slots0[0].spriteId == 0 && slots0[0].maxX == 10 && slots0[1].spriteId == 1 &&
               slots1[0].spriteId == 0 && slots1[1].spriteId == 1,
           "in the first empty slots; a sprite the frame had keeps its slot and bounds");
    const colorizing::document::Size size = document.originalFrameSize();
    expect(slots1[0].minX == 0 && slots1[0].minY == 0 && slots1[0].maxX == size.width - 1 &&
               slots1[0].maxY == size.height - 1,
           "a new slot covers the whole frame");
    for (auto command = added.commands.rbegin(); command != added.commands.rend(); ++command) {
        (*command)->revert(document);
    }
    expect(sameSlots(document.frameSpriteReferences(0), frame0) &&
               sameSlots(document.frameSpriteReferences(1), frame1),
           "undone, the slots are as before");
    added = edit::addSpritesToFrames(document, {0}, {0});
    expect(added.added == 0 && added.kept == 1 && added.commands.empty(),
           "a frame that has every sprite changes nothing");

    // The bounds and offset of a slot the author set up go to every new slot.
    FrameSpriteReference bounds;
    bounds.spriteId = 0;
    bounds.minX = 2;
    bounds.minY = 3;
    bounds.maxX = 6;
    bounds.maxY = 7;
    bounds.offsetX = 5;
    bounds.offsetY = -1;
    added = edit::addSpritesToFrames(document, {0, 1}, {0, 1}, bounds);
    const auto& copied = document.frameSpriteReferences(1);
    expect(added.added == 3 && copied[0].spriteId == 0 && copied[1].spriteId == 1,
           "the sprites go on as before");
    expect(copied[1].minX == 2 && copied[1].minY == 3 && copied[1].maxX == 6 &&
               copied[1].maxY == 7 && copied[1].offsetX == 5 && copied[1].offsetY == -1,
           "with the slot's bounds and offset");
    expect(document.frameSpriteReferences(0)[0].maxX == 10 &&
               document.frameSpriteReferences(0)[0].offsetX == 0,
           "a sprite the frame had keeps its own");
}

void runStateCommandsTests()
{
    testAddSpritesToFrames();
    testCaptureIsOneStep();
    testDetectionAreaKeepsPixelHistories();
    testSlotsKeepGaps();
    testColorRotations();
    testGroupRollsBack();
    testFrameDeletionDropsSpriteSteps();
}
