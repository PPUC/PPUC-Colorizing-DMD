#include "edit/SceneEdits.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "edit/StructureCommand.h"
#include "runtime/Scenes.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: scene edits: " << message << '\n';
        std::exit(1);
    }
}

bool sameFrame(const document::IndexedFrame& a, const document::IndexedFrame& b)
{
    if (a.size.width != b.size.width || a.size.height != b.size.height ||
        a.pixels.size() != b.pixels.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.pixels.size(); ++index) {
        if (a.pixels[index] != b.pixels[index]) {
            return false;
        }
    }
    return true;
}

// Every frame linked to the scene has its marker as the original.
bool markersMatch(const document::ProjectDocument& document, uint16_t sceneId)
{
    const document::SceneDefinition* scene =
        document.colorizationSettings() ? document.colorizationSettings()->scene(sceneId)
                                        : nullptr;
    for (std::size_t frame : edit::sceneFrames(document, sceneId)) {
        if (!scene || !sameFrame(document.originalFrame(frame),
                                 runtime::sceneMarkerFrame(*scene, *document.sceneFrame(frame),
                                                           document.colorCount()))) {
            return false;
        }
    }
    return true;
}

document::SceneDefinition makeScene(uint16_t id, uint16_t frames, uint8_t groups)
{
    document::SceneDefinition scene;
    scene.sceneId = id;
    scene.frameCount = frames;
    scene.frameGroups = groups;
    scene.durationMs = 30;
    return scene;
}

void testAddAndUndo()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3));
    const auto command = edit::addScene(document, makeScene(50500, 2, 2));
    expect(command && command->changesFrames(), "adding a scene adds frames");
    expect(document.frameCount() == 7, "one frame per frame of each group, behind the others");
    const std::vector<std::size_t> frames = edit::sceneFrames(document, 50500);
    expect(frames == std::vector<std::size_t>{3, 4, 5, 6} &&
               document.sceneFrame(3) == document::SceneFrameRef{50500, 1, 0} &&
               document.sceneFrame(6) == document::SceneFrameRef{50500, 2, 1} &&
               document.frameDurationMs(4) == 30,
           "in group and frame order, lasting the scene's frame duration");
    expect(markersMatch(document, 50500), "with the scene markers as originals");
    expect(document.colorizationSettings() &&
               document.colorizationSettings()->scenes.size() == 1,
           "the project has the scene");
    command->revert(document);
    expect(document.frameCount() == 3 && !document.colorizationSettings(),
           "undo takes both back");
    command->apply(document);
    expect(document.frameCount() == 7 && markersMatch(document, 50500), "redo puts them back");

    bool refused = false;
    try {
        (void)edit::addScene(document, makeScene(50500, 1, 1));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && document.frameCount() == 7, "a second scene 50500 is refused");
    expect(!edit::sceneProblem(document, makeScene(50501, 0, 1)).empty() &&
               !edit::sceneProblem(document, makeScene(50501, 1, 0)).empty() &&
               edit::sceneProblem(document, makeScene(50501, 1, 1)).empty() &&
               edit::sceneProblem(document, makeScene(50500, 1, 1), 50500).empty(),
           "a scene needs a frame, a group and its own ID");
}

void testChange()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    auto added = edit::addScene(document, makeScene(50010, 2, 1));
    auto other = edit::addScene(document, makeScene(50020, 1, 1));
    expect(document.frameCount() == 5, "two scenes added");

    // More frames: behind the scene's last frame, before the next scene.
    document::SceneDefinition longer = makeScene(50010, 4, 1);
    longer.durationMs = 70;
    const auto grow = edit::changeScene(document, 50010, longer);
    expect(grow && document.frameCount() == 7 &&
               edit::sceneFrames(document, 50010) == std::vector<std::size_t>{2, 3, 4, 5} &&
               document.sceneFrame(5) == document::SceneFrameRef{50010, 1, 3} &&
               edit::sceneFrames(document, 50020) == std::vector<std::size_t>{6},
           "frames a scene gains go behind its last frame");
    expect(document.frameDurationMs(2) == 70 && document.frameDurationMs(5) == 70,
           "and all of them get the new duration");
    expect(markersMatch(document, 50010), "with their markers");

    // Fewer frames, another ID: frames go, the others get the new markers.
    const document::IndexedFrame before = document.originalFrame(2);
    const auto shrink = edit::changeScene(document, 50010, makeScene(50011, 1, 1));
    expect(shrink && document.frameCount() == 4 &&
               edit::sceneFrames(document, 50011) == std::vector<std::size_t>{2} &&
               edit::sceneFrames(document, 50010).empty() && markersMatch(document, 50011) &&
               !sameFrame(document.originalFrame(2), before),
           "frames a scene loses are deleted, the others follow its new ID");
    shrink->revert(document);
    expect(document.frameCount() == 7 && markersMatch(document, 50010) &&
               sameFrame(document.originalFrame(2), before),
           "undo puts the frames and their originals back");
    grow->revert(document);
    expect(document.frameCount() == 5 && edit::sceneFrames(document, 50010).size() == 2,
           "and the frames it gained go again");

    // A frame deleted from the strip comes back when the scene is applied again.
    auto deleted = edit::deleteFrames(document, std::vector<std::size_t>{3});
    expect(document.frameCount() == 4, "a scene frame is deleted");
    const auto again = edit::changeScene(document, 50010, makeScene(50010, 2, 1));
    expect(again && document.frameCount() == 5 &&
               document.sceneFrame(3) == document::SceneFrameRef{50010, 1, 1},
           "and made again, in its place behind the scene's frames");
    expect(!edit::changeScene(document, 50010, makeScene(50010, 2, 1)),
           "an unchanged scene with all its frames changes nothing");
    (void)added;
    (void)other;
    (void)deleted;
}

void testRemove()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    auto added = edit::addScene(document, makeScene(50007, 3, 1));
    const auto keep = edit::removeScene(document, 50007, false);
    expect(keep && document.frameCount() == 5 && edit::sceneFrames(document, 50007).empty() &&
               document.colorizationSettings()->scenes.empty(),
           "a scene removed without its frames leaves them as ordinary frames");
    keep->revert(document);
    expect(edit::sceneFrames(document, 50007).size() == 3, "undo links them again");
    const auto drop = edit::removeScene(document, 50007, true);
    expect(drop && document.frameCount() == 2, "with its frames, they go too");
    drop->revert(document);
    expect(document.frameCount() == 5 && markersMatch(document, 50007), "and come back");
    bool missing = false;
    try {
        (void)edit::removeScene(document, 50008, true);
    } catch (const std::out_of_range&) {
        missing = true;
    }
    expect(missing, "a scene the project has not is refused");

    document::ColorizationSettings settings = *document.colorizationSettings();
    settings.scalingAlgorithm = document::kScalingLineDoubling;
    const auto scaling = edit::setColorizationSettings(document, settings);
    expect(scaling && !scaling->changesFrames() &&
               document.colorizationSettings()->scalingAlgorithm ==
                   document::kScalingLineDoubling,
           "the scaling settings change alone");
    expect(!edit::setColorizationSettings(document, settings), "and not twice");
    (void)added;
}

void testSceneIds()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    expect(edit::nextFreeSceneId(document) == document::kFirstSceneId,
           "the first free ID is the first of the range");
    expect(!edit::sceneProblem(document, makeScene(49999, 1, 1)).empty() &&
               !edit::sceneProblem(document, makeScene(60000, 1, 1)).empty() &&
               edit::sceneProblem(document, makeScene(59999, 1, 1)).empty(),
           "a new scene's ID is one of 50000 to 59999");
    auto first = edit::addScene(document, makeScene(50000, 1, 1));
    auto third = edit::addScene(document, makeScene(50002, 1, 1));
    expect(edit::nextFreeSceneId(document) == 50001, "the next free ID fills a gap");
    // A scene of an imported pup.csv keeps its ID, and gets no other outside the range.
    document::ColorizationSettings settings = *document.colorizationSettings();
    settings.scenes.push_back(makeScene(1001, 1, 1));
    document.setColorizationSettings(settings);
    expect(edit::sceneProblem(document, makeScene(1001, 2, 1), 1001).empty() &&
               !edit::sceneProblem(document, makeScene(1002, 1, 1), 1001).empty() &&
               edit::sceneProblem(document, makeScene(50003, 1, 1), 1001).empty(),
           "an imported scene keeps its ID or takes one of the range");
    (void)first;
    (void)third;
}

void testOtherLayouts()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::NativeOriginal, 2));
    expect(!edit::sceneProblem(document, makeScene(50001, 1, 1)).empty(),
           "a project with 256x64 ROM frames has no scenes");
}

}  // namespace

void runSceneEditTests()
{
    testAddAndUndo();
    testChange();
    testRemove();
    testSceneIds();
    testOtherLayouts();
}
