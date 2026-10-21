#include "runtime/SpeakerLightPlayer.h"

#include <algorithm>

#include "WS2812FX.h"

namespace colorizing::runtime {

namespace {

constexpr std::size_t kMaxLeds = 256;

// RGBW: an effect's white reaches the LEDs as on an RGBW strip.
constexpr neoPixelType kStripType = NEO_GRBW;

WS2812FX& names()
{
    static WS2812FX strip(1, 0, kStripType, 1, 1);
    return strip;
}

}  // namespace

struct SpeakerLightPlayer::Strip {
    explicit Strip(std::size_t leds)
        : fx(static_cast<uint16_t>(leds), 0, kStripType, 1, 1)
    {
        fx.init();
    }
    WS2812FX fx;
};

std::size_t lightModeCount()
{
    return names().getModeCount();
}

std::string lightModeName(uint8_t mode)
{
    if (mode >= lightModeCount()) {
        return std::string();
    }
    return reinterpret_cast<const char*>(names().getModeName(mode));
}

std::optional<document::LightEffect> lightEffectForAction(
    const document::SpeakerLightSettings& settings,
    const document::LightAction& action)
{
    document::LightEffect effect;
    effect.mode = document::kLightModeStatic;
    effect.colors = {0, 0, 0};
    switch (action.kind) {
        case document::LightActionKind::Effect:
            if (const document::LightEffect* found = settings.effect(action.effectId)) {
                return *found;
            }
            return std::nullopt;
        case document::LightActionKind::Color:
            effect.colors[0] = action.color;
            return effect;
        case document::LightActionKind::Off:
            return effect;
        case document::LightActionKind::Unchanged:
            break;
    }
    return std::nullopt;
}

document::LightActions lightsShownAt(const document::ProjectDocument& document,
                                     std::size_t frameIndex)
{
    document::LightActions shown = document.speakerLights().defaults;
    std::array<bool, document::kLightChannels> found{};
    for (std::size_t frame = std::min(frameIndex + 1, document.frameCount()); frame-- > 0;) {
        const document::LightActions& lights = document.frameLights(frame);
        bool all = true;
        for (std::size_t channel = 0; channel < document::kLightChannels; ++channel) {
            if (!found[channel] && lights[channel].kind != document::LightActionKind::Unchanged) {
                shown[channel] = lights[channel];
                found[channel] = true;
            }
            all = all && found[channel];
        }
        if (all) {
            break;
        }
    }
    return shown;
}

SpeakerLightPlayer::SpeakerLightPlayer(std::size_t ledCount)
    : m_strip(std::make_unique<Strip>(std::clamp<std::size_t>(ledCount, 1, kMaxLeds)))
{
    start(std::nullopt, 0);
}

SpeakerLightPlayer::~SpeakerLightPlayer() = default;

void SpeakerLightPlayer::setLedCount(std::size_t ledCount)
{
    ledCount = std::clamp<std::size_t>(ledCount, 1, kMaxLeds);
    if (ledCount == this->ledCount()) {
        return;
    }
    m_strip = std::make_unique<Strip>(ledCount);
    start(m_playing, m_startMs);
}

std::size_t SpeakerLightPlayer::ledCount() const
{
    return m_strip->fx.getLength();
}

void SpeakerLightPlayer::setDefault(const std::optional<document::LightEffect>& effect)
{
    m_default = effect;
}

void SpeakerLightPlayer::play(const document::LightEffect& effect, uint32_t nowMs)
{
    start(effect, nowMs);
}

void SpeakerLightPlayer::playDefault(uint32_t nowMs)
{
    start(m_default, nowMs);
}

void SpeakerLightPlayer::start(const std::optional<document::LightEffect>& effect,
                               uint32_t nowMs)
{
    m_playing = effect;
    m_startMs = nowMs;
    WS2812FX& fx = m_strip->fx;
    const uint32_t black[3] = {0, 0, 0};
    uint32_t colors[3] = {0, 0, 0};
    uint8_t mode = document::kLightModeStatic;
    uint16_t speed = 1000;
    uint8_t options = 0;
    uint8_t brightness = 255;
    if (effect) {
        std::copy(effect->colors.begin(), effect->colors.end(), colors);
        mode = effect->mode < fx.getModeCount() ? effect->mode : document::kLightModeStatic;
        speed = effect->speed;
        options = effect->options;
        brightness = effect->brightness;
    }
    ws2812fxSetMillis(nowMs);
    fx.setSegment(0, 0, static_cast<uint16_t>(fx.getLength() - 1), mode,
                  effect ? colors : black, speed, options);
    fx.resetSegmentRuntimes();
    fx.setBrightness(brightness);
    fx.start();
    fx.trigger();
    fx.service();
}

void SpeakerLightPlayer::update(uint32_t nowMs)
{
    if (m_playing && m_playing->durationMs > 0 && nowMs - m_startMs >= m_playing->durationMs) {
        start(m_default, nowMs);
    }
    ws2812fxSetMillis(nowMs);
    m_strip->fx.service();
}

const std::optional<document::LightEffect>& SpeakerLightPlayer::playing() const
{
    return m_playing;
}

std::vector<uint32_t> SpeakerLightPlayer::colors() const
{
    WS2812FX& fx = m_strip->fx;
    std::vector<uint32_t> colors(fx.getLength());
    const uint8_t* pixels = fx.getPixels();
    // NEO_GRBW: green, red, blue, white.
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const uint8_t* p = pixels + 4 * i;
        const uint32_t white = p[3];
        const auto mix = [white](uint32_t value) { return std::min<uint32_t>(255, value + white); };
        colors[i] = (mix(p[1]) << 16) | (mix(p[0]) << 8) | mix(p[2]);
    }
    return colors;
}

}  // namespace colorizing::runtime
