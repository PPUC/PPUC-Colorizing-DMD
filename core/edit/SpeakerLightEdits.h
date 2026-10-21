#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/SpeakerLights.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// What one frame does to the speaker light channels (ProjectDocument::frameLights()), in the
// frame's settings history.
class FrameLightsCommand final : public EditCommand {
public:
    FrameLightsCommand(std::size_t frameIndex,
                       const document::LightActions& before,
                       const document::LightActions& after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_frameIndex;
    document::LightActions m_before;
    document::LightActions m_after;
};

// The project's light effects and defaults (ProjectDocument::speakerLights()), with the
// frames that lose an effect that goes. One step in the structure history.
class SpeakerLightsCommand final : public EditCommand {
public:
    struct FrameChange {
        std::size_t frameIndex = 0;
        document::LightActions before{};
        document::LightActions after{};
    };

    SpeakerLightsCommand(document::SpeakerLightSettings before,
                         document::SpeakerLightSettings after,
                         std::vector<FrameChange> frames);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    document::SpeakerLightSettings m_before;
    document::SpeakerLightSettings m_after;
    std::vector<FrameChange> m_frames;
};

// Each runs the change on the document and returns the command, already applied; nullptr when
// nothing changes. std::invalid_argument when the settings are not valid (see
// ProjectDocument::setSpeakerLights()), the document left as it was.
//
// The effects and defaults as they are: adding or changing an effect, the defaults.
[[nodiscard]] std::unique_ptr<SpeakerLightsCommand> setSpeakerLights(
    document::ProjectDocument& document,
    document::SpeakerLightSettings settings);
// Removes the effect; the defaults and the frames that start it leave those channels
// unchanged instead. std::out_of_range for an effect the project does not have.
[[nodiscard]] std::unique_ptr<SpeakerLightsCommand> removeLightEffect(
    document::ProjectDocument& document,
    uint16_t effectId);

// Ambient lighting for every frame that does nothing to any channel yet (a project from before
// speaker lights): channels 1 and 2 as suggestedAmbientLighting() says for the frame's picture,
// colorsOf(frameIndex). One step; nullptr when no frame is without lights.
[[nodiscard]] std::unique_ptr<SpeakerLightsCommand> addAmbientLightingToFramesWithoutLights(
    document::ProjectDocument& document,
    const std::function<document::Rgb565Frame(std::size_t frameIndex)>& colorsOf);
// The frames addAmbientLightingToFramesWithoutLights() would give lights.
[[nodiscard]] std::vector<std::size_t> framesWithoutLights(
    const document::ProjectDocument& document);

// The ambient lighting suggestion for a colorized frame: channel 1 (left) the color of the frame's
// left part, channel 2 (right) of its right part, each the average of the lit pixels of the
// outer quarter of the frame (off when there are none); channels 3 and 4 unchanged.
[[nodiscard]] document::LightActions suggestedAmbientLighting(const document::Rgb565Frame& frame);

}  // namespace colorizing::edit
