#include "edit/SpeakerLightEdits.h"

#include <algorithm>
#include <stdexcept>

namespace colorizing::edit {

FrameLightsCommand::FrameLightsCommand(std::size_t frameIndex,
                                       const document::LightActions& before,
                                       const document::LightActions& after)
    : m_frameIndex(frameIndex),
      m_before(before),
      m_after(after)
{
}

void FrameLightsCommand::apply(document::ProjectDocument& document) const
{
    document.setFrameLights(m_frameIndex, m_after);
}

void FrameLightsCommand::revert(document::ProjectDocument& document) const
{
    document.setFrameLights(m_frameIndex, m_before);
}

EditTarget FrameLightsCommand::target() const
{
    return EditTarget{EditKind::FrameAttributes, m_frameIndex, render::Resolution::Sd,
                      {m_frameIndex}};
}

std::vector<RecordReference> FrameLightsCommand::recordReferences() const
{
    return {{RecordKind::Frame, m_frameIndex}};
}

SpeakerLightsCommand::SpeakerLightsCommand(document::SpeakerLightSettings before,
                                           document::SpeakerLightSettings after,
                                           std::vector<FrameChange> frames)
    : m_before(std::move(before)),
      m_after(std::move(after)),
      m_frames(std::move(frames))
{
}

void SpeakerLightsCommand::apply(document::ProjectDocument& document) const
{
    // Frames first: they must not name an effect that goes.
    for (const FrameChange& frame : m_frames) {
        document.setFrameLights(frame.frameIndex, frame.after);
    }
    document.setSpeakerLights(m_after);
}

void SpeakerLightsCommand::revert(document::ProjectDocument& document) const
{
    // The effect first: the frames name it again.
    document.setSpeakerLights(m_before);
    for (const FrameChange& frame : m_frames) {
        document.setFrameLights(frame.frameIndex, frame.before);
    }
}

EditTarget SpeakerLightsCommand::target() const
{
    EditTarget target{EditKind::ProjectStructure, 0, render::Resolution::Sd, {}};
    for (const FrameChange& frame : m_frames) {
        target.frameIndices.push_back(frame.frameIndex);
    }
    return target;
}

std::vector<RecordReference> SpeakerLightsCommand::recordReferences() const
{
    std::vector<RecordReference> references;
    for (const FrameChange& frame : m_frames) {
        references.push_back({RecordKind::Frame, frame.frameIndex});
    }
    return references;
}

std::unique_ptr<SpeakerLightsCommand> setSpeakerLights(document::ProjectDocument& document,
                                                       document::SpeakerLightSettings settings)
{
    if (settings == document.speakerLights()) {
        return nullptr;
    }
    auto command = std::make_unique<SpeakerLightsCommand>(
        document.speakerLights(), std::move(settings),
        std::vector<SpeakerLightsCommand::FrameChange>{});
    command->apply(document);
    return command;
}

std::unique_ptr<SpeakerLightsCommand> removeLightEffect(document::ProjectDocument& document,
                                                        uint16_t effectId)
{
    document::SpeakerLightSettings settings = document.speakerLights();
    const auto effect =
        std::find_if(settings.effects.begin(), settings.effects.end(),
                     [effectId](const document::LightEffect& e) { return e.id == effectId; });
    if (effect == settings.effects.end()) {
        throw std::out_of_range("the project has no light effect " + std::to_string(effectId));
    }
    settings.effects.erase(effect);
    const auto forget = [effectId](document::LightActions& actions) {
        for (document::LightAction& action : actions) {
            if (action.kind == document::LightActionKind::Effect &&
                action.effectId == effectId) {
                action = document::LightAction{};
            }
        }
    };
    forget(settings.defaults);
    std::vector<SpeakerLightsCommand::FrameChange> frames;
    for (std::size_t frameIndex : document.framesUsingLightEffect(effectId)) {
        SpeakerLightsCommand::FrameChange change;
        change.frameIndex = frameIndex;
        change.before = document.frameLights(frameIndex);
        change.after = change.before;
        forget(change.after);
        frames.push_back(change);
    }
    auto command = std::make_unique<SpeakerLightsCommand>(document.speakerLights(),
                                                          std::move(settings), std::move(frames));
    command->apply(document);
    return command;
}

document::LightActions suggestedAmbientLighting(const document::Rgb565Frame& frame)
{
    document::LightActions actions{};
    const int width = frame.size.width;
    const int height = frame.size.height;
    if (width <= 0 || height <= 0 || !frame.isValid()) {
        return actions;
    }
    const int part = std::max(1, width / 4);
    const auto average = [&](int firstColumn) {
        uint64_t red = 0;
        uint64_t green = 0;
        uint64_t blue = 0;
        uint64_t lit = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = firstColumn; x < firstColumn + part; ++x) {
                const uint16_t pixel =
                    frame.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)];
                if (pixel == 0) {
                    continue;
                }
                // RGB565 to 8 bits per channel.
                red += ((pixel >> 11) & 0x1f) * 255 / 31;
                green += ((pixel >> 5) & 0x3f) * 255 / 63;
                blue += (pixel & 0x1f) * 255 / 31;
                ++lit;
            }
        }
        document::LightAction action;
        if (lit == 0) {
            action.kind = document::LightActionKind::Off;
            return action;
        }
        action.kind = document::LightActionKind::Color;
        action.color = static_cast<uint32_t>(((red / lit) << 16) | ((green / lit) << 8) |
                                             (blue / lit));
        return action;
    };
    actions[0] = average(0);
    actions[1] = average(width - part);
    return actions;
}

}  // namespace colorizing::edit
