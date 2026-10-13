#include "panels/RecordListPage.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace colorizing::app {

RecordListPage::RecordListPage(QListWidget* list,
                               const QString& newToolTip,
                               const QString& deleteToolTip,
                               QWidget* parent)
    : QWidget(parent)
    , m_list(list)
{
    m_newButton = new QToolButton(this);
    m_newButton->setText(QStringLiteral("New"));
    m_newButton->setToolTip(newToolTip);
    m_newButton->setEnabled(false);
    m_deleteButton = new QToolButton(this);
    m_deleteButton->setText(QStringLiteral("Delete"));
    m_deleteButton->setToolTip(deleteToolTip);
    m_deleteButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_newButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch(1);
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
}

std::vector<QWidget*> RecordListPage::tabOrder() const
{
    return {m_newButton, m_deleteButton, m_list};
}

}  // namespace colorizing::app
