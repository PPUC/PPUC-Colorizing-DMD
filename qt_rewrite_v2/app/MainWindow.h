#pragma once

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <QMainWindow>
#include <QElapsedTimer>
#include <QString>

#include "document/ProjectDocument.h"
#include "FrameImageUtils.h"

namespace colorizing::app {
class ColorGridWidget;
class FramePreviewWidget;
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
    enum class DrawingTool {
        Point,
        Line,
        Rectangle,
        FilledRectangle,
        Circle,
        FilledCircle,
        Ellipse,
        FilledEllipse,
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

    struct PixelChange {
        int x = 0;
        int y = 0;
        uint16_t oldColor = 0;
        uint16_t newColor = 0;
    };

    struct PixelEditCommand {
        std::size_t frameIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;
        std::vector<PixelChange> changes;
    };

    struct MaskPixelChange {
        int x = 0;
        int y = 0;
        uint8_t oldValue = 0;
        uint8_t newValue = 0;
    };

    struct MaskEditCommand {
        std::size_t maskIndex = 0;
        std::vector<MaskPixelChange> changes;
    };

    struct EditHistoryKey {
        std::size_t frameIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;

        [[nodiscard]] bool operator<(const EditHistoryKey& other) const;
    };

    struct EditHistory {
        std::vector<PixelEditCommand> undoStack;
        std::vector<PixelEditCommand> redoStack;
    };

    struct MaskEditHistory {
        std::vector<MaskEditCommand> undoStack;
        std::vector<MaskEditCommand> redoStack;
    };

    using DynamicMaskPixelChange = MaskPixelChange;

    struct DynamicMaskEditCommand {
        std::size_t frameIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;
        std::vector<DynamicMaskPixelChange> changes;
    };

    struct DynamicMaskEditHistory {
        std::vector<DynamicMaskEditCommand> undoStack;
        std::vector<DynamicMaskEditCommand> redoStack;
    };

    struct DynamicColorEditCommand {
        std::size_t frameIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;
        std::size_t setIndex = 0;
        std::size_t originalColorIndex = 0;
        uint16_t oldColor = 0;
        uint16_t newColor = 0;
    };

    struct DynamicColorEditHistory {
        std::vector<DynamicColorEditCommand> undoStack;
        std::vector<DynamicColorEditCommand> redoStack;
    };

    struct DynamicShadowEditChange {
        std::size_t frameIndex = 0;
        uint8_t oldDirection = 0;
        uint8_t newDirection = 0;
        uint16_t oldColor = 0;
        uint16_t newColor = 0;
    };

    struct DynamicShadowEditCommand {
        render::Resolution resolution = render::Resolution::Sd;
        std::size_t setIndex = 0;
        bool editDirection = false;
        bool editColor = false;
        std::vector<DynamicShadowEditChange> changes;
    };

    struct DynamicShadowEditHistory {
        std::vector<DynamicShadowEditCommand> undoStack;
        std::vector<DynamicShadowEditCommand> redoStack;
    };

    using BackgroundMaskPixelChange = MaskPixelChange;

    struct BackgroundMaskEditCommand {
        std::size_t frameIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;
        std::vector<BackgroundMaskPixelChange> changes;
    };

    struct BackgroundMaskEditHistory {
        std::vector<BackgroundMaskEditCommand> undoStack;
        std::vector<BackgroundMaskEditCommand> redoStack;
    };

    struct BackgroundEditHistoryKey {
        std::size_t backgroundIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;

        [[nodiscard]] bool operator<(const BackgroundEditHistoryKey& other) const;
    };

    struct BackgroundImageEditCommand {
        std::size_t backgroundIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;
        std::vector<PixelChange> changes;
    };

    struct BackgroundImageEditHistory {
        std::vector<BackgroundImageEditCommand> undoStack;
        std::vector<BackgroundImageEditCommand> redoStack;
    };

    struct SpriteEditHistoryKey {
        std::size_t spriteIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;

        [[nodiscard]] bool operator<(const SpriteEditHistoryKey& other) const;
    };

    struct SpriteImageEditCommand {
        std::size_t spriteIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;
        std::vector<PixelChange> changes;
    };

    struct SpriteImageEditHistory {
        std::vector<SpriteImageEditCommand> undoStack;
        std::vector<SpriteImageEditCommand> redoStack;
    };

    struct PaletteColorChange {
        std::size_t colorIndex = 0;
        uint16_t oldColor = 0;
        uint16_t newColor = 0;
    };

    struct PaletteEditCommand {
        std::size_t paletteIndex = 0;
        std::vector<PaletteColorChange> changes;
    };

    struct PaletteEditHistory {
        std::vector<PaletteEditCommand> undoStack;
        std::vector<PaletteEditCommand> redoStack;
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
    void setCurrentColorRotationLength(int length);
    void setCurrentColorRotationDelayMs(int delayMs);
    void editCurrentColorRotationColor(std::size_t colorIndex);
    void clearCurrentColorRotationSlot();
    void updatePaletteControls();
    void setCurrentPaletteName();
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
    [[nodiscard]] std::vector<render::Point> pixelsForStroke(const render::CanvasHit& start,
                                                             const render::CanvasHit& end) const;
    void commitPixels(render::Resolution resolution,
                      const std::vector<render::Point>& pixels,
                      const QString& statusText);
    void commitMaskPixels(const std::vector<render::Point>& pixels,
                          uint8_t value,
                          const QString& statusText);
    void commitDynamicMaskPixels(render::Resolution resolution,
                                 const std::vector<render::Point>& pixels,
                                 uint8_t value,
                                 const QString& statusText);
    void commitBackgroundMaskPixels(render::Resolution resolution,
                                    const std::vector<render::Point>& pixels,
                                    uint8_t value,
                                    const QString& statusText);
    void commitBackgroundImagePixels(render::Resolution resolution,
                                     const std::vector<render::Point>& pixels,
                                     const QString& statusText);
    void applyPixelEdit(const PixelEditCommand& command, bool useNewColor);
    void applyMaskEdit(const MaskEditCommand& command, bool useNewValue);
    void applyDynamicMaskEdit(const DynamicMaskEditCommand& command, bool useNewValue);
    void applyDynamicColorEdit(const DynamicColorEditCommand& command, bool useNewColor);
    void applyDynamicShadowEdit(const DynamicShadowEditCommand& command, bool useNewValue);
    void applyBackgroundMaskEdit(const BackgroundMaskEditCommand& command, bool useNewValue);
    void applyBackgroundImageEdit(const BackgroundImageEditCommand& command, bool useNewColor);
    void applySpriteImageEdit(const SpriteImageEditCommand& command, bool useNewColor);
    void applyPaletteEdit(const PaletteEditCommand& command, bool useNewColor);
    [[nodiscard]] std::optional<EditHistoryKey> currentEditHistoryKey() const;
    [[nodiscard]] EditHistory* currentEditHistory();
    [[nodiscard]] const EditHistory* currentEditHistory() const;
    [[nodiscard]] std::optional<std::size_t> currentComparisonMaskIndex() const;
    [[nodiscard]] MaskEditHistory* currentMaskEditHistory();
    [[nodiscard]] const MaskEditHistory* currentMaskEditHistory() const;
    [[nodiscard]] bool isComparisonMaskModeActive() const;
    [[nodiscard]] DynamicMaskEditHistory* currentDynamicMaskEditHistory();
    [[nodiscard]] const DynamicMaskEditHistory* currentDynamicMaskEditHistory() const;
    [[nodiscard]] DynamicColorEditHistory* currentDynamicColorEditHistory();
    [[nodiscard]] const DynamicColorEditHistory* currentDynamicColorEditHistory() const;
    [[nodiscard]] DynamicShadowEditHistory* currentDynamicShadowEditHistory();
    [[nodiscard]] const DynamicShadowEditHistory* currentDynamicShadowEditHistory() const;
    [[nodiscard]] bool isDynamicMaskModeActive() const;
    [[nodiscard]] BackgroundMaskEditHistory* currentBackgroundMaskEditHistory();
    [[nodiscard]] const BackgroundMaskEditHistory* currentBackgroundMaskEditHistory() const;
    [[nodiscard]] bool isBackgroundMaskModeActive() const;
    [[nodiscard]] std::optional<std::size_t> selectedPaletteIndex() const;
    [[nodiscard]] PaletteEditHistory* currentPaletteEditHistory();
    [[nodiscard]] const PaletteEditHistory* currentPaletteEditHistory() const;
    [[nodiscard]] std::optional<std::size_t> selectedBackgroundIndex() const;
    [[nodiscard]] std::optional<std::size_t> currentBackgroundIndex() const;
    [[nodiscard]] std::optional<BackgroundEditHistoryKey> currentBackgroundEditHistoryKey() const;
    [[nodiscard]] BackgroundImageEditHistory* currentBackgroundImageEditHistory();
    [[nodiscard]] const BackgroundImageEditHistory* currentBackgroundImageEditHistory() const;
    [[nodiscard]] bool isBackgroundImageTargetActive() const;
    [[nodiscard]] std::optional<std::size_t> currentSpriteIndex() const;
    [[nodiscard]] std::optional<SpriteEditHistoryKey> currentSpriteEditHistoryKey() const;
    [[nodiscard]] SpriteImageEditHistory* currentSpriteImageEditHistory();
    [[nodiscard]] const SpriteImageEditHistory* currentSpriteImageEditHistory() const;
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
    QSpinBox* m_colorRotationLengthSpinBox = nullptr;
    QSpinBox* m_colorRotationDelaySpinBox = nullptr;
    QSpinBox* m_playbackIntervalSpinBox = nullptr;
    QSpinBox* m_dmdPixelSizeSpinBox = nullptr;
    QSpinBox* m_paletteGradientStartSpinBox = nullptr;
    QSpinBox* m_paletteGradientEndSpinBox = nullptr;
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
    QComboBox* m_colorRotationSlotComboBox = nullptr;
    QComboBox* m_paletteComboBox = nullptr;
    QCheckBox* m_switchToMonochromeCheckBox = nullptr;
    QComboBox* m_backgroundComboBox = nullptr;
    QComboBox* m_spriteComboBox = nullptr;
    QLineEdit* m_spriteNameLineEdit = nullptr;
    QLineEdit* m_paletteNameLineEdit = nullptr;
    QComboBox* m_spriteSlotComboBox = nullptr;
    QComboBox* m_spriteDetectionAreaComboBox = nullptr;
    QLabel* m_spriteOriginalPreviewLabel = nullptr;
    QLabel* m_spriteColorizedPreviewLabel = nullptr;
    QLabel* m_backgroundPreviewLabel = nullptr;
    QLabel* m_selectedMaskLabel = nullptr;
    QLabel* m_selectedBackgroundLabel = nullptr;
    QLabel* m_selectedSpriteLabel = nullptr;
    QLabel* m_selectedPaletteLabel = nullptr;
    QLabel* m_frameSpritesLabel = nullptr;
    std::vector<QToolButton*> m_toolButtons;
    std::vector<QToolButton*> m_colorButtons;
    std::vector<uint16_t> m_favoriteColors;
    std::vector<QToolButton*> m_dynamicColorButtons;
    std::vector<QToolButton*> m_dynamicShadowDirectionButtons;
    QToolButton* m_dynamicShadowColorButton = nullptr;
    QToolButton* m_clearColorRotationSlotButton = nullptr;
    QToolButton* m_applyPaletteGradientButton = nullptr;
    ColorGridWidget* m_colorRotationGrid = nullptr;
    ColorGridWidget* m_paletteGrid = nullptr;
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
    std::map<EditHistoryKey, EditHistory> m_editHistories;
    std::map<std::size_t, MaskEditHistory> m_maskEditHistories;
    std::map<EditHistoryKey, DynamicMaskEditHistory> m_dynamicMaskEditHistories;
    std::map<EditHistoryKey, DynamicColorEditHistory> m_dynamicColorEditHistories;
    std::map<EditHistoryKey, DynamicShadowEditHistory> m_dynamicShadowEditHistories;
    std::map<EditHistoryKey, BackgroundMaskEditHistory> m_backgroundMaskEditHistories;
    std::map<BackgroundEditHistoryKey, BackgroundImageEditHistory> m_backgroundImageEditHistories;
    std::map<SpriteEditHistoryKey, SpriteImageEditHistory> m_spriteImageEditHistories;
    std::map<std::size_t, PaletteEditHistory> m_paletteEditHistories;
    QString m_currentCromPath;
    QString m_currentCrpPath;
    std::optional<document::ProjectDocument> m_document;
};

}  // namespace colorizing::app
