#include "panels/ScenesPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

#include "FrameImageUtils.h"
#include "LazyListIcons.h"
#include "ThumbnailCaptions.h"
#include "document/ProjectDocument.h"
#include "edit/SceneEdits.h"
#include "runtime/Scenes.h"

namespace colorizing::app {

namespace {

constexpr QSize kThumbnailSize{160, 42};
// The first frame of the scene, for its thumbnail; -1 for none.
constexpr int kFirstFrameRole = Qt::UserRole + 1;

QIcon blankThumbnail()
{
    QPixmap pixmap(kThumbnailSize.width(), kThumbnailSize.width() / 4);
    pixmap.fill(Qt::black);
    return QIcon(pixmap);
}

}  // namespace

ScenesPanel::ScenesPanel(QWidget* parent)
    : QWidget(parent)
{
    m_list = new QListWidget(this);
    m_list->setToolTip(tr("The scenes libserum plays when their PUP trigger fires; double-click "
                          "one to list its frames and show its first frame"));
    m_list->setIconSize(kThumbnailSize);
    showCaptionsBelowThumbnails(m_list);
    m_thumbnails = new LazyListIcons(m_list, blankThumbnail(), [this](const QListWidgetItem& item) {
        const int frame = item.data(kFirstFrameRole).toInt();
        if (!m_document || frame < 0 || static_cast<std::size_t>(frame) >= m_document->frameCount()) {
            return blankThumbnail();
        }
        const QImage image = renderResolvedFrameImage(
            *m_document, static_cast<std::size_t>(frame),
            m_document->rendersHdPlaneOnly() ? render::Resolution::Hd : render::Resolution::Sd,
            1, true);
        return QIcon(QPixmap::fromImage(
            image.scaled(kThumbnailSize, Qt::KeepAspectRatio, Qt::FastTransformation)));
    });
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    // A row of small buttons above the list, as the other sections of the left sidebar have.
    m_addButton = new QToolButton(this);
    m_addButton->setText(tr("New"));
    m_addButton->setToolTip(tr("Add a scene with a frame for each of its frames, behind the last "
                               "frame"));
    m_editButton = new QToolButton(this);
    m_editButton->setText(tr("Edit"));
    m_editButton->setToolTip(tr("Change the selected scene; its frames follow"));
    m_deleteButton = new QToolButton(this);
    m_deleteButton->setText(tr("Delete"));
    m_deleteButton->setToolTip(tr("Delete the selected scene"));
    m_showFramesButton = new QToolButton(this);
    m_showFramesButton->setText(tr("Show Frames"));
    m_showFramesButton->setToolTip(tr("List the frames of the selected scene in the strip"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_editButton);
    buttons->addWidget(m_deleteButton);
    buttons->addWidget(m_showFramesButton);
    buttons->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_note);

    connect(m_addButton, &QToolButton::clicked, this, [this]() {
        if (onAddRequested) {
            onAddRequested();
        }
    });
    const auto edit = [this]() {
        if (const auto scene = selectedScene(); scene && onEditRequested) {
            onEditRequested(*scene);
        }
    };
    connect(m_editButton, &QToolButton::clicked, this, edit);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() {
        if (const auto scene = selectedScene(); scene && onOpenRequested) {
            onOpenRequested(*scene);
        }
    });
    connect(m_deleteButton, &QToolButton::clicked, this, [this]() {
        if (const auto scene = selectedScene(); scene && onDeleteRequested) {
            onDeleteRequested(*scene);
        }
    });
    connect(m_showFramesButton, &QToolButton::clicked, this, [this]() {
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
    m_document = document;
    m_supportsScenes = document && runtime::supportsScenes(document->geometry());
    const std::optional<uint16_t> selected = selectedScene();
    m_filling = true;
    m_list->clear();
    const document::ColorizationSettings settings =
        document ? document->colorizationSettings().value_or(document::ColorizationSettings{})
                 : document::ColorizationSettings{};
    int selectedRow = -1;
    for (const document::SceneDefinition& scene : settings.scenes) {
        const std::vector<std::size_t> sceneFrames =
            document ? edit::sceneFrames(*document, scene.sceneId) : std::vector<std::size_t>{};
        const int frames = static_cast<int>(sceneFrames.size());
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
        auto* item = new QListWidgetItem(m_thumbnails->placeholder(), text, m_list);
        item->setData(Qt::UserRole, static_cast<int>(scene.sceneId));
        item->setData(kFirstFrameRole,
                      sceneFrames.empty() ? -1 : static_cast<int>(sceneFrames.front()));
        if (selected && *selected == scene.sceneId) {
            selectedRow = m_list->count() - 1;
        }
    }
    m_list->setCurrentRow(selectedRow);
    m_thumbnails->scheduleRowsInView();
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

void ScenesPanel::refreshThumbnails()
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_thumbnails->isBuilt(m_list->item(row))) {
            m_thumbnails->invalidate(m_list->item(row));
        }
    }
}

void ScenesPanel::buildAllThumbnails()
{
    m_thumbnails->ensureBuilt(0, m_list->count() - 1);
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
