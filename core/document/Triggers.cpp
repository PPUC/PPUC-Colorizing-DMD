#include "document/Triggers.h"

namespace colorizing::document {

namespace {

bool hasScene(const std::optional<ColorizationSettings>& settings, uint32_t triggerId)
{
    return settings && triggerId <= 0xffff &&
           settings->scene(static_cast<uint16_t>(triggerId)) != nullptr;
}

}  // namespace

void setTriggersFromId(ProjectFrameSnapshot& frame,
                       uint32_t triggerId,
                       const std::optional<ColorizationSettings>& settings)
{
    frame.pupTriggerId = kNoTriggerId;
    frame.sceneTriggerId = kNoTriggerId;
    frame.monochromeTriggerId = kNoTriggerId;
    if (triggerId == kNoTriggerId) {
        return;
    }
    if (triggerId >= kPaletteMonochromeTriggerId) {
        frame.monochromeTriggerId = triggerId;
    } else if (triggerId <= kLastPupTriggerId) {
        frame.pupTriggerId = triggerId;
        if (hasScene(settings, triggerId)) {
            frame.sceneTriggerId = triggerId;
        }
    } else {
        frame.sceneTriggerId = triggerId;
    }
}

std::size_t linkPupTriggerScenes(ProjectSnapshot& snapshot)
{
    std::size_t linked = 0;
    for (ProjectFrameSnapshot& frame : snapshot.frames) {
        if (frame.pupTriggerId != kNoTriggerId && frame.sceneTriggerId == kNoTriggerId &&
            !frame.scene && hasScene(snapshot.colorizationSettings, frame.pupTriggerId)) {
            frame.sceneTriggerId = frame.pupTriggerId;
            ++linked;
        }
    }
    return linked;
}

std::size_t linkPupTriggerScenes(ProjectDocument& document)
{
    std::size_t linked = 0;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        const uint32_t pup = document.pupTriggerId(frame);
        if (pup != kNoTriggerId && document.sceneTriggerId(frame) == kNoTriggerId &&
            !document.sceneFrame(frame) && hasScene(document.colorizationSettings(), pup)) {
            document.setSceneTriggerId(frame, pup);
            ++linked;
        }
    }
    return linked;
}

}  // namespace colorizing::document
