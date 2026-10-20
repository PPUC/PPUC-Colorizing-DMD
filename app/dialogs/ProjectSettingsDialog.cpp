#include "dialogs/ProjectSettingsDialog.h"

#include <QDialogButtonBox>
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

ProjectSettingsPanel* ProjectSettingsDialog::panel() const
{
    return m_panel;
}

}  // namespace colorizing::app
