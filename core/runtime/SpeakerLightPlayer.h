#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/SpeakerLights.h"

namespace colorizing::runtime {

// The WS2812FX modes (the library the DMD's firmware drives the speaker lights with), by
// FX_MODE_* value: their names, as the library has them ("Rainbow Cycle").
[[nodiscard]] std::size_t lightModeCount();
[[nodiscard]] std::string lightModeName(uint8_t mode);

// The effect an action starts: the effect it names, a static color, or off (static black).
// Nothing for an unchanged channel or an effect the settings do not have.
[[nodiscard]] std::optional<document::LightEffect> lightEffectForAction(
    const document::SpeakerLightSettings& settings,
    const document::LightAction& action);

// What each channel plays while the frame shows in the editor: per channel the action of the
// last frame up to it in the strip that changes the channel, the default before that.
[[nodiscard]] document::LightActions lightsShownAt(const document::ProjectDocument& document,
                                                   std::size_t frameIndex);

// One speaker light channel as the DMD's firmware drives it, for the preview: the
// WS2812FX effect code itself on a strip of ledCount LEDs. play() starts an effect; after its
// duration the channel returns to its default. Times are in ms of any steady clock.
class SpeakerLightPlayer {
public:
    explicit SpeakerLightPlayer(std::size_t ledCount = 16);
    ~SpeakerLightPlayer();
    SpeakerLightPlayer(const SpeakerLightPlayer&) = delete;
    SpeakerLightPlayer& operator=(const SpeakerLightPlayer&) = delete;

    void setLedCount(std::size_t ledCount);
    [[nodiscard]] std::size_t ledCount() const;
    // What the channel returns to after an effect with a duration (off without one).
    void setDefault(const std::optional<document::LightEffect>& effect);
    // Starts the effect from its beginning.
    void play(const document::LightEffect& effect, uint32_t nowMs);
    // Plays the default.
    void playDefault(uint32_t nowMs);
    // Runs the effect up to nowMs.
    void update(uint32_t nowMs);
    // The effect playing, nothing while off.
    [[nodiscard]] const std::optional<document::LightEffect>& playing() const;
    // The LEDs as the strip shows them: 0x00RRGGBB, the white of an RGBW strip mixed in.
    [[nodiscard]] std::vector<uint32_t> colors() const;

private:
    void start(const std::optional<document::LightEffect>& effect, uint32_t nowMs);

    struct Strip;
    std::unique_ptr<Strip> m_strip;
    std::optional<document::LightEffect> m_default;
    std::optional<document::LightEffect> m_playing;
    uint32_t m_startMs = 0;
};

}  // namespace colorizing::runtime
