#include "dialogs/SceneDialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "panels/SceneSettingsForm.h"

namespace colorizing::app {

SceneDialog::SceneDialog(const document::SceneDefinition& scene,
                         std::function<std::string(const document::SceneDefinition&)> problem,
                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Scene"));
    m_form = new SceneSettingsForm(this);
    m_form->setScene(scene, std::move(problem));
    m_form->onChanged = [this]() { updateState(); };
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_form);
    layout->addWidget(m_buttons);
    updateState();
}

void SceneDialog::updateState()
{
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_form->problem().empty());
}

document::SceneDefinition SceneDialog::scene() const
{
    return m_form->scene();
}

}  // namespace colorizing::app
