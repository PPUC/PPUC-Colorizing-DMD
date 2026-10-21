#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QWidget>

#include <array>
#include <functional>
#include <memory>
#include <optional>

#include "document/SpeakerLights.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTimer;
class QToolButton;

namespace colorizing::runtime {
class SpeakerLightPlayer;
}

namespace colorizing::app {

class SpeakerLightView;

// The Light Effect section of the right sidebar: the settings of the effect selected in the
// Light Effects section, WS2812FX's segment settings (mode, three colors with white, speed,
// options) with a brightness scale and a duration, and a preview that plays the fields as
// they are. Changes wait for Apply; Revert shows the effect again.
class LightEffectPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(LightEffectPanel)

public:
    explicit LightEffectPanel(QWidget* parent = nullptr);
    ~LightEffectPanel() override;

    // The effect as the project has it; unapplied changes are dropped. none: no effect.
    void setEffect(const std::optional<document::LightEffect>& effect);
    [[nodiscard]] std::optional<uint16_t> effectId() const;
    // The effect as the fields say.
    [[nodiscard]] document::LightEffect effect() const;
    [[nodiscard]] bool hasChanges() const { return m_changed; }
    // For tests: the color the color button of color index would pick.
    void setColor(std::size_t index, uint32_t color);
    [[nodiscard]] SpeakerLightView* preview() const { return m_preview; }
    // For tests: advances the preview by ms.
    void advancePreview(uint32_t ms);

    std::function<void(const document::LightEffect& effect)> onApplyRequested;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void changed();
    void restartPreview();
    void updateState();
    void updateColorButtons();

    std::optional<document::LightEffect> m_effect;
    bool m_changed = false;
    bool m_filling = false;
    std::array<uint32_t, 3> m_colors{};
    QLabel* m_titleLabel = nullptr;
    QLineEdit* m_name = nullptr;
    QComboBox* m_mode = nullptr;
    std::array<QToolButton*, 3> m_colorButtons{};
    std::array<QSpinBox*, 3> m_white{};
    QSpinBox* m_speed = nullptr;
    QCheckBox* m_reverse = nullptr;
    QComboBox* m_fade = nullptr;
    QComboBox* m_size = nullptr;
    QCheckBox* m_gamma = nullptr;
    QSpinBox* m_brightness = nullptr;
    QSpinBox* m_duration = nullptr;
    QToolButton* m_applyButton = nullptr;
    QToolButton* m_revertButton = nullptr;
    SpeakerLightView* m_preview = nullptr;
    std::unique_ptr<runtime::SpeakerLightPlayer> m_player;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    uint32_t m_offsetMs = 0;
};

}  // namespace colorizing::app
