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
    const auto command = edit::addScene(document, makeScene(500, 2, 2));
    expect(command && command->changesFrames(), "adding a scene adds frames");
    expect(document.frameCount() == 7, "one frame per frame of each group, behind the others");
    const std::vector<std::size_t> frames = edit::sceneFrames(document, 500);
    expect(frames == std::vector<std::size_t>{3, 4, 5, 6} &&
               document.sceneFrame(3) == document::SceneFrameRef{500, 1, 0} &&
               document.sceneFrame(6) == document::SceneFrameRef{500, 2, 1} &&
               document.frameDurationMs(4) == 30,
           "in group and frame order, lasting the scene's frame duration");
    expect(markersMatch(document, 500), "with the scene markers as originals");
    expect(document.colorizationSettings() &&
               document.colorizationSettings()->scenes.size() == 1,
           "the project has the scene");
    command->revert(document);
    expect(document.frameCount() == 3 && !document.colorizationSettings(),
           "undo takes both back");
    command->apply(document);
    expect(document.frameCount() == 7 && markersMatch(document, 500), "redo puts them back");

    bool refused = false;
    try {
        (void)edit::addScene(document, makeScene(500, 1, 1));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && document.frameCount() == 7, "a second scene 500 is refused");
    expect(!edit::sceneProblem(document, makeScene(501, 0, 1)).empty() &&
               !edit::sceneProblem(document, makeScene(501, 1, 0)).empty() &&
               edit::sceneProblem(document, makeScene(501, 1, 1)).empty() &&
               edit::sceneProblem(document, makeScene(500, 1, 1), 500).empty(),
           "a scene needs a frame, a group and its own ID");
}

void testChange()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    auto added = edit::addScene(document, makeScene(10, 2, 1));
    auto other = edit::addScene(document, makeScene(20, 1, 1));
    expect(document.frameCount() == 5, "two scenes added");

    // More frames: behind the scene's last frame, before the next scene.
    document::SceneDefinition longer = makeScene(10, 4, 1);
    longer.durationMs = 70;
    const auto grow = edit::changeScene(document, 10, longer);
    expect(grow && document.frameCount() == 7 &&
               edit::sceneFrames(document, 10) == std::vector<std::size_t>{2, 3, 4, 5} &&
               document.sceneFrame(5) == document::SceneFrameRef{10, 1, 3} &&
               edit::sceneFrames(document, 20) == std::vector<std::size_t>{6},
           "frames a scene gains go behind its last frame");
    expect(document.frameDurationMs(2) == 70 && document.frameDurationMs(5) == 70,
           "and all of them get the new duration");
    expect(markersMatch(document, 10), "with their markers");

    // Fewer frames, another ID: frames go, the others get the new markers.
    const document::IndexedFrame before = document.originalFrame(2);
    const auto shrink = edit::changeScene(document, 10, makeScene(11, 1, 1));
    expect(shrink && document.frameCount() == 4 &&
               edit::sceneFrames(document, 11) == std::vector<std::size_t>{2} &&
               edit::sceneFrames(document, 10).empty() && markersMatch(document, 11) &&
               !sameFrame(document.originalFrame(2), before),
           "frames a scene loses are deleted, the others follow its new ID");
    shrink->revert(document);
    expect(document.frameCount() == 7 && markersMatch(document, 10) &&
               sameFrame(document.originalFrame(2), before),
           "undo puts the frames and their originals back");
    grow->revert(document);
    expect(document.frameCount() == 5 && edit::sceneFrames(document, 10).size() == 2,
           "and the frames it gained go again");

    // A frame deleted from the strip comes back when the scene is applied again.
    auto deleted = edit::deleteFrames(document, std::vector<std::size_t>{3});
    expect(document.frameCount() == 4, "a scene frame is deleted");
    const auto again = edit::changeScene(document, 10, makeScene(10, 2, 1));
    expect(again && document.frameCount() == 5 &&
               document.sceneFrame(3) == document::SceneFrameRef{10, 1, 1},
           "and made again, in its place behind the scene's frames");
    expect(!edit::changeScene(document, 10, makeScene(10, 2, 1)),
           "an unchanged scene with all its frames changes nothing");
    (void)added;
    (void)other;
    (void)deleted;
}

void testRemove()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    auto added = edit::addScene(document, makeScene(7, 3, 1));
    const auto keep = edit::removeScene(document, 7, false);
    expect(keep && document.frameCount() == 5 && edit::sceneFrames(document, 7).empty() &&
               document.colorizationSettings()->scenes.empty(),
           "a scene removed without its frames leaves them as ordinary frames");
    keep->revert(document);
    expect(edit::sceneFrames(document, 7).size() == 3, "undo links them again");
    const auto drop = edit::removeScene(document, 7, true);
    expect(drop && document.frameCount() == 2, "with its frames, they go too");
    drop->revert(document);
    expect(document.frameCount() == 5 && markersMatch(document, 7), "and come back");
    bool missing = false;
    try {
        (void)edit::removeScene(document, 8, true);
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

void testOtherLayouts()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::NativeOriginal, 2));
    expect(!edit::sceneProblem(document, makeScene(1, 1, 1)).empty(),
           "a project with 256x64 ROM frames has no scenes");
}

}  // namespace

void runSceneEditTests()
{
    testAddAndUndo();
    testChange();
    testRemove();
    testOtherLayouts();
}
