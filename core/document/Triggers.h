#pragma once

#include <cstdint>

#include <optional>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "document/Scene.h"

namespace colorizing::document {

// What a trigger ID asks of the runtime, by its range. A frame has a trigger of each of the
// three kinds, PUP, scene and monochrome (FrameRecord::pupTriggerId and the others); the Win32
// editor, a cROM and a project file before 3.99.17 keep one ID per frame.
enum class TriggerKind {
    None,
    // A PUP trigger: 0 to kLastPupTriggerId.
    Pup,
    // Starts a scene: kFirstSceneId to kLastSceneId.
    Scene,
    // libserum shows the frames in monochrome from this frame on (kMonochromeTriggerId), or in
    // the colors of this frame's palette (kPaletteMonochromeTriggerId).
    Monochrome,
    PaletteMonochrome,
    // Any other value, kept as it is.
    Other,
};

constexpr uint32_t kLastPupTriggerId = kFirstSceneId - 1;
constexpr uint32_t kMonochromeTriggerId = 65432;
constexpr uint32_t kPaletteMonochromeTriggerId = 65431;

[[nodiscard]] constexpr TriggerKind triggerKind(uint32_t triggerId)
{
    if (triggerId == kNoTriggerId) {
        return TriggerKind::None;
    }
    if (triggerId <= kLastPupTriggerId) {
        return TriggerKind::Pup;
    }
    if (triggerId >= kFirstSceneId && triggerId <= kLastSceneId) {
        return TriggerKind::Scene;
    }
    if (triggerId == kMonochromeTriggerId) {
        return TriggerKind::Monochrome;
    }
    if (triggerId == kPaletteMonochromeTriggerId) {
        return TriggerKind::PaletteMonochrome;
    }
    return TriggerKind::Other;
}

// The one trigger ID of the Win32 editor's frame as the frame's three triggers, split by range
// as libserum splits it: an ID up to kLastPupTriggerId is the PUP trigger, and starts the scene
// of that ID too when settings has one (a pup.csv may give a scene such an ID); one of
// kPaletteMonochromeTriggerId and up the monochrome trigger; anything else the scene trigger.
void setTriggersFromId(ProjectFrameSnapshot& frame,
                       uint32_t triggerId,
                       const std::optional<ColorizationSettings>& settings);

// A frame whose PUP trigger is the ID of a scene of settings and which starts no scene starts
// that scene too, as libserum played it with one ID per frame; for scenes added to a project
// whose frames came with one ID each. Returns how many frames got their scene.
std::size_t linkPupTriggerScenes(ProjectSnapshot& snapshot);
std::size_t linkPupTriggerScenes(ProjectDocument& document);

// The one ID where only one is kept (the cROM's frame block, the project file for an editor
// before 3.99.17, the plugin API): the scene trigger, else the PUP trigger, else the monochrome
// trigger.
[[nodiscard]] constexpr uint32_t combinedTriggerId(uint32_t pupTriggerId,
                                                   uint32_t sceneTriggerId,
                                                   uint32_t monochromeTriggerId)
{
    return sceneTriggerId != kNoTriggerId ? sceneTriggerId
           : pupTriggerId != kNoTriggerId ? pupTriggerId
                                          : monochromeTriggerId;
}

}  // namespace colorizing::document
