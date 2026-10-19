#include "panels/HelperLinesPanel.h"

#include <QGridLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "PlaneLabels.h"
#include "document/ProjectDocument.h"

namespace colorizing::app {

HelperLinesPanel::HelperLinesPanel(QWidget* parent)
    : QWidget(parent)
{
    m_list = new QListWidget(this);
    m_list->setToolTip(tr("Checked lines are shown on the canvas, on every frame; double-click "
                          "to edit one"));
    m_showButton = new QPushButton(tr("Show Lines"), this);
    m_showButton->setCheckable(true);
    m_showButton->setChecked(true);
    m_showButton->setToolTip(tr("Show the checked lines on the canvas, or hide them all; their "
                                "check boxes stay as they are"));
    m_addHorizontalButton = new QPushButton(tr("Horizontal"), this);
    m_addHorizontalButton->setToolTip(
        tr("Add a horizontal line, at the row the mouse last pointed at on the canvas"));
    m_addVerticalButton = new QPushButton(tr("Vertical"), this);
    m_addVerticalButton->setToolTip(
        tr("Add a vertical line, at the column the mouse last pointed at on the canvas"));
    m_editButton = new QPushButton(tr("Edit"), this);
    m_editButton->setToolTip(tr("Rename or move the selected line"));
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_deleteButton->setToolTip(tr("Delete the selected line"));
    auto* buttons = new QGridLayout;
    buttons->addWidget(m_addHorizontalButton, 0, 0);
    buttons->addWidget(m_addVerticalButton, 0, 1);
    buttons->addWidget(m_editButton, 1, 0);
    buttons->addWidget(m_deleteButton, 1, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_showButton);
    layout->addLayout(buttons);
    layout->addWidget(m_list, 1);

    connect(m_showButton, &QPushButton::toggled, this, [this](bool shown) {
        if (onLinesShownChanged) {
            onLinesShownChanged(shown);
        }
    });
    connect(m_addHorizontalButton, &QPushButton::clicked, this, [this]() {
        if (onAddRequested) {
            onAddRequested(false);
        }
    });
    connect(m_addVerticalButton, &QPushButton::clicked, this, [this]() {
        if (onAddRequested) {
            onAddRequested(true);
        }
    });
    connect(m_editButton, &QPushButton::clicked, this, [this]() {
        if (const auto line = selectedLine(); line && onEditRequested) {
            onEditRequested(*line);
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        if (const auto line = selectedLine(); line && onEditRequested) {
            onEditRequested(*line);
        }
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        if (const auto line = selectedLine(); line && onDeleteRequested) {
            onDeleteRequested(*line);
        }
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!m_filling && onVisibilityChanged) {
            onVisibilityChanged(static_cast<std::size_t>(m_list->row(item)),
                                item->checkState() == Qt::Checked);
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { updateButtons(); });
    updateButtons();
}

void HelperLinesPanel::setLines(const std::vector<document::HelperLine>& lines,
                                const document::ProjectDocument* document)
{
    m_hasProject = document != nullptr;
    const int row = m_list->currentRow();
    m_filling = true;
    m_list->clear();
    for (const document::HelperLine& line : lines) {
        const QString plane = document ? planeLabel(*document, line.plane) : QString();
        const QString where =
            line.vertical ? tr("vertical at %1 (%2)").arg(line.position).arg(plane)
                          : tr("horizontal at %1 (%2)").arg(line.position).arg(plane);
        auto* item = new QListWidgetItem(
            line.name.empty() ? where
                              : tr("%1 — %2").arg(QString::fromStdString(line.name), where),
            m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(line.visible ? Qt::Checked : Qt::Unchecked);
    }
    m_list->setCurrentRow(std::min(row, m_list->count() - 1));
    m_filling = false;
    updateButtons();
}

void HelperLinesPanel::setLinesShown(bool shown)
{
    const QSignalBlocker blocker(m_showButton);
    m_showButton->setChecked(shown);
}

std::optional<std::size_t> HelperLinesPanel::selectedLine() const
{
    const int row = m_list->currentRow();
    return row >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(row)) : std::nullopt;
}

void HelperLinesPanel::updateButtons()
{
    m_addHorizontalButton->setEnabled(m_hasProject);
    m_addVerticalButton->setEnabled(m_hasProject);
    m_editButton->setEnabled(selectedLine().has_value());
    m_deleteButton->setEnabled(selectedLine().has_value());
}

}  // namespace colorizing::app
