#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>

class QListWidget;
class QToolButton;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

// The Light Effects section of the left sidebar: the project's speaker light effects, with
// buttons to add and delete them. The selected one is edited in the right sidebar's Light
// Effect section. It shows them and reports what was asked; the owner changes the project
// (undoably) and calls setProject().
class LightEffectsPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(LightEffectsPanel)

public:
    explicit LightEffectsPanel(QWidget* parent = nullptr);

    // nullptr without a project.
    void setProject(const document::ProjectDocument* document);
    [[nodiscard]] std::optional<uint16_t> selectedEffect() const;
    // Selects the effect (reported as a selection change); ignored for one it lacks.
    void selectEffect(uint16_t effectId);
    [[nodiscard]] QListWidget* list() const { return m_list; }

    std::function<void()> onAddRequested;
    std::function<void(uint16_t effectId)> onDeleteRequested;
    std::function<void()> onSelectionChanged;
    std::function<void()> onPressed;

private:
    void updateButtons();

    QListWidget* m_list = nullptr;
    QToolButton* m_addButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    bool m_hasProject = false;
    bool m_filling = false;
};

}  // namespace colorizing::app
