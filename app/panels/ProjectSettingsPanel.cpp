#include "panels/ProjectSettingsPanel.h"

#include <QComboBox>
#include <QFormLayout>

#include <algorithm>

#include "document/ProjectDocument.h"

namespace colorizing::app {

ProjectSettingsPanel::ProjectSettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    m_scalingComboBox = new QComboBox(this);
    m_scalingComboBox->addItem(tr("Scale2x"), document::kScalingScale2x);
    m_scalingComboBox->addItem(tr("Line doubling"), document::kScalingLineDoubling);
    m_scalingComboBox->setToolTip(
        tr("How libserum makes the 256x64 picture of a frame that has none"));
    m_shadowComboBox = new QComboBox(this);
    m_shadowComboBox->addItem(tr("1 pixel (native)"), document::kShadowOffsetNative);
    m_shadowComboBox->addItem(tr("2 pixels (proportional)"),
                              document::kShadowOffsetProportional);
    m_shadowComboBox->setToolTip(tr("How far a dynamic shadow falls on the 256x64 plane"));
    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addRow(tr("Upscaling"), m_scalingComboBox);
    layout->addRow(tr("Upscaled shadow offset"), m_shadowComboBox);

    const auto changed = [this](int) {
        if (!m_filling && onScalingChanged) {
            onScalingChanged(static_cast<uint8_t>(m_scalingComboBox->currentData().toInt()),
                             static_cast<uint8_t>(m_shadowComboBox->currentData().toInt()));
        }
    };
    connect(m_scalingComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            changed);
    connect(m_shadowComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            changed);
    setProject(nullptr);
}

void ProjectSettingsPanel::setProject(const document::ProjectDocument* document)
{
    const document::ColorizationSettings settings =
        document ? document->colorizationSettings().value_or(document::ColorizationSettings{})
                 : document::ColorizationSettings{};
    m_filling = true;
    m_scalingComboBox->setCurrentIndex(
        std::max(0, m_scalingComboBox->findData(settings.scalingAlgorithm)));
    m_shadowComboBox->setCurrentIndex(
        std::max(0, m_shadowComboBox->findData(settings.shadowOffset)));
    m_filling = false;
    m_scalingComboBox->setEnabled(document != nullptr);
    m_shadowComboBox->setEnabled(document != nullptr);
}

}  // namespace colorizing::app
