#include "edit/FrameMoveMarks.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/FrameMove.h"
#include "edit/Sections.h"

namespace document = colorizing::document;
namespace edit = colorizing::edit;
namespace fixtures = colorizing::runtime_fixtures;

using document::ComponentId;
using document::ProjectDocument;
using Frames = std::vector<std::size_t>;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frame move marks: " << message << '\n';
        std::exit(1);
    }
}

ProjectDocument makeDocument()
{
    return ProjectDocument::fromSnapshot(
        fixtures::makeProject(colorizing::render::PlaneLayout::Ordinary, 10));
}

std::vector<ComponentId> idsOf(const ProjectDocument& document, const Frames& frames)
{
    std::vector<ComponentId> ids;
    for (const std::size_t frame : frames) {
        ids.push_back(document.frameMeta(frame).id);
    }
    return ids;
}

void moveMarked(ProjectDocument& document,
                const edit::FrameMoveMarks& marks,
                std::size_t frame,
                bool behind)
{
    const edit::FrameMovePlace place = edit::frameMovePlace(frame, behind);
    expect(edit::moveFrames(document, marks.frames(document), place.beforeFrame,
                            place.joinSection) != nullptr,
           "the marked frames move");
}

void testMarks()
{
    ProjectDocument document = makeDocument();
    document.markClean();
    edit::FrameMoveMarks marks;
    expect(marks.isEmpty() && marks.frames(document).empty(), "nothing is marked at first");
    marks.mark(document, {7, 2});
    expect(marks.frames(document) == Frames{2, 7}, "the marked frames, in the order of the frames");
    expect(!document.isDirty(), "marks are no change of the project");
    marks.mark(document, {4});
    expect(marks.frames(document) == Frames{4}, "marking again replaces the marks");
    marks.clear();
    expect(marks.isEmpty() && marks.frames(document).empty(), "clearing ends them");
}

void testMarksFollowTheirFrames()
{
    ProjectDocument document = makeDocument();
    edit::FrameMoveMarks marks;
    marks.mark(document, {7, 8});
    const std::vector<ComponentId> ids = idsOf(document, {7, 8});

    const std::unique_ptr<edit::FrameMoveCommand> other = edit::moveFrames(document, {0, 1}, 10);
    expect(marks.frames(document) == Frames{5, 6} && idsOf(document, {5, 6}) == ids,
           "the marks stay on their frames when others move");
    other->revert(document);
    expect(marks.frames(document) == Frames{7, 8}, "and when that is undone");

    document::RemovedFrames removed = document.removeFrames({2, 8});
    expect(marks.frames(document) == Frames{6}, "a deleted frame is no longer among them");
    document.insertFrames(std::move(removed));
    expect(marks.frames(document) == Frames{7, 8}, "an undo brings it back");
}

void testMoveBeforeAndBehind()
{
    // Looking for the target changes nothing of the numbers given: they are the current ones,
    // also when the frames go towards the end.
    {
        ProjectDocument document = makeDocument();
        const std::vector<ComponentId> moved = idsOf(document, {1, 2});
        const ComponentId target = document.frameMeta(6).id;
        edit::FrameMoveMarks marks;
        marks.mark(document, {1, 2});
        moveMarked(document, marks, 6, false);
        expect(marks.frames(document) == Frames{4, 5} && idsOf(document, {4, 5}) == moved &&
                   document.frameMeta(6).id == target,
               "before a later frame: right before it");
    }
    {
        ProjectDocument document = makeDocument();
        const std::vector<ComponentId> moved = idsOf(document, {1, 2});
        const ComponentId target = document.frameMeta(6).id;
        edit::FrameMoveMarks marks;
        marks.mark(document, {1, 2});
        moveMarked(document, marks, 6, true);
        expect(marks.frames(document) == Frames{5, 6} && idsOf(document, {5, 6}) == moved &&
                   document.frameMeta(4).id == target,
               "behind a later frame: right behind it");
    }
    {
        ProjectDocument document = makeDocument();
        const std::vector<ComponentId> moved = idsOf(document, {7, 8});
        edit::FrameMoveMarks marks;
        marks.mark(document, {7, 8});
        moveMarked(document, marks, 0, false);
        expect(marks.frames(document) == Frames{0, 1} && idsOf(document, {0, 1}) == moved,
               "before the first frame");
        moveMarked(document, marks, 9, true);
        expect(marks.frames(document) == Frames{8, 9} && idsOf(document, {8, 9}) == moved,
               "behind the last frame");
    }
}

void testFramesThereAlready()
{
    ProjectDocument document = makeDocument();
    edit::FrameMoveMarks marks;
    marks.mark(document, {3, 4});
    for (const auto& [frame, behind] : std::vector<std::pair<std::size_t, bool>>{
             {3, false}, {4, true}, {5, false}, {2, true}}) {
        const edit::FrameMovePlace place = edit::frameMovePlace(frame, behind);
        expect(edit::moveFrames(document, marks.frames(document), place.beforeFrame,
                                place.joinSection) == nullptr,
               "a marked frame or its neighbour as the target changes nothing");
    }
    expect(marks.frames(document) == Frames{3, 4}, "and the marks stay");
}

void testFrameLeftBehind()
{
    ProjectDocument document = makeDocument();
    const auto leftBehind = [&](const Frames& frames) {
        return edit::frameLeftBehindByMove(document, frames);
    };
    const auto idOf = [&](std::size_t frame) { return document.frameMeta(frame).id; };
    expect(!leftBehind({}), "no frames, no frame left behind");
    expect(leftBehind({3, 4}) == idOf(5), "the frame behind them");
    expect(leftBehind({0}) == idOf(1), "also at the start");
    expect(leftBehind({3, 4, 7}) == idOf(5), "of frames apart, the first gap");
    expect(leftBehind({8, 9}) == idOf(7), "at the end, the frame before them");
    expect(leftBehind({0, 1, 2, 3, 4, 5, 6, 7, 8, 9}) == std::nullopt, "every frame moves");

    // It goes by the ID: towards the start, the frame's number grows by the frames moved.
    const std::optional<ComponentId> next = leftBehind({7, 8});
    expect(edit::moveFrames(document, {7, 8}, 1) != nullptr, "the frames move");
    expect(next && document.frameIndexOf(*next) == 9, "the frame to go on with is found again");
}

void testSections()
{
    const auto sectionFirstFrames = [](const ProjectDocument& document) {
        Frames first;
        for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
            if (edit::sectionStartingAt(document, frame)) {
                first.push_back(frame);
            }
        }
        return first;
    };
    const auto withSection = []() {
        ProjectDocument document = makeDocument();
        (void)edit::addOrRenameSection(document, 5, "Second");
        return document;
    };
    {
        ProjectDocument document = withSection();
        edit::FrameMoveMarks marks;
        marks.mark(document, {8});
        moveMarked(document, marks, 5, false);
        expect(sectionFirstFrames(document) == Frames{5} && marks.frames(document) == Frames{5},
               "before a section's first frame they join it");
    }
    {
        ProjectDocument document = withSection();
        edit::FrameMoveMarks marks;
        marks.mark(document, {8});
        moveMarked(document, marks, 4, true);
        expect(sectionFirstFrames(document) == Frames{6} && marks.frames(document) == Frames{5},
               "behind the frame before it they stay in the section of that frame");
    }
}

}  // namespace

void runFrameMoveMarksTests()
{
    testMarks();
    testMarksFollowTheirFrames();
    testMoveBeforeAndBehind();
    testFramesThereAlready();
    testFrameLeftBehind();
    testSections();
}
