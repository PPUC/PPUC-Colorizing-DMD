#pragma once

#include <cstdint>

#include "document/ProjectDocument.h"
#include "document/Scene.h"

namespace colorizing::document {

// What a frame's trigger ID asks of the runtime, by its range. A frame has at most one.
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

}  // namespace colorizing::document
