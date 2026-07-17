#pragma once

#include <memory>
#include <optional>

#include <QMainWindow>

#include "document/ProjectDocument.h"

namespace colorizing::app {
class FramePreviewWidget;
}

class QLabel;
class QListWidget;
class QToolButton;

namespace colorizing::app {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void createActions();
    void createCentralView();
    void openProject();
    void loadProject(const QString& cromPath, const QString& crpPath);
    void setStatusText(const QString& text);
    void setFrameStatusText(const QString& text);
    void refreshProjectSummary();
    void selectFrame(int frameIndex);
    void updateResolutionMode();

    QLabel* m_statusLabel = nullptr;
    QLabel* m_frameStatusLabel = nullptr;
    QListWidget* m_frameList = nullptr;
    QToolButton* m_sdButton = nullptr;
    QToolButton* m_hdButton = nullptr;
    FramePreviewWidget* m_preview = nullptr;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    std::optional<document::ProjectDocument> m_document;
};

}  // namespace colorizing::app
