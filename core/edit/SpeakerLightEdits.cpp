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

std::vector<std::size_t> framesWithoutLights(const document::ProjectDocument& document)
{
    std::vector<std::size_t> frames;
    for (std::size_t frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
        if (document::changesNothing(document.frameLights(frameIndex))) {
            frames.push_back(frameIndex);
        }
    }
    return frames;
}

std::unique_ptr<SpeakerLightsCommand> addAmbientLightingToFramesWithoutLights(
    document::ProjectDocument& document,
    const std::function<document::Rgb565Frame(std::size_t frameIndex)>& colorsOf)
{
    std::vector<SpeakerLightsCommand::FrameChange> frames;
    for (std::size_t frameIndex : framesWithoutLights(document)) {
        SpeakerLightsCommand::FrameChange change;
        change.frameIndex = frameIndex;
        change.before = document.frameLights(frameIndex);
        change.after = change.before;
        const document::LightActions suggested = suggestedAmbientLighting(colorsOf(frameIndex));
        change.after[0] = suggested[0];
        change.after[1] = suggested[1];
        frames.push_back(change);
    }
    if (frames.empty()) {
        return nullptr;
    }
    auto command = std::make_unique<SpeakerLightsCommand>(
        document.speakerLights(), document.speakerLights(), std::move(frames));
    command->apply(document);
    return command;
}

bool hasAutomaticLights(const document::LightActions& actions)
{
    for (const document::LightAction& action : actions) {
        if (action.automatic) {
            return true;
        }
    }
    return false;
}

document::LightActions updatedAmbientLighting(const document::LightActions& actions,
                                              const document::LightActions& suggested)
{
    document::LightActions updated = actions;
    for (std::size_t channel = 0; channel < updated.size(); ++channel) {
        if (updated[channel].automatic &&
            suggested[channel].kind != document::LightActionKind::Unchanged) {
            updated[channel] = suggested[channel];
        }
    }
    return updated;
}

std::unique_ptr<SpeakerLightsCommand> updateAmbientLighting(
    document::ProjectDocument& document,
    const std::function<document::Rgb565Frame(std::size_t frameIndex)>& colorsOf)
{
    std::vector<SpeakerLightsCommand::FrameChange> frames;
    for (std::size_t frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
        const document::LightActions& lights = document.frameLights(frameIndex);
        if (!hasAutomaticLights(lights)) {
            continue;
        }
        const document::LightActions updated =
            updatedAmbientLighting(lights, suggestedAmbientLighting(colorsOf(frameIndex)));
        if (updated != lights) {
            frames.push_back({frameIndex, lights, updated});
        }
    }
    if (frames.empty()) {
        return nullptr;
    }
    auto command = std::make_unique<SpeakerLightsCommand>(
        document.speakerLights(), document.speakerLights(), std::move(frames));
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
    const auto pixelAt = [&](int x, int y) {
        return frame.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)];
    };
    const auto columnLit = [&](int x) {
        for (int y = 0; y < height; ++y) {
            if (pixelAt(x, y) != 0) {
                return true;
            }
        }
        return false;
    };
    // The lit content nearest to each edge: a black margin (a Stern ROM's empty score
    // block of a single player game) is skipped.
    int firstLit = 0;
    while (firstLit < width && !columnLit(firstLit)) {
        ++firstLit;
    }
    if (firstLit == width) {
        for (std::size_t channel : {std::size_t{0}, std::size_t{1}}) {
            actions[channel].kind = document::LightActionKind::Off;
            actions[channel].automatic = true;
        }
        return actions;
    }
    int lastLit = width - 1;
    while (!columnLit(lastLit)) {
        --lastLit;
    }
    const int part = std::max(1, width / 4);
    const auto average = [&](int firstColumn) {
        firstColumn = std::clamp(firstColumn, 0, width - part);
        uint64_t red = 0;
        uint64_t green = 0;
        uint64_t blue = 0;
        uint64_t lit = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = firstColumn; x < firstColumn + part; ++x) {
                const uint16_t pixel = pixelAt(x, y);
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
        action.kind = document::LightActionKind::Color;
        action.color = static_cast<uint32_t>(((red / lit) << 16) | ((green / lit) << 8) |
                                             (blue / lit));
        action.automatic = true;
        return action;
    };
    actions[0] = average(firstLit);
    actions[1] = average(lastLit + 1 - part);
    return actions;
}

}  // namespace colorizing::edit
