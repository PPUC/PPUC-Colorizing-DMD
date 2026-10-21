#include "panels/LightEffectsPanel.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

#include "ThumbnailCaptions.h"
#include "document/ProjectDocument.h"
#include "runtime/SpeakerLightPlayer.h"

namespace colorizing::app {

namespace {

constexpr int kEffectIdRole = Qt::UserRole + 1;

// The effect's three colors side by side.
QIcon colorSwatches(const document::LightEffect& effect)
{
    QPixmap pixmap(48, 16);
    pixmap.fill(Qt::black);
    QPainter painter(&pixmap);
    const std::size_t shown = effect.mode == document::kLightModeStatic ? 1 : 3;
    for (std::size_t i = 0; i < shown; ++i) {
        const uint32_t color = effect.colors[i];
        const int white = static_cast<int>((color >> 24) & 0xff);
        painter.fillRect(static_cast<int>(i) * 16 + 1, 1, 14, 14,
                         QColor(std::min(255, static_cast<int>((color >> 16) & 0xff) + white),
                                std::min(255, static_cast<int>((color >> 8) & 0xff) + white),
                                std::min(255, static_cast<int>(color & 0xff) + white)));
    }
    return QIcon(pixmap);
}

}  // namespace

LightEffectsPanel::LightEffectsPanel(QWidget* parent)
    : QWidget(parent)
{
    m_list = new QListWidget(this);
    m_list->setToolTip(tr("The speaker light effects frames can start; the selected one is edited "
                          "in the Light Effect section"));
    m_list->setIconSize(QSize(48, 16));
    showCaptionsBelowThumbnails(m_list);
    m_addButton = new QToolButton(this);
    m_addButton->setText(tr("New"));
    m_addButton->setToolTip(tr("Add a speaker light effect"));
    m_deleteButton = new QToolButton(this);
    m_deleteButton->setText(tr("Delete"));
    m_deleteButton->setToolTip(tr("Delete the selected effect; the frames that start it leave "
                                  "that channel unchanged"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_deleteButton);
    buttons->addStretch(1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(m_list, 1);

    connect(m_addButton, &QToolButton::clicked, this, [this]() {
        if (onAddRequested) {
            onAddRequested();
        }
    });
    connect(m_deleteButton, &QToolButton::clicked, this, [this]() {
        if (const auto effect = selectedEffect(); effect && onDeleteRequested) {
            onDeleteRequested(*effect);
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

void LightEffectsPanel::setProject(const document::ProjectDocument* document)
{
    m_hasProject = document != nullptr;
    const std::optional<uint16_t> selected = selectedEffect();
    m_filling = true;
    m_list->clear();
    int selectedRow = -1;
    if (document) {
        for (const document::LightEffect& effect : document->speakerLights().effects) {
            const std::size_t frames = document->framesUsingLightEffect(effect.id).size();
            auto* item = new QListWidgetItem(
                colorSwatches(effect),
                tr("%1\n%2, %n frame(s)", "", static_cast<int>(frames))
                    .arg(QString::fromStdString(effect.name),
                         QString::fromStdString(runtime::lightModeName(effect.mode))),
                m_list);
            item->setData(kEffectIdRole, effect.id);
            if (selected == effect.id) {
                selectedRow = m_list->count() - 1;
            }
        }
    }
    m_list->setCurrentRow(selectedRow);
    m_filling = false;
    updateButtons();
    // The selection may be gone with the effect.
    if (selected != selectedEffect() && onSelectionChanged) {
        onSelectionChanged();
    }
}

std::optional<uint16_t> LightEffectsPanel::selectedEffect() const
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(item->data(kEffectIdRole).toInt());
}

void LightEffectsPanel::selectEffect(uint16_t effectId)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(kEffectIdRole).toInt() == effectId) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

void LightEffectsPanel::updateButtons()
{
    m_addButton->setEnabled(m_hasProject);
    m_deleteButton->setEnabled(m_hasProject && selectedEffect().has_value());
}

}  // namespace colorizing::app
