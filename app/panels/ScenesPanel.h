#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>

class QLabel;
class QListWidgetItem;
class QListWidget;
class QToolButton;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

class LazyListIcons;

// The Scenes section of the left sidebar: the project's scenes, with buttons to add and delete
// them; a double click lists a scene's frames in the strip. The scene of the frame shown is
// edited in the Inspector's Scene section. It shows them and reports what was
// asked; the owner changes the project (undoably) and calls setProject().
class ScenesPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ScenesPanel)

public:
    explicit ScenesPanel(QWidget* parent = nullptr);

    // nullptr without a project.
    void setProject(const document::ProjectDocument* document);
    [[nodiscard]] std::optional<uint16_t> selectedScene() const;
    // Selects the scene in the list (reported as a selection change); ignored for one it lacks.
    void selectScene(uint16_t sceneId);
    // The list a scene is dragged from (onto the canvas, to start it from the frames); the
    // dragged scene is the selected one.
    [[nodiscard]] const QObject* dragSource() const;
    // "Used by the current frame": only the scene given (none: no scene) is listed; filtered
    // false: every scene.
    void showOnlyScene(bool filtered, std::optional<uint16_t> sceneId);
    // The count beside New and Delete (recordCountText()).
    [[nodiscard]] QString countText() const;
    // The thumbnails (each scene's first frame) no longer show the frames: rebuilt when in view.
    void refreshThumbnails();
    // For tests: builds the thumbnails of every row now.
    void buildAllThumbnails();

    std::function<void()> onAddRequested;
    std::function<void(uint16_t sceneId)> onDeleteRequested;
    // A double click: the scene's frames in the strip and its first frame on the canvas.
    std::function<void(uint16_t sceneId)> onOpenRequested;
    // Another scene selected, or the list clicked (the section is the one in use).
    std::function<void()> onSelectionChanged;
    std::function<void()> onPressed;

private:
    void updateButtons();

    QListWidget* m_list = nullptr;
    LazyListIcons* m_thumbnails = nullptr;
    const document::ProjectDocument* m_document = nullptr;
    QLabel* m_note = nullptr;
    QToolButton* m_addButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    bool m_hasProject = false;
    bool m_supportsScenes = false;
    bool m_filling = false;
    void updateCount();
    QLabel* m_countLabel = nullptr;
};

}  // namespace colorizing::app
