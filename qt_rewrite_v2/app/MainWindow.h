#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QMainWindow>
#include <QElapsedTimer>
#include <QString>

#include "document/ProjectDocument.h"
#include "edit/DrawingTools.h"
#include "edit/UndoHistories.h"
#include "FrameImageUtils.h"

namespace colorizing::app {
class ColorRotationPanel;
class FramePreviewWidget;
class PalettePanel;
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
class QLineEdit;
class QSpinBox;
class QTimer;

namespace colorizing::app {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    using PaletteGradientDirection = edit::PaletteGradientDirection;
    using PaletteGradientPattern = edit::PaletteGradientPattern;

    enum class DrawingTool {
        Point,
        Line,
        Rectangle,
        FilledRectangle,
        Circle,
        FilledCircle,
        Ellipse,
        FilledEllipse,
        PaletteGradient,
        Selection,
        ColorPicker,
        FloodFill,
    };

    enum class PaintTarget {
        Frame,
        Background,
    };

    enum class FrameFilter {
        All,
        Hd,
        Background,
        Dynamic,
        Sprites,
    };

    struct PendingFreePaletteGradient {
        render::CanvasHit shapeStart;
        render::CanvasHit shapeEnd;
        PaintTarget target = PaintTarget::Frame;
        std::optional<std::size_t> backgroundIndex;
    };

    struct CanvasSelection {
        render::CanvasHit shapeStart;
        render::CanvasHit shapeEnd;
        PaintTarget target = PaintTarget::Frame;
        std::optional<std::size_t> backgroundIndex;
        std::vector<render::Point> pixels;
        std::optional<render::CanvasHit> gradientStart;
        std::optional<render::CanvasHit> gradientEnd;
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
    void setCurrentFrameComparisonMaskId(int maskId);
    void setCurrentFrameShapeComparisonMode(int mode);
    void setCurrentFrameBackgroundId(int backgroundId);
    void setCurrentFrameTriggerId(int triggerId);
    void setCurrentFrameDurationMs(int durationMs);
    void setCurrentFrameSwitchToMonochrome(bool enabled);
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
    void clearSpriteCaptureSelection();
    void removeCurrentSpriteSlot();
    void setCurrentSpriteSlotBounds();
    void setCurrentSpriteName();
    void setCurrentSpriteDetectionArea();
    void clearCurrentSpriteDetectionArea();
    void createCurrentSpriteHd();
    void removeCurrentSpriteHd();
    void createCurrentFrameHd();
    void removeCurrentFrameHd();
    void updateDynamicColorControls();
    void updateDynamicShadowControls();
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
    void pickDynamicSet(const render::CanvasHit& hit);
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
    void paintFramePixel(const render::CanvasHit& hit, bool erase);
    void pickFrameColor(const render::CanvasHit& hit);
    void floodFillFrame(const render::CanvasHit& hit);
    void paintBackgroundImagePixel(const render::CanvasHit& hit);
    void pickBackgroundImageColor(const render::CanvasHit& hit);
    void floodFillBackgroundImage(const render::CanvasHit& hit);
    void paintComparisonMaskPixel(const render::CanvasHit& hit, bool erase);
    void floodFillComparisonMask(const render::CanvasHit& hit, bool erase);
    void paintDynamicMaskPixel(const render::CanvasHit& hit, bool erase);
    void floodFillDynamicMask(const render::CanvasHit& hit, bool erase);
    void paintBackgroundMaskPixel(const render::CanvasHit& hit, bool erase);
    void floodFillBackgroundMask(const render::CanvasHit& hit, bool erase);
    void previewFrameStroke(const render::CanvasHit& start,
                            const render::CanvasHit& end,
                            bool erase);
    void paintFrameStroke(const render::CanvasHit& start,
                          const render::CanvasHit& end,
                          bool erase);
    [[nodiscard]] bool isFreePaletteGradientPending() const;
    void beginFreePaletteGradientSelection(const render::CanvasHit& start,
                                           const render::CanvasHit& end);
    void previewFreePaletteGradientDirection(const render::CanvasHit& start,
                                             const render::CanvasHit& end);
    void showPendingFreePaletteGradientSelection();
    void cancelFreePaletteGradientSelection();
    void toggleCanvasSelectionDirectionMode();
    void setCanvasSelectionFromStroke(const render::CanvasHit& start,
                                      const render::CanvasHit& end);
    void clearCanvasSelection();
    void updateCanvasSelectionOverlay();
    [[nodiscard]] bool isCanvasSelectionDirectionModeActive() const;
    void setCanvasSelectionDirection(const render::CanvasHit& start,
                                     const render::CanvasHit& end);
    void previewCanvasSelectionDirection(const render::CanvasHit& start,
                                         const render::CanvasHit& end);
    void fillCanvasSelectionWithPaletteGradient();
    [[nodiscard]] std::vector<render::Point> pixelsForStroke(const render::CanvasHit& start,
                                                             const render::CanvasHit& end) const;
    void commitPixels(render::Resolution resolution,
                      const std::vector<render::Point>& pixels,
                      const QString& statusText);
    void commitPaletteGradientPixels(render::Resolution resolution,
                                     const render::CanvasHit& start,
                                     const render::CanvasHit& end,
                                     const render::CanvasHit& gradientStart,
                                     const render::CanvasHit& gradientEnd,
                                     const QString& statusText);
    void commitFramePixelColors(render::Resolution resolution,
                                const std::vector<render::Point>& pixels,
                                const edit::PixelColorSource& colorForPoint,
                                const QString& statusText);
    void commitMaskPixels(const std::vector<render::Point>& pixels,
                          uint8_t value,
                          const QString& statusText);
    // kind is DynamicMask or BackgroundMask.
    void commitFrameMaskPixels(edit::EditKind kind,
                               render::Resolution resolution,
                               const std::vector<render::Point>& pixels,
                               uint8_t value,
                               const QString& statusText);
    void commitBackgroundImagePixels(render::Resolution resolution,
                                     const std::vector<render::Point>& pixels,
                                     const QString& statusText);
    void commitBackgroundImageGradientPixels(render::Resolution resolution,
                                             const render::CanvasHit& start,
                                             const render::CanvasHit& end,
                                             const render::CanvasHit& gradientStart,
                                             const render::CanvasHit& gradientEnd,
                                             const QString& statusText);
    void commitBackgroundImageColors(std::size_t backgroundIndex,
                                     render::Resolution resolution,
                                     const std::vector<render::Point>& pixels,
                                     const edit::PixelColorSource& colorForPoint,
                                     const QString& statusText);
    // Both return false, without committing, when there is nothing to change.
    bool commitPixelChanges(const edit::EditLayer& layer, std::vector<edit::PixelChange> changes);
    bool commitMaskChanges(const edit::EditLayer& layer,
                           std::vector<edit::MaskPixelChange> changes);
    [[nodiscard]] edit::PixelColorSource paletteGradientColorSource(
        std::size_t paletteIndex,
        const render::CanvasHit& shapeStart,
        const render::CanvasHit& shapeEnd,
        const render::CanvasHit& gradientStart,
        const render::CanvasHit& gradientEnd) const;
    void commitEdit(std::unique_ptr<edit::EditCommand> command);
    void refreshAfterEdit(const edit::EditTarget& target);
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
    void paintSpriteCaptureSelectionPixel(const render::CanvasHit& hit, bool erase);
    void commitSpriteCaptureSelectionPixels(const std::vector<render::Point>& pixels, bool erase);
    void updateSpriteCaptureSelectionOverlay();
    [[nodiscard]] std::vector<render::Point> spriteCaptureSelectionPixels() const;

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
    QToolButton* m_backgroundLayerButton = nullptr;
    QToolButton* m_playbackButton = nullptr;
    QToolButton* m_loopPlaybackButton = nullptr;
    QCheckBox* m_useProjectTimingCheckBox = nullptr;
    QToolButton* m_importFrameImageButton = nullptr;
    QToolButton* m_createHdFrameButton = nullptr;
    QToolButton* m_removeHdFrameButton = nullptr;
    QToolButton* m_assignMaskButton = nullptr;
    QToolButton* m_editMaskButton = nullptr;
    QToolButton* m_assignBackgroundButton = nullptr;
    QToolButton* m_editBackgroundButton = nullptr;
    QToolButton* m_copyFrameToBackgroundButton = nullptr;
    QToolButton* m_copyBackgroundToFrameButton = nullptr;
    QToolButton* m_importBackgroundImageButton = nullptr;
    QToolButton* m_exportBackgroundButton = nullptr;
    QToolButton* m_spriteBoundsOverlayButton = nullptr;
    QToolButton* m_spriteDetectionAreasButton = nullptr;
    QToolButton* m_spriteCanvasButton = nullptr;
    QToolButton* m_applySpriteEditsButton = nullptr;
    QToolButton* m_assignSpriteButton = nullptr;
    QToolButton* m_captureSpriteButton = nullptr;
    QToolButton* m_spriteCaptureSelectionButton = nullptr;
    QToolButton* m_clearSpriteCaptureSelectionButton = nullptr;
    QToolButton* m_removeSpriteButton = nullptr;
    QToolButton* m_clearSpriteDetectionAreaButton = nullptr;
    QSpinBox* m_comparisonMaskIdSpinBox = nullptr;
    QSpinBox* m_dynamicSetSpinBox = nullptr;
    QSpinBox* m_backgroundIdSpinBox = nullptr;
    QSpinBox* m_triggerIdSpinBox = nullptr;
    QSpinBox* m_frameDurationSpinBox = nullptr;
    QSpinBox* m_playbackIntervalSpinBox = nullptr;
    QSpinBox* m_dmdPixelSizeSpinBox = nullptr;
    QSpinBox* m_spriteMinXSpinBox = nullptr;
    QSpinBox* m_spriteMinYSpinBox = nullptr;
    QSpinBox* m_spriteMaxXSpinBox = nullptr;
    QSpinBox* m_spriteMaxYSpinBox = nullptr;
    QSpinBox* m_spriteDetectionAreaXSpinBox = nullptr;
    QSpinBox* m_spriteDetectionAreaYSpinBox = nullptr;
    QSpinBox* m_spriteDetectionAreaWidthSpinBox = nullptr;
    QSpinBox* m_spriteDetectionAreaHeightSpinBox = nullptr;
    QComboBox* m_paintTargetComboBox = nullptr;
    QComboBox* m_shapeComparisonModeComboBox = nullptr;
    QComboBox* m_dmdPixelShapeComboBox = nullptr;
    QCheckBox* m_switchToMonochromeCheckBox = nullptr;
    QComboBox* m_backgroundComboBox = nullptr;
    QComboBox* m_spriteComboBox = nullptr;
    QLineEdit* m_spriteNameLineEdit = nullptr;
    QComboBox* m_spriteSlotComboBox = nullptr;
    QComboBox* m_spriteDetectionAreaComboBox = nullptr;
    QLabel* m_spriteOriginalPreviewLabel = nullptr;
    QLabel* m_spriteColorizedPreviewLabel = nullptr;
    QLabel* m_backgroundPreviewLabel = nullptr;
    QLabel* m_selectedMaskLabel = nullptr;
    QLabel* m_selectedBackgroundLabel = nullptr;
    QLabel* m_selectedSpriteLabel = nullptr;
    QLabel* m_frameSpritesLabel = nullptr;
    std::vector<QToolButton*> m_toolButtons;
    std::vector<QToolButton*> m_colorButtons;
    std::vector<uint16_t> m_favoriteColors;
    std::vector<QToolButton*> m_dynamicColorButtons;
    std::vector<QToolButton*> m_dynamicShadowDirectionButtons;
    QToolButton* m_dynamicShadowColorButton = nullptr;
    ColorRotationPanel* m_colorRotationPanel = nullptr;
    PalettePanel* m_palettePanel = nullptr;
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
    bool m_spriteDependentViewsDirty = false;
    std::optional<std::size_t> m_spriteCaptureSelectionFrameIndex;
    std::vector<uint8_t> m_spriteCaptureSelectionMask;
    std::optional<PendingFreePaletteGradient> m_pendingFreePaletteGradient;
    std::optional<CanvasSelection> m_canvasSelection;
    bool m_canvasSelectionDirectionMode = false;
    edit::UndoHistories m_undoHistories;
    QString m_currentCromPath;
    QString m_currentCrpPath;
    std::optional<document::ProjectDocument> m_document;
};

}  // namespace colorizing::app
