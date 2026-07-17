#pragma once

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

namespace colorizing::app {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    struct PointEditCommand {
        std::size_t frameIndex = 0;
        render::Resolution resolution = render::Resolution::Sd;
        int x = 0;
        int y = 0;
        uint16_t oldColor = 0;
        uint16_t newColor = 0;
    };

    void createActions();
    void createCentralView();
    void openProject();
    void saveProject();
    void loadProject(const QString& cromPath, const QString& crpPath);
    void setStatusText(const QString& text);
    void setFrameStatusText(const QString& text);
    void refreshProjectSummary();
    void selectFrame(int frameIndex);
    void updateResolutionMode();
    void setActiveDrawColor(uint16_t rgb565);
    void paintFramePixel(const render::CanvasHit& hit);
    void applyPointEdit(const PointEditCommand& command, bool useNewColor);
    void undo();
    void redo();
    void updateUndoRedoActions();
    void updateProjectHeaderStatus();
    void updateFrameThumbnail(std::size_t frameIndex);

    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_frameStatusLabel = nullptr;
    QListWidget* m_frameList = nullptr;
    QToolButton* m_sdButton = nullptr;
    QToolButton* m_hdButton = nullptr;
    std::vector<QToolButton*> m_colorButtons;
    FramePreviewWidget* m_preview = nullptr;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    uint16_t m_activeDrawColor = 0xffff;
    std::vector<PointEditCommand> m_undoStack;
    std::vector<PointEditCommand> m_redoStack;
    QString m_currentCromPath;
    QString m_currentCrpPath;
    std::optional<document::ProjectDocument> m_document;
};

}  // namespace colorizing::app
