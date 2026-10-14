#include "edit/Sections.h"

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
using colorizing::edit::EditCommand;
using colorizing::edit::EditKind;
using colorizing::render::PlaneLayout;
namespace edit = colorizing::edit;
namespace document = colorizing::document;
namespace fixtures = colorizing::runtime_fixtures;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: sections: " << message << '\n';
        std::exit(1);
    }
}

template <typename Exception, typename Function>
bool throws(Function function)
{
    try {
        function();
    } catch (const Exception&) {
        return true;
    }
    return false;
}

// Ten frames; sections "Attract" at 0, "Game" at 6 and "Bonus" at 3, in that list order.
ProjectDocument makeDocument()
{
    ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, 10);
    document::EditorMetadata& metadata = snapshot.editorMetadata;
    metadata.sectionCount = 3;
    metadata.sectionFirstFrames[0] = 0;
    metadata.sectionFirstFrames[1] = 6;
    metadata.sectionFirstFrames[2] = 3;
    document::setFixedName(metadata.sectionNames, 0, kSectionNameBytes, "Attract");
    document::setFixedName(metadata.sectionNames, 1, kSectionNameBytes, "Game");
    document::setFixedName(metadata.sectionNames, 2, kSectionNameBytes, "Bonus");
    return ProjectDocument::fromSnapshot(std::move(snapshot));
}

std::string names(const ProjectDocument& document)
{
    std::string text;
    for (const edit::Section& section : edit::sections(document)) {
        text += section.name + "@" + std::to_string(section.firstFrame) + " ";
    }
    return text;
}

void testLookups()
{
    const ProjectDocument document = makeDocument();
    expect(names(document) == "Attract@0 Game@6 Bonus@3 ", "the list in its own order");
    expect(edit::sectionOfFrame(document, 0) == 0u && edit::sectionOfFrame(document, 2) == 0u &&
               edit::sectionOfFrame(document, 4) == 2u && edit::sectionOfFrame(document, 9) == 1u,
           "a frame belongs to the latest section that starts at or before it");
    expect(edit::sectionStartingAt(document, 3) == 2u && !edit::sectionStartingAt(document, 4),
           "where sections start");
    expect(edit::sectionName(document, 1) == "Game", "a name");
}

void testNames()
{
    const ProjectDocument document = makeDocument();
    expect(edit::sectionNameProblem(document, "Multiball").empty(), "a new name is fine");
    expect(!edit::sectionNameProblem(document, "").empty(), "a name is needed");
    expect(!edit::sectionNameProblem(document, std::string(32, 'x')).empty() &&
               edit::sectionNameProblem(document, std::string(31, 'x')).empty(),
           "at most 31 characters");
    expect(!edit::sectionNameProblem(document, "- None -").empty(), "not the no-section entry");
    expect(!edit::sectionNameProblem(document, "Game").empty() &&
               edit::sectionNameProblem(document, "Game", 1).empty(),
           "not another section's name, its own is fine");
}

void testAddRenameDelete()
{
    ProjectDocument document = makeDocument();
    std::unique_ptr<EditCommand> added = edit::addOrRenameSection(document, 8, "Multiball");
    expect(added && added->target().kind == EditKind::Sections,
           "adding is a section change");
    expect(names(document) == "Attract@0 Game@6 Bonus@3 Multiball@8 ", "added behind the others");
    std::unique_ptr<EditCommand> renamed = edit::addOrRenameSection(document, 6, "Main game");
    expect(names(document) == "Attract@0 Main game@6 Bonus@3 Multiball@8 ",
           "a frame that starts a section renames it");
    expect(edit::addOrRenameSection(document, 6, "Main game") == nullptr, "same name, no change");
    expect(throws<std::invalid_argument>([&]() {
               (void)edit::addOrRenameSection(document, 5, "Bonus");
           }) && names(document) == "Attract@0 Main game@6 Bonus@3 Multiball@8 ",
           "a taken name changes nothing");
    expect(throws<std::out_of_range>([&]() {
               (void)edit::addOrRenameSection(document, 10, "Late");
           }),
           "a frame that does not exist");

    const document::EditorMetadata before = document.editorMetadata();
    std::unique_ptr<EditCommand> deleted = edit::deleteSection(document, 1);
    expect(names(document) == "Attract@0 Bonus@3 Multiball@8 ", "deleted, the others move up");
    const document::EditorMetadata& after = document.editorMetadata();
    expect(after.sectionFirstFrames[3] == before.sectionFirstFrames[3] &&
               document::fixedName(after.sectionNames, 3, kSectionNameBytes) == "Multiball",
           "the old last entry stays behind the count, as the Win32 editor leaves it");

    deleted->revert(document);
    expect(names(document) == "Attract@0 Main game@6 Bonus@3 Multiball@8 ", "undo of a delete");
    renamed->revert(document);
    added->revert(document);
    expect(names(document) == "Attract@0 Game@6 Bonus@3 ", "undo back to the start");
    added->apply(document);
    expect(names(document) == "Attract@0 Game@6 Bonus@3 Multiball@8 ", "redo");
}

void testOrder()
{
    ProjectDocument document = makeDocument();
    std::unique_ptr<EditCommand> up = edit::moveSection(document, 2, -1);
    expect(names(document) == "Attract@0 Bonus@3 Game@6 ", "moved up with its first frame");
    expect(edit::moveSection(document, 0, -1) == nullptr &&
               edit::moveSection(document, 2, 1) == nullptr,
           "not past the ends");
    std::unique_ptr<EditCommand> sorted = edit::sortSectionsByName(document);
    expect(names(document) == "Attract@0 Bonus@3 Game@6 " && sorted == nullptr,
           "already sorted");
    up->revert(document);
    sorted = edit::sortSectionsByName(document);
    expect(sorted && names(document) == "Attract@0 Bonus@3 Game@6 ", "sorted by name");
    expect(edit::sectionOfFrame(document, 7) == 2u, "frames follow their section");
}

void testFrameDeletionDropsSectionSteps()
{
    ProjectDocument document = makeDocument();
    edit::UndoHistories histories;
    std::unique_ptr<EditCommand> added = edit::addOrRenameSection(document, 8, "Multiball");
    histories.push(edit::sectionsHistoryKey(), std::move(added));
    expect(histories.canUndo(edit::sectionsHistoryKey()), "the step is there");
    histories.push(edit::structureHistoryKey(), edit::deleteFrames(document, {1}));
    expect(!histories.canUndo(edit::sectionsHistoryKey()),
           "deleting a frame moves the first frames and drops the section steps");
    expect(names(document) == "Attract@0 Game@5 Bonus@2 Multiball@7 ",
           "the sections followed the deletion");
}

void testTriggerList()
{
    ProjectSnapshot snapshot = fixtures::makeProject(PlaneLayout::Ordinary, 6);
    snapshot.frames[4].triggerId = 3;
    snapshot.frames[1].triggerId = 7;
    snapshot.frames[2].triggerId = 3;
    snapshot.frames[5].triggerId = 65432;
    const ProjectDocument document = ProjectDocument::fromSnapshot(std::move(snapshot));
    const std::vector<edit::TriggerEntry> list = edit::triggerList(document);
    std::string text;
    for (const edit::TriggerEntry& entry : list) {
        text += std::to_string(entry.frameIndex) + ":" + std::to_string(entry.triggerId) + " ";
    }
    expect(text == "2:3 4:3 1:7 5:65432 ", "by trigger ID, then frame: " + text);
}

}  // namespace

void runSectionsTests()
{
    testLookups();
    testNames();
    testAddRenameDelete();
    testOrder();
    testFrameDeletionDropsSectionSteps();
    testTriggerList();
}
