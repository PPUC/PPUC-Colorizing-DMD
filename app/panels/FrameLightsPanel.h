#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>

#include "document/SpeakerLights.h"

class QComboBox;
class QLabel;
class QToolButton;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

// What a frame, or the project's default, does to one speaker light channel: a choice of
// unchanged (for a default: off), off, a static color or one of the project's effects, and the
// color. Reports a change; the owner applies it.
class LightActionEditor final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(LightActionEditor)

public:
    // forDefault: no "Unchanged", the default of a channel is off without one.
    explicit LightActionEditor(bool forDefault, QWidget* parent = nullptr);

    // The effects to offer; keeps the action.
    void setEffects(const document::SpeakerLightSettings& settings);
    // Does not notify.
    void setAction(const document::LightAction& action);
    [[nodiscard]] document::LightAction action() const;
    // For tests: what the color button would pick.
    void setColor(uint32_t color);

    std::function<void()> onChanged;

private:
    void updateColorButton();

    bool m_forDefault = false;
    QComboBox* m_kind = nullptr;
    QToolButton* m_colorButton = nullptr;
    uint32_t m_color = 0x00ffffffu;
    bool m_filling = false;
};

// The Speaker Lights section of the right sidebar: what the frame on the canvas does to each
// speaker light channel when it shows, and the ambient lighting suggestion. A change applies to the
// selected frames, that channel only; the owner does it.
class FrameLightsPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(FrameLightsPanel)

public:
    explicit FrameLightsPanel(QWidget* parent = nullptr);

    // nullptr or no frame: nothing to edit.
    void setFrame(const document::ProjectDocument* document, std::optional<std::size_t> frame);
    [[nodiscard]] LightActionEditor* channel(std::size_t index) const { return m_channels[index]; }

    std::function<void(std::size_t channel, const document::LightAction& action)>
        onChannelChanged;
    // Suggest from Frame: the colors of each selected frame for channels 1 and 2.
    std::function<void()> onSuggestRequested;

private:
    std::array<LightActionEditor*, document::kLightChannels> m_channels{};
    QToolButton* m_suggestButton = nullptr;
    QLabel* m_note = nullptr;
};

}  // namespace colorizing::app
