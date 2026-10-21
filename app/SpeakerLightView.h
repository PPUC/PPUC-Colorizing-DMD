#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/SpeakerLights.h"

class QTimer;

namespace colorizing::runtime {
class SpeakerLightPlayer;
}

namespace colorizing::app {

// How the speaker light preview draws a speaker: its LEDs on a circle or around a rectangle.
enum class SpeakerShape {
    Circle,
    Rectangle,
};

// The speaker light preview (an editor setting, as the hardware differs from machine to
// machine): whether it shows, the speakers' shape and LEDs, and the channel each speaker shows.
struct SpeakerLightPreviewOptions {
    bool visible = false;
    SpeakerShape shape = SpeakerShape::Circle;
    int ledCount = 16;
    // 0 to 3: channel 1 to 4.
    int leftChannel = 0;
    int rightChannel = 1;

    friend bool operator==(const SpeakerLightPreviewOptions&,
                           const SpeakerLightPreviewOptions&) = default;
};

[[nodiscard]] SpeakerLightPreviewOptions loadSpeakerLightPreviewOptions();
void saveSpeakerLightPreviewOptions(const SpeakerLightPreviewOptions& options);

// One speaker: its LEDs in their colors (0x00RRGGBB), on a circle or around a rectangle.
class SpeakerLightView final : public QWidget {
public:
    explicit SpeakerLightView(QWidget* parent = nullptr);

    void setShape(SpeakerShape shape);
    void setColors(const std::vector<uint32_t>& colors);
    [[nodiscard]] const std::vector<uint32_t>& colors() const { return m_colors; }
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    SpeakerShape m_shape = SpeakerShape::Circle;
    std::vector<uint32_t> m_colors;
};

// A left and a right speaker, each playing a channel with the WS2812FX effects
// (runtime::SpeakerLightPlayer). The effects come from the project at a frame (the canvas) or
// from libserum (the frame tester).
class SpeakerLightPair final : public QObject {
public:
    explicit SpeakerLightPair(QWidget* viewParent);
    ~SpeakerLightPair() override;

    [[nodiscard]] SpeakerLightView* left() const { return m_views[0]; }
    [[nodiscard]] SpeakerLightView* right() const { return m_views[1]; }
    void setOptions(const SpeakerLightPreviewOptions& options);
    [[nodiscard]] const SpeakerLightPreviewOptions& options() const { return m_options; }

    // The canvas: what the channels play while the frame shows (runtime::lightsShownAt()). An
    // effect already playing on a speaker goes on; nothing without a project or frame.
    void showFrame(const document::ProjectDocument* document, std::optional<std::size_t> frame);
    // The frame tester: the defaults of the loaded colorization, played from now on.
    void setDefaults(
        const std::array<std::optional<document::LightEffect>, document::kLightChannels>&
            defaults);
    // The frame tester: what libserum started (nothing for a channel it left alone).
    void start(const std::array<std::optional<document::LightEffect>, document::kLightChannels>&
                   effects);
    // For tests: advances the effects by ms and draws them.
    void advance(uint32_t ms);

private:
    void tick();
    [[nodiscard]] uint32_t now() const;
    [[nodiscard]] int channelOf(std::size_t speaker) const;
    void startSpeaker(std::size_t speaker, const std::optional<document::LightEffect>& effect);

    SpeakerLightPreviewOptions m_options;
    std::array<SpeakerLightView*, 2> m_views{};
    std::array<std::unique_ptr<runtime::SpeakerLightPlayer>, 2> m_players;
    // What each speaker was given last, to leave an effect playing.
    std::array<std::optional<document::LightEffect>, 2> m_shown;
    std::array<bool, 2> m_hasShown{};
    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    uint32_t m_offsetMs = 0;
};

}  // namespace colorizing::app
