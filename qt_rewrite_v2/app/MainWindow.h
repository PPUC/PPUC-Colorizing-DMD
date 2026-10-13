#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QMainWindow>
#include <QElapsedTimer>
#include <QString>

#include "CanvasEditController.h"
#include "document/ProjectDocument.h"
#include "edit/DrawingTools.h"
#include "edit/UndoHistories.h"
#include "FrameImageUtils.h"
#include "panels/FramePanel.h"

namespace colorizing::app {
class BackgroundPanel;
class ColorRotationPanel;
class DynamicPanel;
class FramePanel;
class FramePreviewWidget;
class MaskPanel;
class PalettePanel;
class SpritePanel;
class SpriteEditorWidget;
}

class QLabel;
class QListWidget;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QAction;
class QMenu;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTimer;

namespace colorizing::app {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    using DrawingTool = edit::DrawingTool;

    enum class FrameFilter {
        All,
        Hd,
        Background,
        Dynamic,
        Sprites,
        // Offered for HD-only projects alone: frames the runtime shows nothing for.
        HdIncomplete,
    };

    void createActions();
    void createCentralView();
    void openProject();
    void openRecentProject(const QString& entry);
    void saveProject();
    void saveProjectAs();
    void exportCurrentFramePng();
    void importImageIntoSelectedFrames();
    void loadProject(const QString& cromPath, const QString& crpPath);
    void rememberRecentProject(const QString& cromPath, const QString& crpPath);
    void refreshRecentProjectsMenu();
    void setStatusText(const QString& text);
    void setFrameStatusText(const QString& text);
    void refreshProjectSummary();
    void refreshFrameList(std::optional<std::size_t> selectedFrameIndex = std::nullopt);
    [[nodiscard]] QString frameListItemText(std::size_t frameIndex) const;
    // See ProjectDocument::rendersHdPlaneOnly(): SD mode, SD fallbacks and Remove HD are off.
    [[nodiscard]] bool isHdOnlyProject() const;
    // See ProjectDocument::editsOriginalPlaneOnly(): HD mode and every HD action are off.
    [[nodiscard]] bool isOriginalPlaneOnlyProject() const;
    // "SD" or "HD"; the plane's size where those names would mislead.
    [[nodiscard]] QString planeName(render::Resolution resolution) const;
    void showNoExtraPlaneEditing();
    // Why the runtime shows nothing for the frame of an HD-only project; empty otherwise.
    [[nodiscard]] QString frameHdGapsText(std::size_t frameIndex) const;
    void updateFrameFilterChoices();
    [[nodiscard]] bool frameMatchesFilter(std::size_t frameIndex) const;
    [[nodiscard]] std::optional<std::size_t> currentFrameIndex() const;
    [[nodiscard]] std::vector<std::size_t> selectedFrameIndices() const;
    void selectFrame(int frameIndex);
    void updateResolutionMode();
    void setDrawingTool(DrawingTool tool);
    void setActiveDrawColor(uint16_t rgb565);
    void updateFrameMetadataControls();
    void updateMaskControls();
    void updateBackgroundControls();
    void updatePaintTargetMode();
    void setPaintTarget(PaintTarget target);
    void paintTargetChanged();
    void setCurrentFrameComparisonMaskId(int maskId);
    void setCurrentFrameShapeComparisonMode(int mode);
    void setCurrentFrameBackgroundId(int backgroundId);
    void setCurrentFrameTriggerId(int triggerId);
    void setCurrentFrameDurationMs(int durationMs);
    void setCurrentFrameSwitchToMonochrome(bool enabled);
    struct FrameEditCount {
        int changed = 0;
        int skipped = 0;
    };
    // Sets the attribute on every selected frame, one undoable command per frame. Frames that
    // already have the value are left alone; frames the attribute cannot apply to are skipped.
    FrameEditCount editSelectedFramesAttribute(edit::FrameAttribute attribute, uint32_t value);
    void updateFrameStatusText();
    void leaveSpriteCanvas();
    void assignSelectedMaskToFrames();
    void editAssignedMask();
    void assignSelectedBackgroundToFrames();
    void editAssignedBackground();
    void copyCurrentFrameToSelectedBackground();
    void copySelectedBackgroundToFrames();
    void importImageIntoSelectedBackground();
    void exportSelectedBackgroundPng();
    void assignSelectedSpriteToCurrentSlot();
    void captureSelectedSpriteFromCurrentFrame();
    void removeCurrentSpriteSlot();
    void setCurrentSpriteSlotBounds();
    void setCurrentSpriteName(const QString& name);
    void setCurrentSpriteDetectionArea();
    void clearCurrentSpriteDetectionArea();
    void createCurrentSpriteHd();
    void removeCurrentSpriteHd();
    void createCurrentFrameHd();
    // HD-only project: creates what the frame lacks, without a dialog; nothing is upscaled.
    void createCurrentFrameHdFromOriginal(std::size_t frameIndex);
    void removeCurrentFrameHd();
    void updateDynamicColorControls();
    void updateDynamicShadowControls();
    void dynamicSetChanged(int setIndex);
    void editDynamicColor(std::size_t originalColorIndex);
    void setDynamicShadowDirectionBit(uint8_t bit, bool enabled);
    void editDynamicShadowColor();
    void updateColorRotationControls();
    void setCurrentColorRotationLength(std::size_t slotIndex, int length);
    void setCurrentColorRotationDelayMs(std::size_t slotIndex, int delayMs);
    void editCurrentColorRotationColor(std::size_t slotIndex, std::size_t colorIndex);
    void clearCurrentColorRotationSlot(std::size_t slotIndex);
    void updatePaletteControls();
    void setCurrentPaletteName(const QString& paletteName);
    void pickCurrentPaletteColor(std::size_t colorIndex);
    void editCurrentPaletteColor(std::size_t colorIndex);
    void applyCurrentPaletteGradient();
    void setFavoriteDrawColor(std::size_t favoriteIndex, uint16_t rgb565);
    void paintSpriteColorizedPixel(std::size_t spriteIndex,
                                   render::Resolution resolution,
                                   int x,
                                   int y,
                                   bool erase);
    void paintSpriteColorizedPixels(std::size_t spriteIndex,
                                    render::Resolution resolution,
                                    const std::vector<render::Point>& pixels,
                                    bool erase);
    void pickSpriteColorizedPixel(std::size_t spriteIndex,
                                  render::Resolution resolution,
                                  int x,
                                  int y);
    void floodFillSpriteColorized(std::size_t spriteIndex,
                                  render::Resolution resolution,
                                  int x,
                                  int y);
    void setSpriteDependentViewsDirty();
    void applyPendingSpriteDependentViews();
    [[nodiscard]] uint8_t activeDynamicSet() const;
    [[nodiscard]] PaintTarget activePaintTarget() const;
    [[nodiscard]] CanvasEditContext canvasEditContext() const;
    [[nodiscard]] CanvasEditOutputs canvasEditOutputs();
    // Returns false, without committing, when there is nothing to change.
    bool commitPixelChanges(const edit::EditLayer& layer, std::vector<edit::PixelChange> changes);
    void commitEdit(std::unique_ptr<edit::EditCommand> command);
    // For a command that has been applied already.
    void recordEdit(std::unique_ptr<edit::EditCommand> command);
    void refreshAfterEdit(const edit::EditCommand& command);
    [[nodiscard]] std::vector<edit::HistoryKey> currentHistoryKeys() const;
    [[nodiscard]] std::optional<std::size_t> currentComparisonMaskIndex() const;
    [[nodiscard]] bool isComparisonMaskModeActive() const;
    [[nodiscard]] bool isDynamicMaskModeActive() const;
    [[nodiscard]] bool isBackgroundMaskModeActive() const;
    [[nodiscard]] std::optional<std::size_t> selectedPaletteIndex() const;
    [[nodiscard]] std::optional<std::size_t> selectedBackgroundIndex() const;
    [[nodiscard]] std::optional<std::size_t> currentBackgroundIndex() const;
    [[nodiscard]] bool isBackgroundImageTargetActive() const;
    [[nodiscard]] std::optional<std::size_t> currentSpriteIndex() const;
    [[nodiscard]] bool isSpriteCanvasActive() const;
    void undo();
    void redo();
    void updateUndoRedoActions();
    void updateProjectHeaderStatus();
    void updateFrameThumbnail(std::size_t frameIndex);
    void updateFrameListItem(std::size_t frameIndex);
    void updateFrameListItemText(std::size_t frameIndex);
    void refreshComponentBrowsers();
    void updateMaskBrowserSelection();
    void updateBackgroundBrowserSelection();
    void updateSpriteBrowserSelection();
    void updatePaletteBrowserSelection();
    void updatePlaybackControls();
    void setPlaybackRunning(bool running);
    void advancePlaybackFrame();
    [[nodiscard]] int playbackIntervalForCurrentFrame() const;
    [[nodiscard]] DmdRenderOptions currentDmdRenderOptions() const;
    void updateDmdRenderOptions();
    void updateSpriteControls();
    [[nodiscard]] bool isSpriteCaptureSelectionModeActive() const;

    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_frameStatusLabel = nullptr;
    QTabWidget* m_leftBrowserTabs = nullptr;
    QListWidget* m_frameList = nullptr;
    QListWidget* m_maskList = nullptr;
    QListWidget* m_backgroundList = nullptr;
    QListWidget* m_spriteList = nullptr;
    QComboBox* m_frameFilterComboBox = nullptr;
    QToolButton* m_sdButton = nullptr;
    QToolButton* m_hdButton = nullptr;
    QToolButton* m_comparisonMaskOverlayButton = nullptr;
    QToolButton* m_dynamicMaskOverlayButton = nullptr;
    QToolButton* m_backgroundMaskOverlayButton = nullptr;
    QToolButton* m_playbackButton = nullptr;
    QToolButton* m_loopPlaybackButton = nullptr;
    QCheckBox* m_useProjectTimingCheckBox = nullptr;
    QSpinBox* m_playbackIntervalSpinBox = nullptr;
    QComboBox* m_backgroundComboBox = nullptr;
    std::vector<QToolButton*> m_toolButtons;
    std::vector<QToolButton*> m_colorButtons;
    std::vector<uint16_t> m_favoriteColors;
    BackgroundPanel* m_backgroundPanel = nullptr;
    ColorRotationPanel* m_colorRotationPanel = nullptr;
    DynamicPanel* m_dynamicPanel = nullptr;
    FramePanel* m_framePanel = nullptr;
    MaskPanel* m_maskPanel = nullptr;
    PalettePanel* m_palettePanel = nullptr;
    SpritePanel* m_spritePanel = nullptr;
    QToolButton* m_activeColorButton = nullptr;
    QStackedWidget* m_canvasStack = nullptr;
    FramePreviewWidget* m_preview = nullptr;
    SpriteEditorWidget* m_spriteEditor = nullptr;
    QTimer* m_playbackTimer = nullptr;
    QTimer* m_colorRotationPreviewTimer = nullptr;
    QElapsedTimer m_playbackElapsedTimer;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    DrawingTool m_drawingTool = DrawingTool::Point;
    uint16_t m_activeDrawColor = 0xffff;
    // Sprite shown on the Sprite page and in the sprite canvas; -1 once a project has none.
    int m_selectedSpriteIndex = 0;
    // Mask picked in the mask browser. It starts as the first frame's mask and is assigned to
    // frames only on request.
    std::optional<std::size_t> m_selectedMaskIndex;
    bool m_spriteDependentViewsDirty = false;
    CanvasEditController m_canvasEdit;
    edit::UndoHistories m_undoHistories;
    QString m_currentCromPath;
    QString m_currentCrpPath;
    std::optional<document::ProjectDocument> m_document;
};

}  // namespace colorizing::app
