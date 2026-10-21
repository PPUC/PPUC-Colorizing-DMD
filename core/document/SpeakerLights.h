#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace colorizing::document {

// Speaker lights: the WS2812FX LED strips of a DMD (around the speakers of the backbox),
// driven by the DMD's firmware. The colorization starts effects on them from its frames;
// libserum hands them to the host (Serum_GetLightEffects()), which sends them to the DMD.
// The number of LEDs of a strip is the hardware's (set up on the device), not the project's.
constexpr std::size_t kLightChannels = 4;

// WS2812FX's option bits (setSegment()'s options).
constexpr uint8_t kLightReverse = 0x80;
constexpr uint8_t kLightFadeMask = 0x70;
constexpr uint8_t kLightGamma = 0x08;
constexpr uint8_t kLightSizeMask = 0x06;
// WS2812FX's FX_MODE_STATIC: colors[0] on every LED.
constexpr uint8_t kLightModeStatic = 0;
constexpr uint8_t kLightModeRainbowCycle = 12;

// An effect for one channel, with the WS2812FX settings of a segment. The project's effects
// are named and shared: a frame starts one on a channel by its ID.
struct LightEffect {
    // Unique in the project, from 1 on.
    uint16_t id = 0;
    std::string name;
    // A WS2812FX FX_MODE_* value.
    uint8_t mode = kLightModeRainbowCycle;
    // kLight... option bits.
    uint8_t options = 0;
    // WS2812FX's speed: the length of one cycle in ms (lower is faster).
    uint16_t speed = 1000;
    // How long it plays before the channel returns to its default; 0 until the next effect.
    uint32_t durationMs = 0;
    // 0xWWRRGGBB: white is used by RGBW strips only.
    std::array<uint32_t, 3> colors{0x00ff0000u, 0x0000ff00u, 0x000000ffu};
    // A scale of the brightness set on the device (255: as set there).
    uint8_t brightness = 255;

    friend bool operator==(const LightEffect&, const LightEffect&) = default;
};

// What a frame, or the project's default, does to one channel.
enum class LightActionKind : uint8_t {
    // The channel keeps what it plays (for a default: off).
    Unchanged = 0,
    Effect = 1,
    // A static color (0xWWRRGGBB).
    Color = 2,
    Off = 3,
};

struct LightAction {
    LightActionKind kind = LightActionKind::Unchanged;
    // The effect's LightEffect::id (Effect).
    uint16_t effectId = 0;
    // Color.
    uint32_t color = 0;
    // Set by the ambient lighting (from the frame's colors) rather than chosen by the author:
    // updated when the frame's colors change (edit::updateAmbientLighting()). Editor only.
    bool automatic = false;

    friend bool operator==(const LightAction&, const LightAction&) = default;
};

// An action per channel.
using LightActions = std::array<LightAction, kLightChannels>;

[[nodiscard]] inline bool changesNothing(const LightActions& actions)
{
    for (const LightAction& action : actions) {
        if (action.kind != LightActionKind::Unchanged) {
            return false;
        }
    }
    return true;
}

// The project's effects and what each channel shows when nothing else plays.
struct SpeakerLightSettings {
    std::vector<LightEffect> effects;
    LightActions defaults{};

    [[nodiscard]] const LightEffect* effect(uint16_t id) const
    {
        for (const LightEffect& candidate : effects) {
            if (candidate.id == id) {
                return &candidate;
            }
        }
        return nullptr;
    }
    // The next unused ID.
    [[nodiscard]] uint16_t nextEffectId() const
    {
        uint16_t id = 1;
        for (const LightEffect& candidate : effects) {
            if (candidate.id >= id) {
                id = static_cast<uint16_t>(candidate.id + 1);
            }
        }
        return id;
    }
    [[nodiscard]] bool empty() const { return effects.empty() && changesNothing(defaults); }

    friend bool operator==(const SpeakerLightSettings&, const SpeakerLightSettings&) = default;
};

}  // namespace colorizing::document
