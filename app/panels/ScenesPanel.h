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

// The Scenes section of the left sidebar: the project's scenes, with buttons to add, edit and
// delete them and to list a scene's frames in the strip. It shows them and reports what was
// asked; the owner changes the project (undoably) and calls setProject().
class ScenesPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ScenesPanel)

public:
    explicit ScenesPanel(QWidget* parent = nullptr);

    // nullptr without a project.
    void setProject(const document::ProjectDocument* document);
    [[nodiscard]] std::optional<uint16_t> selectedScene() const;
    // "Used by the current frame": only the scene given (none: no scene) is listed; filtered
    // false: every scene.
    void showOnlyScene(bool filtered, std::optional<uint16_t> sceneId);

    std::function<void()> onAddRequested;
    std::function<void(uint16_t sceneId)> onEditRequested;
    std::function<void(uint16_t sceneId)> onDeleteRequested;
    std::function<void(uint16_t sceneId)> onShowFramesRequested;
    // Another scene selected, or the list clicked (the section is the one in use).
    std::function<void()> onSelectionChanged;
    std::function<void()> onPressed;

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
