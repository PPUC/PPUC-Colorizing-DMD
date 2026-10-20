#include "panels/ScenesPanel.h"

#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "document/ProjectDocument.h"
#include "edit/SceneEdits.h"
#include "runtime/Scenes.h"

namespace colorizing::app {

ScenesPanel::ScenesPanel(QWidget* parent)
    : QWidget(parent)
{
    m_list = new QListWidget(this);
    m_list->setToolTip(tr("The scenes libserum plays when their PUP trigger fires; double-click "
                          "to edit one"));
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_addButton = new QPushButton(tr("Add"), this);
    m_addButton->setToolTip(tr("Add a scene with a frame for each of its frames, behind the last "
                               "frame"));
    m_editButton = new QPushButton(tr("Edit"), this);
    m_editButton->setToolTip(tr("Change the selected scene; its frames follow"));
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_deleteButton->setToolTip(tr("Delete the selected scene"));
    m_showFramesButton = new QPushButton(tr("Show Frames"), this);
    m_showFramesButton->setToolTip(tr("List the frames of the selected scene in the strip"));
    auto* buttons = new QGridLayout;
    buttons->addWidget(m_addButton, 0, 0);
    buttons->addWidget(m_editButton, 0, 1);
    buttons->addWidget(m_deleteButton, 1, 0);
    buttons->addWidget(m_showFramesButton, 1, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_note);

    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        if (onAddRequested) {
            onAddRequested();
        }
    });
    const auto edit = [this]() {
        if (const auto scene = selectedScene(); scene && onEditRequested) {
            onEditRequested(*scene);
        }
    };
    connect(m_editButton, &QPushButton::clicked, this, edit);
    connect(m_list, &QListWidget::itemDoubleClicked, this, edit);
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        if (const auto scene = selectedScene(); scene && onDeleteRequested) {
            onDeleteRequested(*scene);
        }
    });
    connect(m_showFramesButton, &QPushButton::clicked, this, [this]() {
        if (const auto scene = selectedScene(); scene && onShowFramesRequested) {
            onShowFramesRequested(*scene);
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) {
        updateButtons();
        if (!m_filling && onSelectionChanged) {
            onSelectionChanged();
        }
    });
    connect(m_list, &QListWidget::itemPressed, this, [this]() {
        if (onPressed) {
            onPressed();
        }
    });
    updateButtons();
}

void ScenesPanel::setProject(const document::ProjectDocument* document)
{
    m_hasProject = document != nullptr;
    m_supportsScenes = document && runtime::supportsScenes(document->geometry());
    const std::optional<uint16_t> selected = selectedScene();
    m_filling = true;
    m_list->clear();
    const document::ColorizationSettings settings =
        document ? document->colorizationSettings().value_or(document::ColorizationSettings{})
                 : document::ColorizationSettings{};
    int selectedRow = -1;
    for (const document::SceneDefinition& scene : settings.scenes) {
        const int frames = document ? static_cast<int>(
                                          edit::sceneFrames(*document, scene.sceneId).size())
                                    : 0;
        QString text = scene.frameGroups > 1
                           ? tr("%1 — %2 × %3 frames, %4 ms")
                                 .arg(scene.sceneId)
                                 .arg(scene.frameGroups)
                                 .arg(scene.frameCount)
                                 .arg(scene.durationMs)
                           : tr("%1 — %2 frames, %3 ms")
                                 .arg(scene.sceneId)
                                 .arg(scene.frameCount)
                                 .arg(scene.durationMs);
        if (scene.options & document::kSceneAsBackground) {
            text += tr(", background");
        }
        if (frames < static_cast<int>(scene.totalFrames())) {
            text += tr(" (%1 missing)").arg(static_cast<int>(scene.totalFrames()) - frames);
        }
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, static_cast<int>(scene.sceneId));
        if (selected && *selected == scene.sceneId) {
            selectedRow = m_list->count() - 1;
        }
    }
    m_list->setCurrentRow(selectedRow);
    m_note->setText(m_hasProject && !m_supportsScenes
                        ? tr("libserum plays scenes in projects with 128x32 ROM frames only.")
                        : QString());
    m_note->setVisible(!m_note->text().isEmpty());
    m_filling = false;
    updateButtons();
}

std::optional<uint16_t> ScenesPanel::selectedScene() const
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(item->data(Qt::UserRole).toInt());
}

void ScenesPanel::showOnlyScene(bool filtered, std::optional<uint16_t> sceneId)
{
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        item->setHidden(filtered &&
                        (!sceneId || item->data(Qt::UserRole).toInt() != static_cast<int>(*sceneId)));
    }
}

void ScenesPanel::updateButtons()
{
    const bool selected = selectedScene().has_value();
    m_addButton->setEnabled(m_supportsScenes);
    m_editButton->setEnabled(selected);
    m_deleteButton->setEnabled(selected);
    m_showFramesButton->setEnabled(selected);
}

}  // namespace colorizing::app
