#include "panels/ColorRotationPanel.h"

#include <algorithm>
#include <vector>

#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "ColorGridWidget.h"
#include "PlaneLabels.h"

namespace colorizing::app {

ColorRotationPanel::ColorRotationPanel(QWidget* parent)
    : QWidget(parent)
{
    m_slotComboBox = new QComboBox(this);
    m_slotComboBox->setToolTip(QStringLiteral("Color rotation slot for the active SD/HD mode"));
    for (std::size_t slotIndex = 0; slotIndex < document::kMaxColorRotations; ++slotIndex) {
        m_slotComboBox->addItem(QStringLiteral("Slot %1").arg(static_cast<int>(slotIndex)),
                                static_cast<int>(slotIndex));
    }
    m_slotComboBox->setEnabled(false);
    m_lengthSpinBox = new QSpinBox(this);
    m_lengthSpinBox->setToolTip(QStringLiteral("Number of colors in this rotation"));
    m_lengthSpinBox->setRange(0, static_cast<int>(document::kMaxColorRotationColors));
    m_lengthSpinBox->setEnabled(false);
    // A typed value is one edit (and one undo step) when it is confirmed, not one per digit.
    m_lengthSpinBox->setKeyboardTracking(false);
    m_delaySpinBox = new QSpinBox(this);
    m_delaySpinBox->setToolTip(QStringLiteral("Delay between rotation steps"));
    m_delaySpinBox->setRange(0, 2048);
    m_delaySpinBox->setSingleStep(10);
    m_delaySpinBox->setSuffix(QStringLiteral(" ms"));
    m_delaySpinBox->setKeyboardTracking(false);
    m_delaySpinBox->setEnabled(false);
    m_grid = new ColorGridWidget(document::kMaxColorRotationColors, 8, this);
    m_grid->setToolTip(QStringLiteral("Colors in the selected rotation"));
    m_grid->setColorClickedHandler([this](std::size_t colorIndex) {
        if (onColorClicked) {
            onColorClicked(selectedSlotIndex(), colorIndex);
        }
    });
    m_clearSlotButton = new QToolButton(this);
    m_clearSlotButton->setText(QStringLiteral("Clear Slot"));
    m_clearSlotButton->setToolTip(QStringLiteral("Clear the selected color rotation slot"));
    m_clearSlotButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Slot"), m_slotComboBox);
    form->addRow(QStringLiteral("Length"), m_lengthSpinBox);
    form->addRow(QStringLiteral("Delay"), m_delaySpinBox);
    pageLayout->addLayout(form);
    pageLayout->addWidget(m_grid);
    pageLayout->addWidget(m_clearSlotButton);
    pageLayout->addStretch(1);

    connect(m_slotComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onSlotSelectionChanged) {
                    onSlotSelectionChanged();
                }
            });
    connect(m_lengthSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int length) {
                if (onLengthEdited) {
                    onLengthEdited(selectedSlotIndex(), length);
                }
            });
    connect(m_delaySpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int delayMs) {
                if (onDelayEdited) {
                    onDelayEdited(selectedSlotIndex(), delayMs);
                }
            });
    connect(m_clearSlotButton, &QToolButton::clicked, this, [this]() {
        if (onClearSlotRequested) {
            onClearSlotRequested(selectedSlotIndex());
        }
    });
}

void ColorRotationPanel::refresh(const document::ProjectDocument* document,
                                 std::optional<std::size_t> frameIndex,
                                 render::Resolution resolution)
{
    const bool hasFrame = document && frameIndex.has_value();
    m_slotComboBox->setToolTip(hasHdAsExtraResolution(document)
                                   ? QStringLiteral("Color rotation slot for the active SD/HD mode")
                                   : QStringLiteral("Color rotation slot"));
    QSignalBlocker slotBlocker(m_slotComboBox);
    QSignalBlocker lengthBlocker(m_lengthSpinBox);
    QSignalBlocker delayBlocker(m_delaySpinBox);

    m_slotComboBox->setEnabled(hasFrame);
    m_lengthSpinBox->setEnabled(hasFrame);
    m_delaySpinBox->setEnabled(hasFrame);
    m_clearSlotButton->setEnabled(hasFrame);
    if (!hasFrame) {
        m_lengthSpinBox->setValue(0);
        m_delaySpinBox->setValue(0);
        m_grid->setColors({}, 0);
        return;
    }

    const std::size_t slotIndex = selectedSlotIndex();
    const uint16_t length = document->colorRotationLength(*frameIndex, resolution, slotIndex);
    const uint16_t delayMs = document->colorRotationDelayMs(*frameIndex, resolution, slotIndex);
    m_lengthSpinBox->setValue(static_cast<int>(length));
    m_delaySpinBox->setValue(static_cast<int>(delayMs));

    const document::CompactArray<uint16_t>& rotations = document->colorRotations(*frameIndex, resolution);
    const std::size_t slotOffset = slotIndex * document::kMaxColorRotationLength + 2;
    std::vector<uint16_t> colors(document::kMaxColorRotationColors, 0);
    for (std::size_t colorIndex = 0; colorIndex < colors.size(); ++colorIndex) {
        colors[colorIndex] = rotations[slotOffset + colorIndex];
    }
    m_grid->setColors(colors, length);
}

std::size_t ColorRotationPanel::selectedSlotIndex() const
{
    return static_cast<std::size_t>(std::max(0, m_slotComboBox->currentData().toInt()));
}

}  // namespace colorizing::app
