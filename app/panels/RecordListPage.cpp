#include "panels/RecordListPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace colorizing::app {

QString recordCountText(int shown, int total)
{
    return shown == total ? RecordListPage::tr("Total: %1").arg(total)
                          : RecordListPage::tr("%1 of %2").arg(shown).arg(total);
}

int shownRows(const QListWidget& list)
{
    int shown = 0;
    for (int row = 0; row < list.count(); ++row) {
        shown += list.item(row)->isHidden() ? 0 : 1;
    }
    return shown;
}

RecordListPage::RecordListPage(QListWidget* list,
                               const QString& newToolTip,
                               const QString& deleteToolTip,
                               QWidget* parent)
    : QWidget(parent)
    , m_list(list)
{
    m_newButton = new QToolButton(this);
    m_newButton->setText(tr("New"));
    m_newButton->setToolTip(newToolTip);
    m_newButton->setEnabled(false);
    m_deleteButton = new QToolButton(this);
    m_deleteButton->setText(tr("Delete"));
    m_deleteButton->setToolTip(deleteToolTip);
    m_deleteButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_newButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch(1);
    m_countLabel = new QLabel(this);
    m_countLabel->setToolTip(tr("How many there are in the project; while only those used by "
                                "the current frame are listed, how many of them are"));
    buttonLayout->addWidget(m_countLabel);
    pageLayout->addLayout(buttonLayout);
    pageLayout->addWidget(m_list, 1);

    connect(m_newButton, &QToolButton::clicked, this, [this]() {
        if (onNewRequested) {
            onNewRequested();
        }
    });
    connect(m_deleteButton, &QToolButton::clicked, this, [this]() {
        if (onDeleteRequested) {
            onDeleteRequested();
        }
    });
}

void RecordListPage::refresh(const RecordListPageState& state)
{
    m_newButton->setEnabled(state.hasProject && state.hasSource &&
                            state.recordCount < state.maxRecordCount);
    m_deleteButton->setEnabled(state.hasProject && state.hasSelection);
    m_total = state.recordCount;
    m_hasProject = state.hasProject;
    updateCount();
}

void RecordListPage::updateCount()
{
    m_countLabel->setText(m_hasProject ? recordCountText(std::min(shownRows(*m_list),
                                                                  static_cast<int>(m_total)),
                                                         static_cast<int>(m_total))
                                       : QString());
}

QString RecordListPage::countText() const
{
    return m_countLabel->text();
}

std::vector<QWidget*> RecordListPage::tabOrder() const
{
    return {m_newButton, m_deleteButton, m_list};
}

}  // namespace colorizing::app
