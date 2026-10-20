#include "panels/SceneSettingsPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "panels/SceneSettingsForm.h"

namespace colorizing::app {

SceneSettingsPanel::SceneSettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    m_titleLabel = new QLabel(this);
    m_titleLabel->setWordWrap(true);
    m_form = new SceneSettingsForm(this);
    m_form->onChanged = [this]() {
        m_changed = true;
        updateState();
    };
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setToolTip(
        tr("Change the scene as the fields say; its frames follow (Undo goes back)"));
    m_revertButton = new QPushButton(tr("Revert"), this);
    m_revertButton->setToolTip(tr("Show the scene's settings again, without the changes"));
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        if (m_scene && onApplyRequested) {
            onApplyRequested(m_scene->sceneId, m_form->scene());
        }
    });
    connect(m_revertButton, &QPushButton::clicked, this, [this]() {
        setScene(m_scene, m_problem);
    });
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_applyButton);
    buttons->addWidget(m_revertButton);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_form);
    layout->addLayout(buttons);
    layout->addStretch(1);
    setScene(std::nullopt, nullptr);
}

void SceneSettingsPanel::setScene(
    const std::optional<document::SceneDefinition>& scene,
    std::function<std::string(const document::SceneDefinition&)> problem)
{
    m_scene = scene;
    m_problem = std::move(problem);
    m_form->setScene(scene.value_or(document::SceneDefinition{}), m_problem);
    m_changed = false;
    m_titleLabel->setText(scene ? tr("Scene %1").arg(scene->sceneId) : tr("No scene"));
    updateState();
}

std::optional<uint16_t> SceneSettingsPanel::sceneId() const
{
    return m_scene ? std::optional<uint16_t>(m_scene->sceneId) : std::nullopt;
}

void SceneSettingsPanel::updateState()
{
    m_form->setEnabled(m_scene.has_value());
    m_applyButton->setEnabled(m_scene && m_changed && m_form->problem().empty());
    m_revertButton->setEnabled(m_scene && m_changed);
}

}  // namespace colorizing::app
