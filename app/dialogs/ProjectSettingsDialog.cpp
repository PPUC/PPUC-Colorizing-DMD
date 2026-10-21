#include "dialogs/ProjectSettingsDialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "panels/ProjectSettingsPanel.h"

namespace colorizing::app {

ProjectSettingsDialog::ProjectSettingsDialog(const document::ProjectDocument& document,
                                             QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Project Settings"));
    setModal(true);
    m_panel = new ProjectSettingsPanel(this);
    m_panel->setProject(&document);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    m_panel->onTargetChanged = [this, ok](document::LibserumTarget) {
        ok->setEnabled(m_panel->targetFits());
    };
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_panel);
    layout->addStretch(1);
    layout->addWidget(buttons);
    setMinimumWidth(360);
}

uint8_t ProjectSettingsDialog::scalingAlgorithm() const
{
    return m_panel->scalingAlgorithm();
}

uint8_t ProjectSettingsDialog::shadowOffset() const
{
    return m_panel->shadowOffset();
}

document::LibserumTarget ProjectSettingsDialog::libserumTarget() const
{
    return m_panel->libserumTarget();
}

ProjectSettingsPanel* ProjectSettingsDialog::panel() const
{
    return m_panel;
}

}  // namespace colorizing::app
