#include "dialogs/SelectionDialogs.h"

#include <utility>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace colorizing::app {

AutoSelectDialog::AutoSelectDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("autoSelectDialog"));
    setWindowTitle(tr("Auto Select"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_offset = new QSpinBox(this);
    m_offset->setRange(0, 256);
    m_offset->setToolTip(tr("First row or column of the first stripe"));
    m_selected = new QSpinBox(this);
    m_selected->setRange(1, 256);
    m_selected->setToolTip(tr("Rows or columns selected in each stripe"));
    m_skipped = new QSpinBox(this);
    m_skipped->setRange(1, 256);
    m_skipped->setToolTip(tr("Rows or columns left out between two stripes"));
    m_columns = new QCheckBox(tr("Columns instead of rows"), this);
    form->addRow(tr("Offset"), m_offset);
    //: Number of rows or columns selected in each stripe
    form->addRow(tr("Selected"), m_selected);
    //: Number of rows or columns left out between two stripes
    form->addRow(tr("Skipped"), m_skipped);
    form->addRow(m_columns);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Select"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setPattern(edit::StripePattern{});
}

void AutoSelectDialog::setPattern(const edit::StripePattern& pattern)
{
    m_offset->setValue(pattern.offset);
    m_selected->setValue(pattern.selected);
    m_skipped->setValue(pattern.skipped);
    m_columns->setChecked(pattern.columns);
}

edit::StripePattern AutoSelectDialog::pattern() const
{
    return edit::StripePattern{m_offset->value(),
                               m_selected->value(),
                               m_skipped->value(),
                               m_columns->isChecked()};
}

AutoCopyDialog::AutoCopyDialog(bool hasSelectedFrames,
                               std::function<QString(bool selectedFramesOnly)> describe,
                               QWidget* parent)
    : QDialog(parent),
      m_describe(std::move(describe))
{
    setObjectName(QStringLiteral("autoCopyDialog"));
    setWindowTitle(tr("Auto Copy"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Copy the current frame's colorization inside the selection to "
                                "every frame whose original frame shows the same there."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_allFrames = new QRadioButton(tr("All frames"), this);
    m_selectedFrames = new QRadioButton(tr("Selected frames only"), this);
    m_selectedFrames->setEnabled(hasSelectedFrames);
    m_allFrames->setChecked(true);
    layout->addWidget(m_allFrames);
    layout->addWidget(m_selectedFrames);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("autoCopySummary"));
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(m_allFrames, &QRadioButton::toggled, this, [this](bool) { refresh(); });
    refresh();
}

bool AutoCopyDialog::selectedFramesOnly() const
{
    return m_selectedFrames->isChecked();
}

void AutoCopyDialog::refresh()
{
    if (m_describe) {
        m_summary->setText(m_describe(selectedFramesOnly()));
    }
}

}  // namespace colorizing::app
