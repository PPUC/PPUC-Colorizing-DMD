#pragma once

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <QMainWindow>
#include <QString>

#include "document/ProjectDocument.h"

namespace colorizing::app {
class FramePreviewWidget;
}

class QLabel;
class QListWidget;
class QToolButton;
class QAction;
class QMenu;

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

    void createActions();
    void createCentralView();
    void openProject();
    void openRecentProject(const QString& entry);
    void saveProject();
    void loadProject(const QString& cromPath, const QString& crpPath);
    void rememberRecentProject(const QString& cromPath, const QString& crpPath);
    void refreshRecentProjectsMenu();
    void setStatusText(const QString& text);
    void setFrameStatusText(const QString& text);
    void refreshProjectSummary();
    void selectFrame(int frameIndex);
    void updateResolutionMode();
    void setDrawingTool(DrawingTool tool);
    void setActiveDrawColor(uint16_t rgb565);
    void paintFramePixel(const render::CanvasHit& hit);
    void pickFrameColor(const render::CanvasHit& hit);
    void floodFillFrame(const render::CanvasHit& hit);
    void paintComparisonMaskPixel(const render::CanvasHit& hit);
    void floodFillComparisonMask(const render::CanvasHit& hit);
    void previewFrameStroke(const render::CanvasHit& start, const render::CanvasHit& end);
    void paintFrameStroke(const render::CanvasHit& start, const render::CanvasHit& end);
    [[nodiscard]] std::vector<render::Point> pixelsForStroke(const render::CanvasHit& start,
                                                             const render::CanvasHit& end) const;
    void commitPixels(render::Resolution resolution,
                      const std::vector<render::Point>& pixels,
                      const QString& statusText);
    void commitMaskPixels(const std::vector<render::Point>& pixels,
                          uint8_t value,
                          const QString& statusText);
    void applyPixelEdit(const PixelEditCommand& command, bool useNewColor);
    void applyMaskEdit(const MaskEditCommand& command, bool useNewValue);
    [[nodiscard]] std::optional<EditHistoryKey> currentEditHistoryKey() const;
    [[nodiscard]] EditHistory* currentEditHistory();
    [[nodiscard]] const EditHistory* currentEditHistory() const;
    [[nodiscard]] std::optional<std::size_t> currentComparisonMaskIndex() const;
    [[nodiscard]] MaskEditHistory* currentMaskEditHistory();
    [[nodiscard]] const MaskEditHistory* currentMaskEditHistory() const;
    [[nodiscard]] bool isComparisonMaskModeActive() const;
    void undo();
    void redo();
    void updateUndoRedoActions();
    void updateProjectHeaderStatus();
    void updateFrameThumbnail(std::size_t frameIndex);

    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_frameStatusLabel = nullptr;
    QListWidget* m_frameList = nullptr;
    QToolButton* m_sdButton = nullptr;
    QToolButton* m_hdButton = nullptr;
    QToolButton* m_comparisonMaskOverlayButton = nullptr;
    std::vector<QToolButton*> m_toolButtons;
    std::vector<QToolButton*> m_colorButtons;
    FramePreviewWidget* m_preview = nullptr;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    DrawingTool m_drawingTool = DrawingTool::Point;
    uint16_t m_activeDrawColor = 0xffff;
    std::map<EditHistoryKey, EditHistory> m_editHistories;
    std::map<std::size_t, MaskEditHistory> m_maskEditHistories;
    QString m_currentCromPath;
    QString m_currentCrpPath;
    std::optional<document::ProjectDocument> m_document;
};

}  // namespace colorizing::app
