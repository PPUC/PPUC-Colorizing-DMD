#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>

class QLabel;
class QListWidget;
class QPushButton;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

// The Scenes section of the inspector: the project's scenes, with buttons to add, edit and
// delete them and to list a scene's frames in the strip. It shows them and reports what was
// asked; the owner changes the project (undoably) and calls setProject().
class ScenesPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ScenesPanel)

public:
    explicit ScenesPanel(QWidget* parent = nullptr);

    // nullptr without a project.
    void setProject(const document::ProjectDocument* document);
    [[nodiscard]] std::optional<uint16_t> selectedScene() const;

    std::function<void()> onAddRequested;
    std::function<void(uint16_t sceneId)> onEditRequested;
    std::function<void(uint16_t sceneId)> onDeleteRequested;
    std::function<void(uint16_t sceneId)> onShowFramesRequested;

private:
    void updateButtons();

    QListWidget* m_list = nullptr;
    QLabel* m_note = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_showFramesButton = nullptr;
    bool m_hasProject = false;
    bool m_supportsScenes = false;
    bool m_filling = false;
};

}  // namespace colorizing::app
