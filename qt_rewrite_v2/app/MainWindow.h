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
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"
#include "FrameImageUtils.h"
#include "RuntimeView.h"
#include "panels/FramePanel.h"

namespace colorizing::app {
class BackgroundPanel;
class ColorRotationPanel;
class DynamicPanel;
class FramePanel;
class FramePreviewWidget;
class LazyListIcons;
class MaskPanel;
class PalettePanel;
class RecordListPage;
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
        // Offered where HD is an extra resolution only.
        Hd,
        Background,
        Dynamic,
        Sprites,
        // Offered for HD-only projects alone, as "Not rendered": frames the runtime shows
        // nothing for.
        HdIncomplete,
        // Frames a save flags inactive and the runtime skips, see
        // legacy::isFrameSkippedAtRuntime().
        Inactive,
    };

    void createActions();
    void createCentralView();
    void openProject();
    void openRecentProject(const QString& entry);
    void saveProject();
    void saveProjectAs();
    // Writes libserum's output for the current frame, see RuntimeView.
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
    // See ProjectDocument::rendersHdPlaneOnly(): there are no SD mode and no SD fallbacks, and
    // the 256x64 data cannot be removed.
    [[nodiscard]] bool isHdOnlyProject() const;
    // See ProjectDocument::editsOriginalPlaneOnly(): there is no extra plane at all.
    [[nodiscard]] bool isOriginalPlaneOnlyProject() const;
    // Whether "HD" and "SD" are shown at all, see PlaneLabels.h.
    [[nodiscard]] bool showsHdAndSd() const;
    // "SD" or "HD"; the plane's size in a project with one plane.
    [[nodiscard]] QString planeName(render::Resolution resolution) const;
    // Why the runtime shows nothing for the frame of an HD-only project; empty otherwise.
    [[nodiscard]] QString frameHdGapsText(std::size_t frameIndex) const;
    // The gaps of frameHdGapsText() and what "inactive" means, for a frame list item.
    [[nodiscard]] QString frameListItemToolTip(std::size_t frameIndex) const;
    // A frame's "inactive" marker in the list and the status line follows an edit.
    void updateInactiveFrameMarker(std::size_t frameIndex);
    void updateFrameFilterChoices();
    [[nodiscard]] bool frameMatchesFilter(std::size_t frameIndex) const;
    [[nodiscard]] std::optional<std::size_t> currentFrameIndex() const;
    [[nodiscard]] std::vector<std::size_t> selectedFrameIndices() const;
    void selectFrame(int frameIndex);
    void updateResolutionMode();
    // Enabled state of the SD/HD, overlay and tool buttons: off while the Runtime view is on.
    void updateCanvasModeButtons();
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
    // Adding and deleting records, see edit::StructureCommand. The delete functions ask
    // before they delete.
    void deleteSelectedFrames();
    // Asks for the duration and deletes the selected frames that are shorter.
    void deleteSelectedFramesShorterThan();
    // note: what chose the frames, for the question; empty for the selection itself.
    void deleteFrames(const std::vector<std::size_t>& frameIndices, const QString& note);
    void addSprite();
    void deleteSelectedSprite();
    void addBackgroundFromCurrentFrame();
    void deleteSelectedBackground();
    void addComparisonMask();
    void deleteSelectedComparisonMask();
    [[nodiscard]] bool confirmDeletion(const QString& title,
                                       const QString& question,
                                       const QString& consequences);
    // For a command that has been applied already.
    void recordStructureEdit(std::unique_ptr<edit::StructureCommand> command);
    // After a structure command ran in either direction: stops playback, drops the canvas
    // selections, moves the frame, mask, background or sprite selection to the added or
    // restored record or to the neighbour of a deleted one, and refreshes every view.
    void refreshAfterStructureEdit(const edit::StructureCommand& command);
    // Texts and thumbnails of the frame list rows after a change that leaves the frames
    // where they are; the list is rebuilt only if other frames match the filter now.
    void refreshFrameListItems();
    // Makes the frame, or the nearest listed frame behind it, the current one.
    void selectFrameNear(std::size_t frameIndex);
    // Enabled state of the Delete Frames actions and of New and Delete on the browser pages.
    void updateStructureControls();
    [[nodiscard]] std::vector<edit::HistoryKey> currentHistoryKeys() const;
    [[nodiscard]] std::optional<std::size_t> currentComparisonMaskIndex() const;
    [[nodiscard]] bool isComparisonMaskModeActive() const;
    // The plane dynamic masks, colors and shadows are edited on, whatever the editing mode:
    // see ProjectDocument::dynamicPlane().
    [[nodiscard]] render::Resolution dynamicPlane() const;
    // False in HD mode of an ordinary project: the dynamic zones are those of the SD plane
    // and are not shown or painted over the HD frame.
    [[nodiscard]] bool isDynamicPlaneActive() const;
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
    // The frame's thumbnail is out of date, see LazyListIcons::invalidate().
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
    // The Runtime view shows libserum's output for the current frame on the canvas, see
    // RuntimeView. While it is on nothing can be edited on the canvas.
    void setRuntimeViewEnabled(bool enabled);
    void setRuntimeViewRows(runtime::PlaneRequest rows);
    // Actions, tool buttons and the status line follow the view's state.
    void updateRuntimeViewControls();
    // The frames playback goes through from the current one on, in list order.
    [[nodiscard]] std::vector<std::size_t> playbackFrameOrder() const;
    // Shows the current frame through libserum now and starts its color rotations.
    void startRuntimeRotations();
    void stepRuntimeRotations();
    [[nodiscard]] int playbackIntervalForCurrentFrame() const;
    [[nodiscard]] DmdRenderOptions currentDmdRenderOptions() const;
    void updateDmdRenderOptions();
    void updateSpriteControls();
    [[nodiscard]] bool isSpriteCaptureSelectionModeActive() const;

    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_deleteFramesAction = nullptr;
    // The same in the frame list's menu, with the Delete and Backspace keys.
    QAction* m_deleteFramesListAction = nullptr;
    QAction* m_deleteShortFramesAction = nullptr;
    QAction* m_runtimeViewAction = nullptr;
    QAction* m_runtimeRows32Action = nullptr;
    QAction* m_runtimeRows64Action = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_frameStatusLabel = nullptr;
    QTabWidget* m_leftBrowserTabs = nullptr;
    QListWidget* m_frameList = nullptr;
    LazyListIcons* m_frameThumbnails = nullptr;
    QListWidget* m_maskList = nullptr;
    QListWidget* m_backgroundList = nullptr;
    QListWidget* m_spriteList = nullptr;
    QComboBox* m_frameFilterComboBox = nullptr;
    RecordListPage* m_maskPage = nullptr;
    RecordListPage* m_backgroundPage = nullptr;
    RecordListPage* m_spritePage = nullptr;
    QToolButton* m_sdButton = nullptr;
    QToolButton* m_hdButton = nullptr;
    QToolButton* m_comparisonMaskOverlayButton = nullptr;
    QToolButton* m_dynamicMaskOverlayButton = nullptr;
    QToolButton* m_backgroundMaskOverlayButton = nullptr;
    QToolButton* m_playbackButton = nullptr;
    QToolButton* m_runtimeViewButton = nullptr;
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
    QTimer* m_runtimeRotationTimer = nullptr;
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
    RuntimeView m_runtimeView;
    edit::UndoHistories m_undoHistories;
    QString m_currentCromPath;
    QString m_currentCrpPath;
    std::optional<document::ProjectDocument> m_document;
};

}  // namespace colorizing::app
