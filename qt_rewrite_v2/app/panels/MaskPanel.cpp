#include "panels/MaskPanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace colorizing::app {

namespace {

std::optional<std::size_t> assignedMaskIndex(const document::ProjectDocument* document,
                                             std::optional<std::size_t> frameIndex)
{
    if (!document || !frameIndex) {
        return std::nullopt;
    }
    const uint8_t maskId = document->comparisonMaskId(*frameIndex);
    if (maskId >= document->comparisonMaskCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(maskId);
}

}  // namespace

MaskPanel::MaskPanel(QWidget* parent)
    : QWidget(parent)
{
    m_maskIdSpinBox = new QSpinBox(this);
    m_maskIdSpinBox->setToolTip(QStringLiteral("Assigned comparison mask ID"));
    m_maskIdSpinBox->setMinimum(-1);
    m_maskIdSpinBox->setMaximum(-1);
    m_maskIdSpinBox->setSpecialValueText(QStringLiteral("None"));
    m_maskIdSpinBox->setEnabled(false);
    m_shapeModeComboBox = new QComboBox(this);
    m_shapeModeComboBox->setToolTip(QStringLiteral("Shape comparison mode"));
    m_shapeModeComboBox->setEnabled(false);
    m_assignButton = new QToolButton(this);
    m_assignButton->setText(QStringLiteral("Assign Mask"));
    m_assignButton->setToolTip(
        QStringLiteral("Assign selected comparison mask to selected frames"));
    m_assignButton->setEnabled(false);
    m_editButton = new QToolButton(this);
    m_editButton->setText(QStringLiteral("Edit Mask"));
    m_editButton->setToolTip(QStringLiteral("Show and edit the assigned comparison mask"));
    m_editButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_selectedMaskLabel = new QLabel(QStringLiteral("Selected: None"), this);
    m_selectedMaskLabel->setWordWrap(true);
    pageLayout->addWidget(m_selectedMaskLabel);
    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Mask ID"), m_maskIdSpinBox);
    form->addRow(QStringLiteral("Shape"), m_shapeModeComboBox);
    pageLayout->addLayout(form);
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_assignButton);
    buttonLayout->addWidget(m_editButton);
    pageLayout->addLayout(buttonLayout);
    pageLayout->addStretch(1);

    connect(m_maskIdSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int maskId) {
                if (onMaskIdEdited) {
                    onMaskIdEdited(maskId);
                }
            });
    connect(m_shapeModeComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onShapeModeEdited) {
                    onShapeModeEdited(m_shapeModeComboBox->currentData().toInt());
                }
            });
    connect(m_assignButton, &QToolButton::clicked, this, [this]() {
        if (onAssignRequested) {
            onAssignRequested();
        }
    });
    connect(m_editButton, &QToolButton::clicked, this, [this]() {
        if (onEditRequested) {
            onEditRequested();
        }
    });
}

void MaskPanel::refreshFrame(const document::ProjectDocument* document,
                             std::optional<std::size_t> frameIndex)
{
    QSignalBlocker maskBlocker(m_maskIdSpinBox);
    QSignalBlocker shapeBlocker(m_shapeModeComboBox);

    if (!document || !frameIndex) {
        m_maskIdSpinBox->setRange(-1, -1);
        m_maskIdSpinBox->setValue(-1);
        m_maskIdSpinBox->setEnabled(false);
        m_shapeModeComboBox->clear();
        m_shapeModeComboBox->addItem(QStringLiteral("Full comparison"), 0);
        m_shapeModeComboBox->addItem(QStringLiteral("Shape mode"), 1);
        m_shapeModeComboBox->setCurrentIndex(0);
        m_shapeModeComboBox->setEnabled(false);
        return;
    }

    const int maxMaskId = document->comparisonMaskCount() == 0
                              ? -1
                              : static_cast<int>(document->comparisonMaskCount() - 1);
    m_maskIdSpinBox->setRange(-1, maxMaskId);
    const uint8_t maskId = document->comparisonMaskId(*frameIndex);
    m_maskIdSpinBox->setValue(maskId <= maxMaskId ? static_cast<int>(maskId) : -1);
    m_maskIdSpinBox->setEnabled(maxMaskId >= 0);
    const int shapeMode = static_cast<int>(document->shapeComparisonMode(*frameIndex));
    m_shapeModeComboBox->clear();
    m_shapeModeComboBox->addItem(QStringLiteral("Full comparison"), 0);
    m_shapeModeComboBox->addItem(QStringLiteral("Shape mode"), 1);
    if (shapeMode > 1) {
        m_shapeModeComboBox->addItem(QStringLiteral("Unknown legacy mode %1").arg(shapeMode),
                                     shapeMode);
    }
    const int shapeIndex = m_shapeModeComboBox->findData(shapeMode);
    m_shapeModeComboBox->setCurrentIndex(shapeIndex >= 0 ? shapeIndex : 0);
    m_shapeModeComboBox->setEnabled(true);
}

void MaskPanel::refreshSelection(const document::ProjectDocument* document,
                                 std::optional<std::size_t> frameIndex,
                                 std::optional<std::size_t> selectedMaskIndex,
                                 bool hasSelectedFrames)
{
    const bool selectedMaskValid =
        document && selectedMaskIndex && *selectedMaskIndex < document->comparisonMaskCount();
    const std::optional<std::size_t> assignedMask = assignedMaskIndex(document, frameIndex);

    if (selectedMaskValid) {
        const QString assignedSuffix =
            assignedMask && *assignedMask == *selectedMaskIndex ? QStringLiteral(" (assigned)")
                                                                 : QString();
        m_selectedMaskLabel->setText(QStringLiteral("Selected: %1%2")
                                         .arg(static_cast<int>(*selectedMaskIndex))
                                         .arg(assignedSuffix));
    } else if (assignedMask) {
        m_selectedMaskLabel->setText(
            QStringLiteral("Selected: %1 (assigned)").arg(static_cast<int>(*assignedMask)));
    } else {
        m_selectedMaskLabel->setText(QStringLiteral("Selected: None"));
    }

    m_assignButton->setEnabled(selectedMaskValid && hasSelectedFrames);
    m_editButton->setEnabled(assignedMask.has_value());
}

void MaskPanel::setMaskId(int maskId)
{
    QSignalBlocker blocker(m_maskIdSpinBox);
    m_maskIdSpinBox->setValue(maskId);
}

int MaskPanel::maskId() const
{
    return m_maskIdSpinBox->value();
}

}  // namespace colorizing::app
