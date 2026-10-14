#include "panels/ColorSetPanel.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "FrameImageUtils.h"
#include "edit/ColorSetEdits.h"

namespace colorizing::app {

namespace {

constexpr int kSwatchHeight = 14;

QIcon colorSetIcon(const std::vector<uint16_t>& colors, uint32_t colorCount)
{
    const int count = static_cast<int>(std::min<std::size_t>(colorCount, colors.size()));
    const int swatchWidth = count > 4 ? 8 : 16;
    QPixmap pixmap(swatchWidth * std::max(count, 1), kSwatchHeight);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    for (int index = 0; index < count; ++index) {
        painter.fillRect(index * swatchWidth, 0, swatchWidth, kSwatchHeight,
                         rgb565ToColor(colors[static_cast<std::size_t>(index)]));
    }
    return QIcon(pixmap);
}

QToolButton* makeButton(QWidget* parent, const QString& text, const QString& tip)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setEnabled(false);
    return button;
}

}  // namespace

ColorSetPanel::ColorSetPanel(QWidget* parent)
    : QWidget(parent)
{
    m_list = new QListWidget(this);
    m_list->setToolTip(QStringLiteral("Color sets; double click loads one into the edit colors"));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    m_list->setIconSize(QSize(128, kSwatchHeight));
    m_list->setUniformItemSizes(true);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setToolTip(QStringLiteral("Name of the selected color set"));
    m_nameEdit->setMaxLength(static_cast<int>(document::kColorSetNameBytes) - 1);
    m_nameEdit->setEnabled(false);
    m_storeButton = makeButton(
        this,
        QStringLiteral("Store"),
        QStringLiteral("Store the edit colors in the selected color set and activate it"));
    m_loadButton = makeButton(this,
                              QStringLiteral("Load"),
                              QStringLiteral("Load the selected color set into the edit colors"));
    m_deactivateButton = makeButton(this,
                                    QStringLiteral("Deactivate"),
                                    QStringLiteral("Deactivate the selected color set"));
    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_nameEdit);
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(m_storeButton);
    buttons->addWidget(m_loadButton);
    buttons->addWidget(m_deactivateButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    layout->addWidget(m_hintLabel);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { updateButtons(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const int row = m_list->row(item);
        if (row >= 0 && onLoadRequested) {
            onLoadRequested(static_cast<std::size_t>(row));
        }
    });
    // Only scrolling by the user counts; a refresh, a resize or a selection that scrolls the
    // list must not change what the project stores.
    connect(m_list->verticalScrollBar(), &QScrollBar::actionTriggered, this, [this](int) {
        m_userScrolling = true;
    });
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        if (m_userScrolling && !m_refreshing && onScrolled) {
            onScrolled(firstVisibleSet());
        }
        m_userScrolling = false;
    });
    connect(m_storeButton, &QToolButton::clicked, this, [this]() {
        if (const std::optional<std::size_t> set = selectedSet(); set && onStoreRequested) {
            onStoreRequested(*set);
        }
    });
    connect(m_loadButton, &QToolButton::clicked, this, [this]() {
        if (const std::optional<std::size_t> set = selectedSet(); set && onLoadRequested) {
            onLoadRequested(*set);
        }
    });
    connect(m_deactivateButton, &QToolButton::clicked, this, [this]() {
        if (const std::optional<std::size_t> set = selectedSet(); set && onDeactivateRequested) {
            onDeactivateRequested(*set);
        }
    });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        const std::optional<std::size_t> set = selectedSet();
        if (m_refreshing || !set || !onNameEdited) {
            return;
        }
        const QListWidgetItem* item = m_list->item(static_cast<int>(*set));
        if (item && item->data(Qt::UserRole).toString() != m_nameEdit->text()) {
            onNameEdited(*set, m_nameEdit->text());
        }
    });
    refresh(nullptr, true);
}

void ColorSetPanel::refresh(const document::ProjectDocument* document, bool projectChanged)
{
    m_refreshing = true;
    const std::optional<std::size_t> previous = selectedSet();
    const int scroll = m_list->verticalScrollBar()->value();
    {
        QSignalBlocker blocker(m_list);
        m_list->clear();
        m_active.assign(document ? document::kMaxColorSets : 0, false);
        if (document) {
            const document::ColorSetTables tables = document->colorSetTables();
            for (std::size_t set = 0; set < document::kMaxColorSets; ++set) {
                const bool active = tables.activeColorSets[set] != 0;
                m_active[set] = active;
                const QString name = QString::fromStdString(edit::colorSetName(tables, set));
                auto* item = new QListWidgetItem(m_list);
                item->setData(Qt::UserRole, name);
                if (active) {
                    item->setText(QStringLiteral("%1  %2")
                                      .arg(static_cast<int>(set), 2, 10, QLatin1Char('0'))
                                      .arg(name));
                    item->setIcon(colorSetIcon(edit::colorSetColors(tables, set),
                                               document->colorCount()));
                } else {
                    item->setText(QStringLiteral("%1  (inactive)")
                                      .arg(static_cast<int>(set), 2, 10, QLatin1Char('0')));
                    item->setForeground(QColor(0x88, 0x88, 0x88));
                }
            }
            const auto& metadata = document->editorMetadata();
            const int row = projectChanged || !previous
                                ? std::min<int>(metadata.currentColorSet,
                                                static_cast<int>(document::kMaxColorSets) - 1)
                                : static_cast<int>(*previous);
            m_list->setCurrentRow(row);
            if (projectChanged) {
                m_list->scrollToItem(m_list->item(metadata.firstDisplayedColorSet),
                                     QAbstractItemView::PositionAtTop);
            } else {
                m_list->verticalScrollBar()->setValue(scroll);
            }
        }
    }
    m_hintLabel->setText(document ? QStringLiteral("Store and Load use the first %1 colors.")
                                        .arg(static_cast<int>(document->colorCount()))
                                  : QString());
    m_refreshing = false;
    updateButtons();
}

std::optional<std::size_t> ColorSetPanel::selectedSet() const
{
    const int row = m_list->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= m_active.size()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(row);
}

void ColorSetPanel::selectSet(std::size_t setIndex)
{
    if (setIndex < m_active.size()) {
        m_list->setCurrentRow(static_cast<int>(setIndex));
    }
}

std::size_t ColorSetPanel::firstVisibleSet() const
{
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, m_list->verticalScrollBar()->value())),
        document::kMaxColorSets - 8);
}

void ColorSetPanel::updateButtons()
{
    const std::optional<std::size_t> set = selectedSet();
    const bool active = set && m_active[*set];
    m_storeButton->setEnabled(set.has_value());
    m_loadButton->setEnabled(active);
    m_deactivateButton->setEnabled(active);
    m_refreshing = true;
    {
        QSignalBlocker blocker(m_nameEdit);
        const QListWidgetItem* item = set ? m_list->item(static_cast<int>(*set)) : nullptr;
        m_nameEdit->setText(active && item ? item->data(Qt::UserRole).toString() : QString());
        m_nameEdit->setEnabled(active);
    }
    m_refreshing = false;
}

}  // namespace colorizing::app
