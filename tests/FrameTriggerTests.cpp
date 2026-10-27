// A frame's three triggers: the PUP trigger, the scene it starts and the monochrome trigger.

#include "document/Triggers.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/SceneEdits.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"
#include "project/ProjectFile.h"
#include "runtime/RuntimePreview.h"

namespace {

using namespace colorizing;
using document::kNoTriggerId;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frame triggers: " << message << '\n';
        std::exit(1);
    }
}

document::SceneDefinition sceneOf(uint16_t id)
{
    document::SceneDefinition scene;
    scene.sceneId = id;
    scene.frameCount = 2;
    scene.durationMs = 30;
    scene.interruptable = true;
    scene.immediateStart = true;
    return scene;
}

// One ID per frame, as the Win32 editor keeps it, split by range as libserum splits it.
void testSplit()
{
    document::ColorizationSettings settings;
    settings.scenes = {sceneOf(7)};
    const auto split = [&settings](uint32_t id, bool withScenes) {
        document::ProjectFrameSnapshot frame;
        document::setTriggersFromId(frame, id,
                                    withScenes ? std::optional(settings) : std::nullopt);
        return std::vector<uint32_t>{frame.pupTriggerId, frame.sceneTriggerId,
                                     frame.monochromeTriggerId};
    };
    using Ids = std::vector<uint32_t>;
    expect(split(kNoTriggerId, true) == Ids{kNoTriggerId, kNoTriggerId, kNoTriggerId},
           "no ID, no trigger");
    expect(split(42, true) == Ids{42, kNoTriggerId, kNoTriggerId}, "a PUP trigger");
    expect(split(7, true) == Ids{7, 7, kNoTriggerId},
           "a PUP trigger that is the ID of a scene starts it too");
    expect(split(7, false) == Ids{7, kNoTriggerId, kNoTriggerId}, "without that scene it does not");
    expect(split(50010, true) == Ids{kNoTriggerId, 50010, kNoTriggerId}, "a scene");
    expect(split(62000, true) == Ids{kNoTriggerId, 62000, kNoTriggerId},
           "an ID of no kind below the monochrome ones stays where libserum keeps it");
    expect(split(65432, true) == Ids{kNoTriggerId, kNoTriggerId, 65432} &&
               split(65431, true) == Ids{kNoTriggerId, kNoTriggerId, 65431},
           "the monochrome triggers");
    expect(document::combinedTriggerId(42, 50010, 65432) == 50010 &&
               document::combinedTriggerId(42, kNoTriggerId, 65432) == 42 &&
               document::combinedTriggerId(kNoTriggerId, kNoTriggerId, 65432) == 65432,
           "one ID where only one is kept: the scene, else the PUP trigger, else monochrome");

    // Scenes that come later (a pup.csv beside the project) link frames whose PUP trigger
    // is their ID.
    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2);
    snapshot.frames[0].pupTriggerId = 7;
    snapshot.frames[1].pupTriggerId = 8;
    snapshot.colorizationSettings = settings;
    expect(document::linkPupTriggerScenes(snapshot) == 1 &&
               snapshot.frames[0].sceneTriggerId == 7 &&
               snapshot.frames[1].sceneTriggerId == kNoTriggerId,
           "the frame with the scene's ID starts it");
}

void testProjectFileAndSettingsBlock()
{
    document::ProjectSnapshot fixture =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2);
    fixture.frames[0].pupTriggerId = 42;
    fixture.frames[0].sceneTriggerId = 50010;
    fixture.frames[0].monochromeTriggerId = 65431;
    // A value libserum does not take is kept in the project, left out for libserum.
    fixture.frames[1].monochromeTriggerId = 70000;
    const document::ProjectSnapshot snapshot =
        document::ProjectDocument::fromSnapshot(fixture).toSnapshot();

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("colorizing_triggers_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cdmd");
    const auto saved = project::saveProjectFile(platform::utf8FromPath(path), snapshot, {"test"});
    expect(saved.ok, "the project is saved: " + saved.error.message);
    const legacy::LegacyLoadResult loaded =
        project::loadProjectFile(platform::utf8FromPath(path));
    std::filesystem::remove(path);
    expect(loaded.ok && loaded.snapshot.frames[0].pupTriggerId == 42 &&
               loaded.snapshot.frames[0].sceneTriggerId == 50010 &&
               loaded.snapshot.frames[0].monochromeTriggerId == 65431 &&
               loaded.snapshot.frames[1].monochromeTriggerId == 70000,
           "the project file keeps all three");

    const std::vector<uint8_t> block = legacy::cromSettingsBlock(snapshot);
    const auto u32At = [&block](std::size_t offset) {
        return static_cast<uint32_t>(block[offset]) |
               (static_cast<uint32_t>(block[offset + 1]) << 8) |
               (static_cast<uint32_t>(block[offset + 2]) << 16) |
               (static_cast<uint32_t>(block[offset + 3]) << 24);
    };
    // magic, size, contents, then the frame triggers: count and frame 0 only.
    expect(block.size() == 12 + 4 + 16 && u32At(8) == legacy::kCromSettingsFrameTriggers &&
               u32At(12) == 1 && u32At(16) == 0 && u32At(20) == 42 && u32At(24) == 50010 &&
               u32At(28) == 65431,
           "libserum gets the three triggers of the frames that have one it takes");
}

// libserum reports the frame's PUP trigger and starts its scene at once.
void testLibserumFiresAllThree()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    const auto added = edit::addScene(document, sceneOf(50610));
    const std::vector<std::size_t> sceneFrames = edit::sceneFrames(document, 50610);
    document.setPupTriggerId(0, 42);
    document.setSceneTriggerId(0, 50610);
    document.setPupTriggerId(1, 43);
    runtime::RuntimePreview& preview = runtime::RuntimePreview::instance();
    std::string error;
    expect(preview.openProject(document, runtime::PlaneRequest::Both, &error),
           "the project loads: " + error);
    const runtime::RuntimeFrame started = preview.colorize(document.originalFrame(0));
    expect(started.triggerId == 42, "the PUP trigger is reported");
    expect(started.shownFrameIndex && !sceneFrames.empty() &&
               *started.shownFrameIndex == sceneFrames.front(),
           "and the scene starts with it");
    const runtime::RuntimeFrame next = preview.colorize(document.originalFrame(1));
    expect(next.triggerId == 43 && next.shownFrameIndex == std::optional<std::size_t>(1),
           "the next frame interrupts the scene and reports its own");
    preview.close();
}

}  // namespace

void runFrameTriggerTests()
{
    testSplit();
    testProjectFileAndSettingsBlock();
    testLibserumFiresAllThree();
}
