#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include <QCoreApplication>
#include <QString>
#include <QWidget>

#include "document/ProjectDocument.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

class ColorGridWidget;

// What the sprite page displays. The slot, the detection area and the toggles are the page's
// own state and are kept across refreshes.
struct SpritePanelState {
    const document::ProjectDocument* document = nullptr;
    std::optional<std::size_t> frameIndex;
    std::optional<std::size_t> spriteIndex;
    bool hasCaptureSelection = false;
    bool spriteEditsPending = false;
};

// Spin box values; a minimum is never larger than its maximum.
struct SpriteSlotBounds {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
};

// Spin box values as entered; the area may reach beyond the sprite.
struct SpriteDetectionAreaInput {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Whether the area is in use; an unused one is stored as 0xffff values.
[[nodiscard]] bool isSpriteDetectionAreaSet(const document::SpriteDetectionArea& area);

// Inspector page for the selected sprite and the sprite slots of a frame. It only displays and
// reports what the user asked for; the owner applies the edits and calls refresh(). It has two
// pages, which the owner may put into sections of their own: framePage() for the frame canvas
// (the frame's slots, capture, bounds, Outline Sprites, Edit Sprite) and recordPage() for the
// sprite editor (name, detection areas, shape mode, dynamic sets, HD, Apply Sprite, Back to
// Frame).
class SpritePanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SpritePanel)

public:
    explicit SpritePanel(QWidget* parent = nullptr);

    void refresh(const SpritePanelState& state);
    [[nodiscard]] QWidget* framePage() const { return m_framePage; }
    [[nodiscard]] QWidget* recordPage() const { return m_recordPage; }
    // Ignored for an index the page does not offer.
    void setSelectedDetectionAreaIndex(std::size_t areaIndex);
    // Neither notifies.
    void setSpriteCanvasChecked(bool checked);
    void setCaptureSelectionChecked(bool checked);
    void setApplySpriteEditsEnabled(bool enabled);

    // Empty when the frame has no sprite slots.
    [[nodiscard]] std::optional<std::size_t> selectedSlotIndex() const;
    // Does not notify; ignored for a slot the page does not list.
    void setSelectedSlotIndex(std::size_t slotIndex);
    [[nodiscard]] std::size_t selectedDetectionAreaIndex() const;
    [[nodiscard]] SpriteSlotBounds slotBounds() const;
    [[nodiscard]] SpriteDetectionAreaInput detectionAreaInput() const;
    [[nodiscard]] bool isCaptureSelectionChecked() const;
    [[nodiscard]] bool isBoundsOverlayChecked() const;
    // Outline Sprites: the found sprites outlined on the frame canvas (on at first).
    [[nodiscard]] bool isOutlinesChecked() const;
    [[nodiscard]] bool isDetectionAreasChecked() const;
    [[nodiscard]] bool isSpriteCanvasChecked() const;
    // The warning that the frame uses more than kSpritesPerFrameWarning sprites; empty while
    // it is hidden.
    [[nodiscard]] QString spriteCountWarning() const;

    std::function<void(const QString& name)> onNameEdited;
    std::function<void()> onSlotSelectionChanged;
    std::function<void()> onAssignRequested;
    std::function<void()> onCaptureRequested;
    std::function<void()> onRemoveRequested;
    std::function<void(bool checked)> onCaptureSelectionToggled;
    std::function<void()> onClearCaptureSelectionRequested;
    std::function<void(bool checked)> onBoundsOverlayToggled;
    std::function<void(bool checked)> onOutlinesToggled;
    std::function<void(bool checked)> onDetectionAreasToggled;
    std::function<void(bool checked)> onSpriteCanvasToggled;
    std::function<void()> onApplySpriteEditsRequested;
    // Go to the frame the sprite was taken from (CDMD's "to source frame").
    std::function<void()> onSourceFrameRequested;
    // "Create HD Sprite" and "Remove HD Sprite" for the selected sprite ("Create 256x64
    // Sprite" alone in a project with 256x64 original frames, neither with 192x64 ones).
    std::function<void()> onCreateHdRequested;
    std::function<void()> onRemoveHdRequested;
    // Sprite shape mode and dynamic content (CDMD's sprite window: IDC_SHAPEM, the dynamic
    // sets and their colors).
    std::function<void(bool shapeMode)> onShapeModeToggled;
    std::function<void(bool enabled)> onDynamicPaintToggled;
    std::function<void(std::size_t originalColorIndex)> onDynamicColorClicked;
    std::function<void()> onEditColorsToDynamicSet;
    std::function<void()> onDynamicSetToEditColors;
    // 0 to 8, shown as 1 to 9.
    [[nodiscard]] std::size_t dynamicSet() const;
    void setDynamicPaintChecked(bool checked);
    // Read the new values with slotBounds().
    std::function<void()> onSlotBoundsEdited;
    std::function<void()> onDetectionAreaSelectionChanged;
    // Read the new values with detectionAreaInput().
    std::function<void()> onDetectionAreaEdited;
    std::function<void()> onClearDetectionAreaRequested;

private:
    QWidget* m_framePage = nullptr;
    QWidget* m_recordPage = nullptr;
    QLabel* m_selectedSpriteLabel = nullptr;
    QLabel* m_editedSpriteLabel = nullptr;
    QToolButton* m_backToFrameButton = nullptr;
    QLineEdit* m_nameLineEdit = nullptr;
    QComboBox* m_slotComboBox = nullptr;
    QToolButton* m_assignButton = nullptr;
    QToolButton* m_captureButton = nullptr;
    QToolButton* m_removeButton = nullptr;
    QToolButton* m_captureSelectionButton = nullptr;
    QToolButton* m_clearCaptureSelectionButton = nullptr;
    QToolButton* m_boundsOverlayButton = nullptr;
    QToolButton* m_outlinesButton = nullptr;
    QToolButton* m_detectionAreasButton = nullptr;
    QToolButton* m_spriteCanvasButton = nullptr;
    QToolButton* m_applySpriteEditsButton = nullptr;
    QToolButton* m_sourceFrameButton = nullptr;
    QToolButton* m_createHdButton = nullptr;
    QToolButton* m_removeHdButton = nullptr;
    QCheckBox* m_shapeModeCheckBox = nullptr;
    QToolButton* m_dynamicPaintButton = nullptr;
    QSpinBox* m_dynamicSetSpinBox = nullptr;
    ColorGridWidget* m_dynamicColorGrid = nullptr;
    QToolButton* m_editColorsToSetButton = nullptr;
    QToolButton* m_setToEditColorsButton = nullptr;
    const document::ProjectDocument* m_dynamicDocument = nullptr;
    std::optional<std::size_t> m_dynamicSprite;
    void refreshDynamic(const document::ProjectDocument* document,
                        std::optional<std::size_t> spriteIndex);
    QSpinBox* m_minXSpinBox = nullptr;
    QSpinBox* m_minYSpinBox = nullptr;
    QSpinBox* m_maxXSpinBox = nullptr;
    QSpinBox* m_maxYSpinBox = nullptr;
    QComboBox* m_detectionAreaComboBox = nullptr;
    QToolButton* m_clearDetectionAreaButton = nullptr;
    QSpinBox* m_detectionAreaXSpinBox = nullptr;
    QSpinBox* m_detectionAreaYSpinBox = nullptr;
    QSpinBox* m_detectionAreaWidthSpinBox = nullptr;
    QSpinBox* m_detectionAreaHeightSpinBox = nullptr;
    QLabel* m_frameSpritesLabel = nullptr;
    // Shown when the frame uses more than kSpritesPerFrameWarning sprites.
    QLabel* m_spriteCountWarningLabel = nullptr;
};

}  // namespace colorizing::app
