// Component IDs: the UUIDs and their text, and how the document gives them out and keeps
// them through loads, copies and structure edits.

#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ComponentId.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/StructureCommand.h"

namespace {

using namespace colorizing;
using document::ComponentId;
using document::ComponentMeta;
using document::ProjectDocument;
using document::ProjectSnapshot;
namespace fixtures = runtime_fixtures;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: component ids: " << message << '\n';
        std::exit(1);
    }
}

// The IDs of all components of the document, by kind.
struct DocumentIds {
    std::vector<ComponentId> frames;
    std::vector<ComponentId> masks;
    std::vector<ComponentId> backgrounds;
    std::vector<ComponentId> sprites;
};

DocumentIds idsOf(const ProjectDocument& document)
{
    DocumentIds ids;
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        ids.frames.push_back(document.frameMeta(i).id);
    }
    for (std::size_t i = 0; i < document.comparisonMaskCount(); ++i) {
        ids.masks.push_back(document.comparisonMaskMeta(i).id);
    }
    for (std::size_t i = 0; i < document.backgroundCount(); ++i) {
        ids.backgrounds.push_back(document.backgroundMeta(i).id);
    }
    for (std::size_t i = 0; i < document.spriteCount(); ++i) {
        ids.sprites.push_back(document.spriteMeta(i).id);
    }
    return ids;
}

bool allSetAndUnique(const std::vector<ComponentId>& ids)
{
    const std::set<ComponentId> unique(ids.begin(), ids.end());
    return unique.size() == ids.size() && unique.count(ComponentId{}) == 0;
}

void testIdsAreVersion4Uuids()
{
    std::set<ComponentId> seen;
    for (int i = 0; i < 1000; ++i) {
        const ComponentId id = ComponentId::generate();
        expect(!id.isNull(), "a generated id is not null");
        expect(((id.high >> 12) & 0xf) == 4, "a generated id is a version 4 UUID");
        expect((id.low >> 62) == 2, "a generated id has the RFC 4122 variant");
        seen.insert(id);
    }
    expect(seen.size() == 1000, "generated ids differ");
}

void testIdText()
{
    const ComponentId id{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
    expect(id.toString() == "01234567-89ab-cdef-fedc-ba9876543210",
           "an id is written as 8-4-4-4-12 lowercase digits");
    expect(ComponentId::fromString(id.toString()) == id, "an id reads back from its text");
    expect(ComponentId::fromString("01234567-89AB-CDEF-FEDC-BA9876543210") == id,
           "uppercase digits are read too");
    const ComponentId generated = ComponentId::generate();
    expect(ComponentId::fromString(generated.toString()) == generated,
           "a generated id reads back from its text");
    for (const std::string& bad : {std::string{}, std::string("01234567-89ab-cdef-fedc"),
                                   std::string("01234567-89ab-cdef-fedc-ba987654321g"),
                                   std::string("0123456789ab-cdef-fedc-ba98-76543210"),
                                   std::string("01234567-89ab-cdef-fedc-ba98-6543210")}) {
        expect(!ComponentId::fromString(bad), "\"" + bad + "\" is no id");
    }
}

void testLoadGivesEveryComponentAnId()
{
    const ProjectDocument document =
        ProjectDocument::fromSnapshot(fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    const DocumentIds ids = idsOf(document);
    expect(!ids.frames.empty() && !ids.masks.empty() && !ids.backgrounds.empty() &&
               !ids.sprites.empty(),
           "the fixture has components of each kind");
    expect(allSetAndUnique(ids.frames), "every frame has its own id");
    expect(allSetAndUnique(ids.masks), "every comparison mask has its own id");
    expect(allSetAndUnique(ids.backgrounds), "every background has its own id");
    expect(allSetAndUnique(ids.sprites), "every sprite has its own id");
    for (std::size_t i = 0; i < ids.frames.size(); ++i) {
        expect(document.frameIndexOf(ids.frames[i]) == i, "a frame is found by its id");
    }
    expect(!document.frameIndexOf(ComponentId::generate()), "an unknown id finds no frame");
}

void testSnapshotKeepsIds()
{
    const ProjectDocument document =
        ProjectDocument::fromSnapshot(fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    ProjectSnapshot snapshot = document.toSnapshot();
    snapshot.frames[0].meta.author = "Author";
    snapshot.frames[0].meta.modifiedMs = 1234;
    const ProjectDocument copy = ProjectDocument::fromSnapshot(snapshot);
    const DocumentIds before = idsOf(document);
    const DocumentIds after = idsOf(copy);
    expect(before.frames == after.frames && before.masks == after.masks &&
               before.backgrounds == after.backgrounds && before.sprites == after.sprites,
           "a snapshot keeps the ids");
    expect(copy.frameMeta(0).author == "Author" && copy.frameMeta(0).modifiedMs == 1234,
           "a snapshot keeps the change information");

    const ProjectSnapshot subset = document.toSubsetSnapshot({1});
    expect(subset.frames.size() == 1 && subset.frames[0].meta.id == before.frames[1],
           "a subset keeps the frame's id");
    expect(subset.comparisonMaskMeta.size() == 1 &&
               subset.comparisonMaskMeta[0].id == before.masks[2],
           "a subset keeps the id of the mask the frame uses");
}

void testCopiesGetNewIds()
{
    ProjectSnapshot snapshot = fixtures::makeFullProject(render::PlaneLayout::Ordinary);
    const ComponentId shared = ComponentId::generate();
    snapshot.frames[2].meta.id = shared;
    snapshot.frames[5].meta.id = shared;
    snapshot.frames[5].meta.author = "Copied";
    snapshot.sprites[0].meta.id = shared;
    snapshot.sprites[1].meta.id = shared;
    snapshot.comparisonMaskMeta.resize(1);
    snapshot.comparisonMaskMeta[0].id = shared;
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const DocumentIds ids = idsOf(document);
    expect(allSetAndUnique(ids.frames) && allSetAndUnique(ids.sprites) &&
               allSetAndUnique(ids.masks),
           "duplicate ids are replaced");
    expect(ids.frames[2] == shared && ids.sprites[0] == shared && ids.masks[0] == shared,
           "the first of several with one id keeps it");
    expect(document.frameMeta(5).author.empty(), "a copy is a new component");

    // A frame put in with the id of one the document has becomes a new component.
    ProjectDocument edited = ProjectDocument::fromSnapshot(snapshot);
    std::vector<document::FrameRecord> frames;
    frames.push_back(edited.makeUncolorizedFrame(document::makeIndexedFrame(
                                                     edited.originalFrameSize(), 0),
                                                 100));
    const ComponentId original = edited.frameMeta(0).id;
    frames.back().meta.id = original;
    const auto added = edit::addFrames(edited, std::move(frames));
    const ComponentId first = edited.frameMeta(0).id;
    const ComponentId last = edited.frameMeta(edited.frameCount() - 1).id;
    expect(first == original && last != first && !last.isNull(),
           "an added copy of a frame gets a new id, the frame keeps its own");
}

void testStructureEditsKeepIds()
{
    ProjectDocument document =
        ProjectDocument::fromSnapshot(fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    const DocumentIds before = idsOf(document);

    const auto frames = edit::deleteFrames(document, {1, 4});
    expect(document.frameMeta(1).id == before.frames[2], "the frames behind keep their ids");
    frames->revert(document);
    expect(idsOf(document).frames == before.frames, "undoing a deletion brings the ids back");
    frames->apply(document);
    frames->revert(document);
    expect(idsOf(document).frames == before.frames, "redo and undo keep the ids");

    const auto mask = edit::deleteComparisonMask(document, 1);
    expect(document.comparisonMaskMeta(1).id == before.masks[2],
           "the masks behind keep their ids");
    mask->revert(document);
    expect(idsOf(document).masks == before.masks, "undoing a mask deletion brings its id back");

    const auto background = edit::deleteBackground(document, 0);
    background->revert(document);
    expect(idsOf(document).backgrounds == before.backgrounds,
           "undoing a background deletion brings its id back");

    const auto sprite = edit::deleteSprite(document, 1);
    sprite->revert(document);
    expect(idsOf(document).sprites == before.sprites,
           "undoing a sprite deletion brings its id back");

    const auto newMask = edit::addComparisonMask(document);
    const ComponentId maskId = document.comparisonMaskMeta(before.masks.size()).id;
    expect(!maskId.isNull() && allSetAndUnique(idsOf(document).masks),
           "a new mask gets its own id");
    newMask->revert(document);
    newMask->apply(document);
    expect(document.comparisonMaskMeta(before.masks.size()).id == maskId,
           "redoing an added mask keeps its id");

    const auto newSprite = edit::addSprite(document, "New", 0);
    const auto newBackground = edit::addBackgroundFromFrame(document, 0);
    const DocumentIds after = idsOf(document);
    expect(allSetAndUnique(after.sprites) && allSetAndUnique(after.backgrounds),
           "a new sprite and background get their own ids");
}

}  // namespace

void runComponentIdTests()
{
    testIdsAreVersion4Uuids();
    testIdText();
    testLoadGivesEveryComponentAnId();
    testSnapshotKeepsIds();
    testCopiesGetNewIds();
    testStructureEditsKeepIds();
}
