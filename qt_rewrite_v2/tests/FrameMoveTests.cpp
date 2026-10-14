#include "edit/FrameMove.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/EditorMetadata.h"
#include "document/ProjectDocument.h"
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"

using colorizing::document::ProjectDocument;
using colorizing::document::ProjectSnapshot;
using colorizing::document::kSectionNameBytes;
using colorizing::render::PlaneLayout;
namespace edit = colorizing::edit;
namespace document = colorizing::document;
namespace fixtures = colorizing::runtime_fixtures;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frame move: " << message << '\n';
        std::exit(1);
    }
}

// Ten frames, frame i lasting 100 + i ms so it can be told by its duration; sections
// "Attract" at 0, "Game" at 6 and "Bonus" at 3; sprite 0 taken from frame 7.
ProjectDocument makeDocument()
{
    ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, 10);
    for (std::size_t frame = 0; frame < snapshot.frames.size(); ++frame) {
        snapshot.frames[frame].durationMs = static_cast<uint32_t>(100 + frame);
    }
    document::EditorMetadata& metadata = snapshot.editorMetadata;
    metadata.sectionCount = 3;
    metadata.sectionFirstFrames[0] = 0;
    metadata.sectionFirstFrames[1] = 6;
    metadata.sectionFirstFrames[2] = 3;
    document::setFixedName(metadata.sectionNames, 0, kSectionNameBytes, "Attract");
    document::setFixedName(metadata.sectionNames, 1, kSectionNameBytes, "Game");
    document::setFixedName(metadata.sectionNames, 2, kSectionNameBytes, "Bonus");
    fixtures::addSprite(snapshot);
    snapshot.editorMetadata.spriteSourceFrames[0] = 7;
    return ProjectDocument::fromSnapshot(std::move(snapshot));
}

// The frames by their original numbers, in their order now.
std::vector<std::size_t> order(const ProjectDocument& document)
{
    std::vector<std::size_t> frames;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        frames.push_back(document.frameDurationMs(frame) - 100);
    }
    return frames;
}

std::vector<uint32_t> sectionStarts(const ProjectDocument& document)
{
    const document::RecordTables tables = document.recordTables();
    return {tables.sectionFirstFrames[0], tables.sectionFirstFrames[1],
            tables.sectionFirstFrames[2]};
}

void testMovesABlock()
{
    ProjectDocument document = makeDocument();
    const document::RecordTables before = document.recordTables();
    // 8 and 1 before frame 5, keeping their order: 1 then 8.
    auto command = edit::moveFrames(document, {8, 1}, 5);
    expect(command != nullptr, "a move is a command");
    expect(order(document) == std::vector<std::size_t>{0, 2, 3, 4, 1, 8, 5, 6, 7, 9},
           "the frames go as a block before frame 5");
    expect(command->movedFrames() == std::vector<std::size_t>{4, 5},
           "the moved frames are told by their new numbers");
    // Attract keeps 0, Game follows frame 6 (now 7), Bonus follows frame 3 (now 2).
    expect(sectionStarts(document) == std::vector<uint32_t>{0, 7, 2},
           "sections stay with their first frames");
    expect(document.spriteCount() == 1 && document.recordTables().spriteSourceFrames[0] == 8,
           "a sprite's source frame follows its frame");
    const auto renumbered = command->renumberedRecords();
    expect(renumbered.size() == 1 && renumbered[0].kind == edit::RecordKind::Frame &&
               renumbered[0].firstIndex == 1,
           "renumbered from the first frame whose number changes");

    command->revert(document);
    expect(order(document) == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9} &&
               document.recordTables().sectionFirstFrames == before.sectionFirstFrames &&
               document.recordTables().spriteSourceFrames == before.spriteSourceFrames,
           "undone, everything is as before");
    expect(command->movedFrames() == std::vector<std::size_t>{1, 8}, "and named as before");
    command->apply(document);
    expect(order(document) == std::vector<std::size_t>{0, 2, 3, 4, 1, 8, 5, 6, 7, 9},
           "redone");
}

void testEndsAndNothing()
{
    ProjectDocument document = makeDocument();
    expect(edit::moveFrames(document, {}, 3) == nullptr, "no frame, no command");
    expect(edit::moveFrames(document, {2, 3}, 2) == nullptr &&
               edit::moveFrames(document, {2, 3}, 4) == nullptr,
           "frames moved to where they are: no command");
    auto command = edit::moveFrames(document, {0}, document.frameCount());
    expect(command && order(document) == std::vector<std::size_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 0},
           "to the end");
    command = edit::moveFrames(document, {9}, 0);
    expect(command && order(document) == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
           "to the front");
    // Before one of the moved frames: before the next one that stays.
    command = edit::moveFrames(document, {4, 7}, 7);
    expect(command && order(document) == std::vector<std::size_t>{0, 1, 2, 3, 5, 6, 4, 7, 8, 9},
           "before a moved frame means before the next one that stays");
    bool thrown = false;
    try {
        (void)edit::moveFrames(document, {10}, 0);
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    expect(thrown && order(document).size() == 10, "a frame that does not exist is refused");
}

void testSections()
{
    // Frames of Bonus (3 to 5) all moved behind the last frame: Bonus goes with them.
    ProjectDocument document = makeDocument();
    auto command = edit::moveFrames(document, {3, 4, 5}, 10);
    expect(order(document) == std::vector<std::size_t>{0, 1, 2, 6, 7, 8, 9, 3, 4, 5} &&
               sectionStarts(document) == std::vector<uint32_t>{0, 3, 7},
           "a section whose frames all move goes with them");

    // Only the first frame of Game (6) moves: Game starts at its next frame (7).
    document = makeDocument();
    command = edit::moveFrames(document, {6}, 1);
    expect(order(document) == std::vector<std::size_t>{0, 6, 1, 2, 3, 4, 5, 7, 8, 9} &&
               sectionStarts(document) == std::vector<uint32_t>{0, 7, 4},
           "a section whose first frame moves alone starts at its next frame");

    // Frames moved right before the first frame of Game join Game.
    document = makeDocument();
    command = edit::moveFrames(document, {1, 2}, 6);
    expect(order(document) == std::vector<std::size_t>{0, 3, 4, 5, 1, 2, 6, 7, 8, 9} &&
               sectionStarts(document) == std::vector<uint32_t>{0, 4, 1},
           "frames moved before a section's first frame join that section");
    command->revert(document);
    expect(sectionStarts(document) == std::vector<uint32_t>{0, 6, 3}, "undone");
    // Without joining (moved down behind the last frame of Bonus): they stay in Bonus.
    command = edit::moveFrames(document, {4}, 6, false);
    expect(order(document) == std::vector<std::size_t>{0, 1, 2, 3, 5, 4, 6, 7, 8, 9} &&
               sectionStarts(document) == std::vector<uint32_t>{0, 6, 3},
           "without joining, frames moved before a section's first frame stay before it");

    // A whole section moved before another's first frame keeps itself.
    document = makeDocument();
    command = edit::moveFrames(document, {3, 4, 5}, 0);
    expect(order(document) == std::vector<std::size_t>{3, 4, 5, 0, 1, 2, 6, 7, 8, 9} &&
               sectionStarts(document) == std::vector<uint32_t>{3, 6, 0},
           "a section moved before another's first frame stays a section of its own");
}

void testHistory()
{
    ProjectDocument document = makeDocument();
    edit::UndoHistories histories;
    histories.push(edit::structureHistoryKey(), edit::moveFrames(document, {9}, 0));
    expect(histories.undoLatest({edit::structureHistoryKey()}, document) != nullptr &&
               order(document) == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
           "undone through the structure history");
    expect(histories.redoLatest({edit::structureHistoryKey()}, document) != nullptr &&
               order(document).front() == 9,
           "and redone");
}

}  // namespace

void runFrameMoveTests()
{
    testMovesABlock();
    testEndsAndNothing();
    testSections();
    testHistory();
}
