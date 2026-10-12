#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <QWidget>

#include "document/ProjectDocument.h"
#include "FrameImageUtils.h"
#include "PaintTarget.h"

class QCheckBox;
class QComboBox;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

// Legacy trigger ID that switches the display to monochrome; manual trigger IDs stay below it.
constexpr uint32_t kSwitchToMonochromeTriggerId = 65432;
constexpr int kMaxManualTriggerId = static_cast<int>(kSwitchToMonochromeTriggerId - 1);
constexpr int kMaxFrameDurationMs = 30000;

// What the frame page displays. The drawing target, the background layer toggle and the DMD
// pixel settings are the page's own state and are kept across refreshes.
struct FramePanelState {
    const document::ProjectDocument* document = nullptr;
    std::optional<std::size_t> frameIndex;
    bool hasSelectedFrames = false;
};

// Inspector page for the frame metadata, the drawing target and the DMD rendering settings.
// It only displays and reports what the user asked for; the owner applies the edits and calls
// refresh().
class FramePanel final : public QWidget {
public:
    explicit FramePanel(QWidget* parent = nullptr);

    // Also labels the Create/Remove HD buttons for the frame again. Whether the drawing target
    // can be chosen is the owner's decision alone, see setPaintTargetEnabled().
    void refresh(const FramePanelState& state);
    // Does not notify.
    void setPaintTarget(PaintTarget target);
    void setPaintTargetEnabled(bool enabled);
    void setImportImageEnabled(bool enabled);
    // While a sprite is edited in the canvas, the Create/Remove HD buttons act on that sprite.
    void showSpriteHdActions(bool spriteHasHd);

    [[nodiscard]] PaintTarget paintTarget() const;
    // The spin box value: -1 for no trigger and for the monochrome trigger, 0 while the frame
    // carries a trigger ID the spin box cannot show.
    [[nodiscard]] int triggerId() const;
    [[nodiscard]] bool isSwitchToMonochromeChecked() const;
    [[nodiscard]] bool isBackgroundLayerChecked() const;
    [[nodiscard]] DmdRenderOptions dmdRenderOptions() const;

    std::function<void()> onPaintTargetChanged;
    // -1 clears the assignment.
    std::function<void(int backgroundId)> onBackgroundIdEdited;
    // -1 clears the trigger.
    std::function<void(int triggerId)> onTriggerIdEdited;
    std::function<void(int durationMs)> onDurationEdited;
    std::function<void(bool checked)> onSwitchToMonochromeToggled;
    // Read the new values with dmdRenderOptions().
    std::function<void()> onDmdRenderOptionsChanged;
    std::function<void(bool checked)> onBackgroundLayerToggled;
    std::function<void()> onImportImageRequested;
    std::function<void()> onCreateHdRequested;
    std::function<void()> onRemoveHdRequested;

private:
    void showFrameHdActions(bool canCreate, bool canRemove);

    QSpinBox* m_backgroundIdSpinBox = nullptr;
    QSpinBox* m_triggerIdSpinBox = nullptr;
    QSpinBox* m_durationSpinBox = nullptr;
    QCheckBox* m_switchToMonochromeCheckBox = nullptr;
    QToolButton* m_backgroundLayerButton = nullptr;
    QToolButton* m_importImageButton = nullptr;
    QToolButton* m_createHdButton = nullptr;
    QToolButton* m_removeHdButton = nullptr;
    QComboBox* m_paintTargetComboBox = nullptr;
    QComboBox* m_dmdPixelShapeComboBox = nullptr;
    QSpinBox* m_dmdPixelSizeSpinBox = nullptr;
};

}  // namespace colorizing::app
