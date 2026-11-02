#include "edit/Bookmarks.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "platform/Utf8Path.h"
#include "project/ProjectFile.h"

namespace document = colorizing::document;
namespace edit = colorizing::edit;
namespace fixtures = colorizing::runtime_fixtures;
namespace platform = colorizing::platform;
namespace project = colorizing::project;

using document::ProjectDocument;
using Frames = std::vector<std::size_t>;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: bookmarks: " << message << '\n';
        std::exit(1);
    }
}

ProjectDocument makeDocument()
{
    return ProjectDocument::fromSnapshot(
        fixtures::makeProject(colorizing::render::PlaneLayout::Ordinary, 10));
}

void testToggle()
{
    ProjectDocument document = makeDocument();
    document.markClean();
    expect(edit::bookmarkedFrames(document).empty(), "a project starts without bookmarks");
    expect(edit::toggleBookmark(document, 7) && edit::toggleBookmark(document, 2),
           "a frame without one gets a bookmark");
    expect(document.isDirty(), "a bookmark is saved with the project, so it needs a save");
    expect(edit::bookmarkedFrames(document) == Frames{2, 7} && edit::isBookmarked(document, 7) &&
               !edit::isBookmarked(document, 3),
           "the frames with a bookmark, in their order, whatever the order they were set in");
    expect(!edit::toggleBookmark(document, 7) && edit::bookmarkedFrames(document) == Frames{2},
           "toggling again removes it");
}

void testAdjacent()
{
    const Frames frames{2, 7, 9};
    const auto next = [&](std::size_t from) { return *edit::adjacentBookmark(frames, from, true); };
    const auto previous = [&](std::size_t from) {
        return *edit::adjacentBookmark(frames, from, false);
    };
    expect(next(0) == 2 && next(2) == 7 && next(3) == 7, "next: the one after the current frame");
    expect(next(9) == 2 && next(12) == 2, "next starts over behind the last one");
    expect(previous(9) == 7 && previous(8) == 7 && previous(7) == 2,
           "previous: the one before the current frame");
    expect(previous(2) == 9 && previous(0) == 9, "previous starts over before the first one");
    expect(*edit::adjacentBookmark({5}, 5, true) == 5 &&
               *edit::adjacentBookmark({5}, 5, false) == 5,
           "the only bookmark is the next and the previous of itself");
    expect(!edit::adjacentBookmark({}, 5, true), "no frames, no bookmark");
}

void testFollowsTheFrame()
{
    ProjectDocument document = makeDocument();
    (void)edit::toggleBookmark(document, 5);
    const document::ComponentId id = document.frameMeta(5).id;
    document::RemovedFrames removed = document.removeFrames({1, 2});
    expect(edit::bookmarkedFrames(document) == Frames{3} && document.frameMeta(3).id == id,
           "deleting frames before it keeps the bookmark on its frame");
    document.insertFrames(std::move(removed));
    expect(edit::bookmarkedFrames(document) == Frames{5}, "back with them, it is where it was");
    removed = document.removeFrames({5});
    expect(edit::bookmarkedFrames(document).empty(), "a deleted frame shows no bookmark");
    document.insertFrames(std::move(removed));
    expect(edit::bookmarkedFrames(document) == Frames{5},
           "the undo of the deletion brings the bookmark back");
}

void testProjectFile()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       "colorizing_bookmarks_test.cdmd";
    const std::string utf8 = platform::utf8FromPath(path);
    ProjectDocument document = makeDocument();
    const std::string withoutBookmarks = project::settingsContent(document.toSnapshot());
    (void)edit::toggleBookmark(document, 3);
    (void)edit::toggleBookmark(document, 8);
    expect(project::settingsContent(document.toSnapshot()) == withoutBookmarks,
           "bookmarks are no shared setting of a team project");

    project::ProjectFileOptions options;
    options.editorVersion = "test";
    (void)project::saveProjectFile(utf8, document.toSnapshot(), options);
    ProjectDocument loaded =
        ProjectDocument::fromSnapshot(project::loadProjectFile(utf8).snapshot);
    expect(edit::bookmarkedFrames(loaded) == Frames{3, 8} &&
               loaded.frameMeta(3).id == document.frameMeta(3).id,
           "the bookmarks come back from the project file on their frames");
    expect(!loaded.isDirty(), "loading them does not change the project");

    // What is kept for an undo does not outlive the session.
    (void)document.removeFrames({8});
    (void)project::saveProjectFile(utf8, document.toSnapshot(), options);
    const auto snapshot = project::loadProjectFile(utf8).snapshot;
    expect(snapshot.bookmarks.size() == 1 && snapshot.bookmarks[0] == document.frameMeta(3).id,
           "a save leaves out the bookmark of a deleted frame");
    std::filesystem::remove(path);
}

}  // namespace

void runBookmarksTests()
{
    testToggle();
    testAdjacent();
    testFollowsTheFrame();
    testProjectFile();
}
