// Frame -> Copy Frame Settings: the chosen parts of one frame replace those of others.

#include "edit/FrameSettingsCopy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"

namespace {

using namespace colorizing;
using document::ProjectDocument;
using edit::FrameSettingsPart;
using render::Resolution;
namespace fixtures = runtime_fixtures;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: copy frame settings: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::size_t kSource = 0;
constexpr std::size_t kTarget = 1;
constexpr std::size_t kScene = 2;
constexpr std::size_t kUntouched = 3;

// Frame 0 has something of every part; frame 1 has others of its own; frame 2 is a frame of a
// scene; frame 3 is not a target.
ProjectDocument makeDocument()
{
    document::ProjectSnapshot snapshot =
        fixtures::makeProject(render::PlaneLayout::Ordinary, 4);
    snapshot.comparisonMasks.assign(
        2, document::makeIndexedFrame(snapshot.geometry.romFrameSize(), 0));
    fixtures::addBackground(snapshot);
    const std::size_t background = fixtures::addBackground(snapshot);
    fixtures::addSprite(snapshot);
    fixtures::addSprite(snapshot);
    for (std::size_t frame = 0; frame < 4; ++frame) {
        fixtures::addDynamicData(snapshot, frame);
        fixtures::assignBackground(snapshot, frame, document::kNoBackgroundId);
        snapshot.frames[frame].spriteReferences.assign(document::kSpritesPerFrame,
                                                       document::FrameSpriteReference{});
    }
    document::ProjectFrameSnapshot& source = snapshot.frames[kSource];
    source.dynamicData->maskSd.pixels[5] = 3;
    source.dynamicData->colorsSd[3 * fixtures::kColorCount + 1] = 0x1234;
    source.dynamicData->shadowDirectionsSd[3] = 0x04;
    source.dynamicData->shadowColorsSd[3] = 0x5678;
    source.backgroundData->backgroundId = static_cast<uint16_t>(background);
    // The fixture's masks let the background through everywhere.
    source.backgroundData->maskSd.pixels[7] = 0;
    source.spriteReferences[0] = {1, 2, 3, 40, 20};
    source.comparisonMaskId = 1;
    source.shapeComparisonMode = 1;
    source.triggerId = 777;
    source.durationMs = 55;
    source.dofEvents = {{'S', 5, 255, 0}};
    document::ProjectFrameSnapshot& target = snapshot.frames[kTarget];
    target.dynamicData->maskSd.pixels[9] = 1;
    target.spriteReferences[0] = {0, 0, 0, 10, 10};
    target.spriteReferences[1] = {1, 0, 0, 10, 10};
    snapshot.frames[kScene].scene = document::SceneFrameRef{51300, 1, 0};
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.setColorRotationLength(kSource, Resolution::Sd, 0, 2);
    document.setColorRotationDelayMs(kSource, Resolution::Sd, 0, 100);
    return document;
}

void testPreviewCounts()
{
    const ProjectDocument document = makeDocument();
    const std::vector<std::size_t> targets{kSource, kTarget, kScene};
    const auto changed = [&](FrameSettingsPart part) {
        return edit::framesChangedBy(document, kSource, targets, part);
    };
    expect(changed(FrameSettingsPart::DynamicZones) == 1 &&
               changed(FrameSettingsPart::Sprites) == 1 &&
               changed(FrameSettingsPart::Trigger) == 1 &&
               changed(FrameSettingsPart::Identification) == 1,
           "the source and the frame of a scene are not counted for game settings");
    expect(changed(FrameSettingsPart::DofEvents) == 2,
           "both other frames take the DOF events, the frame of a scene too");
    expect(changed(FrameSettingsPart::Background) == 2 &&
               changed(FrameSettingsPart::BackgroundMask) == 2 &&
               changed(FrameSettingsPart::ColorRotations) == 2 &&
               changed(FrameSettingsPart::Duration) == 2,
           "a frame of a scene takes its background, mask, rotations and duration");
    expect(changed(FrameSettingsPart::Lights) == 0, "no frame has lights: nothing changes");
}

void testCopyReplacesAndUndoes()
{
    ProjectDocument document = makeDocument();
    const std::vector<FrameSettingsPart> all(std::begin(edit::kFrameSettingsParts),
                                             std::end(edit::kFrameSettingsParts));
    edit::FrameSettingsCopy copy =
        edit::copyFrameSettings(document, kSource, {kTarget, kScene}, all);
    expect(copy.command && copy.changedFrames == 2 && copy.sceneFramesLeftOut == 1,
           "both targets changed, the frame of a scene left out of game settings");

    const document::FrameDynamicData& dynamic = *document.dynamicData(kTarget);
    expect(dynamic.maskSd.pixels[5] == 3 && dynamic.maskSd.pixels[9] == 255,
           "the dynamic zones are replaced, not merged");
    expect(dynamic.colorsSd[3 * fixtures::kColorCount + 1] == 0x1234 &&
               dynamic.shadowDirectionsSd[3] == 0x04 && dynamic.shadowColorsSd[3] == 0x5678,
           "set colors and shadows are copied");
    const auto& slots = document.frameSpriteReferences(kTarget);
    expect(slots[0].spriteId == 1 && slots[0].maxX == 40 && slots[0].maxY == 20 &&
               slots[1].spriteId == document::kNoSpriteId,
           "the sprite slots are replaced");
    expect(document.backgroundId(kTarget) == document.backgroundId(kSource) &&
               document.backgroundData(kTarget)->maskSd.pixels[7] == 0,
           "background and its mask are copied");
    expect(document.colorRotations(kTarget, Resolution::Sd) ==
                   document.colorRotations(kSource, Resolution::Sd) &&
               document.comparisonMaskId(kTarget) == 1 &&
               document.shapeComparisonMode(kTarget) == 1 &&
               document.triggerId(kTarget) == 777 && document.frameDurationMs(kTarget) == 55,
           "rotations, identification, trigger and duration are copied");

    expect(document.frameDofEvents(kTarget) == document.frameDofEvents(kSource) &&
               document.frameDofEvents(kScene) == document.frameDofEvents(kSource),
           "the DOF events are copied, also to the frame of a scene");
    expect(document.backgroundId(kScene) == document.backgroundId(kSource) &&
               document.frameDurationMs(kScene) == 55 &&
               document.triggerId(kScene) == document::kNoTriggerId &&
               document.comparisonMaskId(kScene) == document::kNoComparisonMaskId &&
               !document.hasDynamicPixels(kScene),
           "the frame of a scene takes only what it can have");
    expect(document.frameDurationMs(kUntouched) != 55, "a frame that is no target stays");

    copy.command->revert(document);
    expect(document.dynamicData(kTarget)->maskSd.pixels[9] == 1 &&
               document.dynamicData(kTarget)->maskSd.pixels[5] == 255 &&
               document.frameSpriteReferences(kTarget)[1].spriteId == 1 &&
               document.triggerId(kTarget) != 777 &&
               document.backgroundId(kScene) == document::kNoBackgroundId,
           "one undo step puts every target back");
}

void testOnlyChosenParts()
{
    ProjectDocument document = makeDocument();
    const edit::FrameSettingsCopy copy = edit::copyFrameSettings(
        document, kSource, {kTarget}, {FrameSettingsPart::DynamicColors});
    expect(copy.command &&
               document.dynamicData(kTarget)->colorsSd[3 * fixtures::kColorCount + 1] ==
                   0x1234 &&
               document.dynamicData(kTarget)->maskSd.pixels[9] == 1 &&
               document.triggerId(kTarget) != 777,
           "only the chosen part is copied");
    expect(!edit::copyFrameSettings(document, kSource, {kTarget},
                                    {FrameSettingsPart::DynamicColors})
                .command,
           "nothing to change: no step");
}

}  // namespace

void runFrameSettingsCopyTests()
{
    testPreviewCounts();
    testCopyReplacesAndUndoes();
    testOnlyChosenParts();
}
