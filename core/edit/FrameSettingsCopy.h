#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditBatch.h"

namespace colorizing::edit {

// Frame -> Copy Frame Settings: the settings of one frame put on others, each chosen part
// replacing the target's, not merged into it. The pictures (colorized pixels) are not part
// of it; Copy and Paste is for those.
enum class FrameSettingsPart {
    // On the plane the dynamic zones live on (ProjectDocument::dynamicPlane()): the mask, the
    // colors of every set (with which sets color the SD picture only), every set's shadow.
    DynamicZones,
    DynamicColors,
    DynamicShadows,
    // The assigned background, and the background masks.
    Background,
    BackgroundMask,
    // Every sprite slot with its bounds.
    Sprites,
    ColorRotations,
    // The comparison mask and the shape mode.
    Identification,
    Trigger,
    Lights,
    Duration,
};

inline constexpr FrameSettingsPart kFrameSettingsParts[] = {
    FrameSettingsPart::DynamicZones,   FrameSettingsPart::DynamicColors,
    FrameSettingsPart::DynamicShadows, FrameSettingsPart::Background,
    FrameSettingsPart::BackgroundMask, FrameSettingsPart::Sprites,
    FrameSettingsPart::ColorRotations, FrameSettingsPart::Identification,
    FrameSettingsPart::Trigger,        FrameSettingsPart::Lights,
    FrameSettingsPart::Duration,
};

// Whether a frame of a scene takes the part (ProjectDocument::stripSceneFrame()): the
// background, its mask, color rotations, lights and the duration.
[[nodiscard]] bool sceneFrameTakes(FrameSettingsPart part);

// The targets (the source itself left out) the part would change. A frame of a scene does not
// take what it cannot have, and a frame without the data a part needs (dynamic or background
// data) is left alone. HD data goes only between frames that both have HD.
[[nodiscard]] std::size_t framesChangedBy(const document::ProjectDocument& document,
                                          std::size_t source,
                                          const std::vector<std::size_t>& targets,
                                          FrameSettingsPart part);

struct FrameSettingsCopy {
    // The copy as one step, applied already; nullptr when nothing changed.
    std::unique_ptr<EditBatchCommand> command;
    // Targets that changed, and targets that are frames of scenes and were left out of a part
    // they cannot have.
    std::size_t changedFrames = 0;
    std::size_t sceneFramesLeftOut = 0;
};

// Throws std::out_of_range for a frame that does not exist; the document is then as it was.
[[nodiscard]] FrameSettingsCopy copyFrameSettings(document::ProjectDocument& document,
                                                  std::size_t source,
                                                  const std::vector<std::size_t>& targets,
                                                  const std::vector<FrameSettingsPart>& parts);

}  // namespace colorizing::edit
