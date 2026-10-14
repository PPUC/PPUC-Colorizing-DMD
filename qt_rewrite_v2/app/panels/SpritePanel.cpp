#include "panels/SpritePanel.h"

#include <algorithm>
#include <vector>

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "PlaneLabels.h"
#include "render/CanvasLayout.h"

namespace colorizing::app {

namespace {

QString spriteDisplayName(const document::ProjectDocument& document, std::size_t spriteIndex)
{
    const document::SpriteRecord& sprite = document.sprite(spriteIndex);
    const QString base = QStringLiteral("Sprite %1").arg(static_cast<int>(spriteIndex));
    if (sprite.name.empty()) {
        return base;
    }
    return QStringLiteral("%1 - %2").arg(base, QString::fromStdString(sprite.name));
}

}  // namespace

bool isSpriteDetectionAreaSet(const document::SpriteDetectionArea& area)
{
    return area.x != 0xffff && area.y != 0xffff &&
           area.width != 0xffff && area.height != 0xffff &&
           area.width > 0 && area.height > 0;
}

SpritePanel::SpritePanel(QWidget* parent)
    : QWidget(parent)
{
    m_nameLineEdit = new QLineEdit(this);
    m_nameLineEdit->setToolTip(QStringLiteral("Selected sprite name"));
    m_nameLineEdit->setMaxLength(31);
    m_nameLineEdit->setEnabled(false);
    m_slotComboBox = new QComboBox(this);
    m_slotComboBox->setToolTip(QStringLiteral("Current frame sprite slot"));
    m_slotComboBox->setEnabled(false);
    m_assignButton = new QToolButton(this);
    m_assignButton->setText(QStringLiteral("Assign"));
    m_assignButton->setToolTip(QStringLiteral("Assign selected sprite to selected slot"));
    m_assignButton->setEnabled(false);
    m_captureButton = new QToolButton(this);
    m_captureButton->setText(QStringLiteral("Capture"));
    m_captureButton->setToolTip(
        QStringLiteral("Copy original/colorized pixels from current frame bounds into selected "
                       "sprite"));
    m_captureButton->setEnabled(false);
    m_removeButton = new QToolButton(this);
    m_removeButton->setText(QStringLiteral("Remove"));
    m_removeButton->setToolTip(
        QStringLiteral("Remove the selected slot's sprite from the selected frames; the slots "
                       "behind it move up"));
    m_removeButton->setEnabled(false);
    m_captureSelectionButton = new QToolButton(this);
    m_captureSelectionButton->setText(QStringLiteral("Select Capture"));
    m_captureSelectionButton->setToolTip(
        QStringLiteral("Draw a shaped sprite capture selection on the original frame"));
    m_captureSelectionButton->setCheckable(true);
    m_captureSelectionButton->setEnabled(false);
    m_clearCaptureSelectionButton = new QToolButton(this);
    m_clearCaptureSelectionButton->setText(QStringLiteral("Clear Sel"));
    m_clearCaptureSelectionButton->setToolTip(
        QStringLiteral("Clear the current sprite capture selection"));
    m_clearCaptureSelectionButton->setEnabled(false);
    m_boundsOverlayButton = new QToolButton(this);
    m_boundsOverlayButton->setText(QStringLiteral("Show Bounds"));
    m_boundsOverlayButton->setToolTip(
        QStringLiteral("Show selected sprite slot bounds on original frame"));
    m_boundsOverlayButton->setCheckable(true);
    m_boundsOverlayButton->setChecked(false);
    m_boundsOverlayButton->setEnabled(false);
    m_detectionAreasButton = new QToolButton(this);
    m_detectionAreasButton->setText(QStringLiteral("Show Det Areas"));
    m_detectionAreasButton->setToolTip(
        QStringLiteral("Show sprite detection areas on original sprite preview"));
    m_detectionAreasButton->setCheckable(true);
    m_detectionAreasButton->setChecked(false);
    m_detectionAreasButton->setEnabled(false);
    m_spriteCanvasButton = new QToolButton(this);
    m_spriteCanvasButton->setText(QStringLiteral("Edit Sprite"));
    m_spriteCanvasButton->setToolTip(QStringLiteral("Show selected sprite in the main canvas"));
    m_spriteCanvasButton->setCheckable(true);
    m_spriteCanvasButton->setEnabled(false);
    m_applySpriteEditsButton = new QToolButton(this);
    m_applySpriteEditsButton->setText(QStringLiteral("Apply Sprite"));
    m_applySpriteEditsButton->setToolTip(
        QStringLiteral("Refresh frame previews from edited sprite data"));
    m_applySpriteEditsButton->setEnabled(false);
    m_minXSpinBox = new QSpinBox(this);
    m_minXSpinBox->setRange(0, 127);
    m_minXSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box minimum X"));
    m_minXSpinBox->setEnabled(false);
    m_minYSpinBox = new QSpinBox(this);
    m_minYSpinBox->setRange(0, 31);
    m_minYSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box minimum Y"));
    m_minYSpinBox->setEnabled(false);
    m_maxXSpinBox = new QSpinBox(this);
    m_maxXSpinBox->setRange(0, 127);
    m_maxXSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box maximum X"));
    m_maxXSpinBox->setEnabled(false);
    m_maxYSpinBox = new QSpinBox(this);
    m_maxYSpinBox->setRange(0, 31);
    m_maxYSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box maximum Y"));
    m_maxYSpinBox->setEnabled(false);
    m_detectionAreaComboBox = new QComboBox(this);
    m_detectionAreaComboBox->setToolTip(QStringLiteral("Sprite detection area"));
    for (std::size_t areaIndex = 0; areaIndex < document::kMaxSpriteDetectAreas; ++areaIndex) {
        m_detectionAreaComboBox->addItem(QStringLiteral("Area %1")
                                             .arg(static_cast<int>(areaIndex)),
                                         static_cast<int>(areaIndex));
    }
    m_detectionAreaComboBox->setEnabled(false);
    m_clearDetectionAreaButton = new QToolButton(this);
    m_clearDetectionAreaButton->setText(QStringLiteral("Clear Area"));
    m_clearDetectionAreaButton->setToolTip(
        QStringLiteral("Disable selected sprite detection area"));
    m_clearDetectionAreaButton->setEnabled(false);
    m_detectionAreaXSpinBox = new QSpinBox(this);
    m_detectionAreaXSpinBox->setRange(0, static_cast<int>(document::kMaxSpriteWidth - 1));
    m_detectionAreaXSpinBox->setToolTip(QStringLiteral("Sprite detection area X"));
    m_detectionAreaXSpinBox->setEnabled(false);
    m_detectionAreaYSpinBox = new QSpinBox(this);
    m_detectionAreaYSpinBox->setRange(0, static_cast<int>(document::kMaxSpriteHeight - 1));
    m_detectionAreaYSpinBox->setToolTip(QStringLiteral("Sprite detection area Y"));
    m_detectionAreaYSpinBox->setEnabled(false);
    m_detectionAreaWidthSpinBox = new QSpinBox(this);
    m_detectionAreaWidthSpinBox->setRange(1, static_cast<int>(document::kMaxSpriteWidth));
    m_detectionAreaWidthSpinBox->setToolTip(QStringLiteral("Sprite detection area width"));
    m_detectionAreaWidthSpinBox->setEnabled(false);
    m_detectionAreaHeightSpinBox = new QSpinBox(this);
    m_detectionAreaHeightSpinBox->setRange(1, static_cast<int>(document::kMaxSpriteHeight));
    m_detectionAreaHeightSpinBox->setToolTip(QStringLiteral("Sprite detection area height"));
    m_detectionAreaHeightSpinBox->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_frameSpritesLabel = new QLabel(QStringLiteral("No frame sprites"), this);
    m_frameSpritesLabel->setWordWrap(true);
    m_frameSpritesLabel->setMinimumHeight(28);
    m_selectedSpriteLabel = new QLabel(QStringLiteral("Selected: None"), this);
    m_selectedSpriteLabel->setWordWrap(true);
    pageLayout->addWidget(m_selectedSpriteLabel);
    pageLayout->addWidget(m_nameLineEdit);
    pageLayout->addWidget(m_slotComboBox);
    auto* slotButtonLayout = new QHBoxLayout();
    slotButtonLayout->addWidget(m_assignButton);
    slotButtonLayout->addWidget(m_captureButton);
    slotButtonLayout->addWidget(m_removeButton);
    pageLayout->addLayout(slotButtonLayout);
    auto* captureSelectionLayout = new QHBoxLayout();
    captureSelectionLayout->addWidget(m_captureSelectionButton);
    captureSelectionLayout->addWidget(m_clearCaptureSelectionButton);
    pageLayout->addLayout(captureSelectionLayout);
    auto* visualizationGrid = new QGridLayout();
    visualizationGrid->setHorizontalSpacing(4);
    visualizationGrid->setVerticalSpacing(4);
    visualizationGrid->addWidget(m_boundsOverlayButton, 0, 0);
    visualizationGrid->addWidget(m_detectionAreasButton, 0, 1);
    visualizationGrid->addWidget(m_spriteCanvasButton, 1, 0);
    visualizationGrid->addWidget(m_applySpriteEditsButton, 1, 1);
    pageLayout->addLayout(visualizationGrid);
    auto* boundsGrid = new QGridLayout();
    boundsGrid->setHorizontalSpacing(4);
    boundsGrid->setVerticalSpacing(4);
    boundsGrid->addWidget(new QLabel(QStringLiteral("Min X"), this), 0, 0);
    boundsGrid->addWidget(m_minXSpinBox, 0, 1);
    boundsGrid->addWidget(new QLabel(QStringLiteral("Min Y"), this), 0, 2);
    boundsGrid->addWidget(m_minYSpinBox, 0, 3);
    boundsGrid->addWidget(new QLabel(QStringLiteral("Max X"), this), 1, 0);
    boundsGrid->addWidget(m_maxXSpinBox, 1, 1);
    boundsGrid->addWidget(new QLabel(QStringLiteral("Max Y"), this), 1, 2);
    boundsGrid->addWidget(m_maxYSpinBox, 1, 3);
    pageLayout->addLayout(boundsGrid);
    auto* detectionAreaLayout = new QHBoxLayout();
    detectionAreaLayout->addWidget(m_detectionAreaComboBox);
    detectionAreaLayout->addWidget(m_clearDetectionAreaButton);
    pageLayout->addLayout(detectionAreaLayout);
    auto* detectionAreaGrid = new QGridLayout();
    detectionAreaGrid->setHorizontalSpacing(4);
    detectionAreaGrid->setVerticalSpacing(4);
    detectionAreaGrid->addWidget(new QLabel(QStringLiteral("X"), this), 0, 0);
    detectionAreaGrid->addWidget(m_detectionAreaXSpinBox, 0, 1);
    detectionAreaGrid->addWidget(new QLabel(QStringLiteral("Y"), this), 0, 2);
    detectionAreaGrid->addWidget(m_detectionAreaYSpinBox, 0, 3);
    detectionAreaGrid->addWidget(new QLabel(QStringLiteral("W"), this), 1, 0);
    detectionAreaGrid->addWidget(m_detectionAreaWidthSpinBox, 1, 1);
    detectionAreaGrid->addWidget(new QLabel(QStringLiteral("H"), this), 1, 2);
    detectionAreaGrid->addWidget(m_detectionAreaHeightSpinBox, 1, 3);
    pageLayout->addLayout(detectionAreaGrid);
    pageLayout->addWidget(m_frameSpritesLabel);
    pageLayout->addStretch(1);

    connect(m_nameLineEdit, &QLineEdit::editingFinished, this, [this]() {
        if (onNameEdited) {
            onNameEdited(m_nameLineEdit->text());
        }
    });
    connect(m_slotComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onSlotSelectionChanged) {
                    onSlotSelectionChanged();
                }
            });
    connect(m_assignButton, &QToolButton::clicked, this, [this]() {
        if (onAssignRequested) {
            onAssignRequested();
        }
    });
    connect(m_captureButton, &QToolButton::clicked, this, [this]() {
        if (onCaptureRequested) {
            onCaptureRequested();
        }
    });
    connect(m_captureSelectionButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onCaptureSelectionToggled) {
            onCaptureSelectionToggled(checked);
        }
    });
    connect(m_clearCaptureSelectionButton, &QToolButton::clicked, this, [this]() {
        if (onClearCaptureSelectionRequested) {
            onClearCaptureSelectionRequested();
        }
    });
    connect(m_removeButton, &QToolButton::clicked, this, [this]() {
        if (onRemoveRequested) {
            onRemoveRequested();
        }
    });
    connect(m_boundsOverlayButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onBoundsOverlayToggled) {
            onBoundsOverlayToggled(checked);
        }
    });
    connect(m_detectionAreasButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onDetectionAreasToggled) {
            onDetectionAreasToggled(checked);
        }
    });
    connect(m_spriteCanvasButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onSpriteCanvasToggled) {
            onSpriteCanvasToggled(checked);
        }
    });
    connect(m_applySpriteEditsButton, &QToolButton::clicked, this, [this]() {
        if (onApplySpriteEditsRequested) {
            onApplySpriteEditsRequested();
        }
    });
    connect(m_detectionAreaComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onDetectionAreaSelectionChanged) {
                    onDetectionAreaSelectionChanged();
                }
            });
    connect(m_clearDetectionAreaButton, &QToolButton::clicked, this, [this]() {
        if (onClearDetectionAreaRequested) {
            onClearDetectionAreaRequested();
        }
    });
    // The edited bound stops at its counterpart instead of pushing it.
    const auto connectBound = [this](QSpinBox* edited, QSpinBox* counterpart, bool isMinimum) {
        connect(edited,
                static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this,
                [this, edited, counterpart, isMinimum](int value) {
                    const int limit = counterpart->value();
                    if (isMinimum ? value > limit : value < limit) {
                        QSignalBlocker blocker(edited);
                        edited->setValue(limit);
                    }
                    if (onSlotBoundsEdited) {
                        onSlotBoundsEdited();
                    }
                });
    };
    connectBound(m_minXSpinBox, m_maxXSpinBox, true);
    connectBound(m_minYSpinBox, m_maxYSpinBox, true);
    connectBound(m_maxXSpinBox, m_minXSpinBox, false);
    connectBound(m_maxYSpinBox, m_minYSpinBox, false);
    for (QSpinBox* spinBox : {m_detectionAreaXSpinBox,
                              m_detectionAreaYSpinBox,
                              m_detectionAreaWidthSpinBox,
                              m_detectionAreaHeightSpinBox}) {
        connect(spinBox,
                static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this,
                [this](int) {
                    if (onDetectionAreaEdited) {
                        onDetectionAreaEdited();
                    }
                });
    }
}

void SpritePanel::refresh(const SpritePanelState& state)
{
    const document::ProjectDocument* document = state.document;
    const std::optional<std::size_t> frameIndex = state.frameIndex;
    const QVariant previousSlotData = m_slotComboBox->currentData();
    const QVariant previousAreaData = m_detectionAreaComboBox->currentData();
    {
        QSignalBlocker blocker(m_slotComboBox);
        m_slotComboBox->clear();
        if (document && frameIndex) {
            const std::vector<document::FrameSpriteReference>& references =
                document->frameSpriteReferences(*frameIndex);
            for (std::size_t slot = 0; slot < references.size(); ++slot) {
                const document::FrameSpriteReference& reference = references[slot];
                const QString assignedSprite =
                    reference.spriteId == 255
                        ? QString()
                        : static_cast<std::size_t>(reference.spriteId) >=
                                document->spriteCount()
                            ? QStringLiteral("Sprite %1")
                                  .arg(static_cast<int>(reference.spriteId))
                        : spriteDisplayName(*document, reference.spriteId);
                m_slotComboBox->addItem(
                    reference.spriteId == 255
                        ? QStringLiteral("Slot %1: Empty").arg(static_cast<int>(slot))
                        : QStringLiteral("Slot %1: %2")
                              .arg(static_cast<int>(slot))
                              .arg(assignedSprite),
                    static_cast<int>(slot));
            }
        }
        if (m_slotComboBox->count() == 0) {
            m_slotComboBox->addItem(QStringLiteral("No slots"), -1);
            m_slotComboBox->setCurrentIndex(0);
        } else if (previousSlotData.isValid()) {
            const int previousIndex = m_slotComboBox->findData(previousSlotData);
            m_slotComboBox->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        } else {
            m_slotComboBox->setCurrentIndex(0);
        }
    }
    m_slotComboBox->setEnabled(document && frameIndex.has_value());

    if (!document || !frameIndex) {
        m_frameSpritesLabel->setText(QStringLiteral("No frame sprites"));
    } else {
        const std::vector<document::FrameSpriteReference>& references =
            document->frameSpriteReferences(*frameIndex);
        QStringList assignedSprites;
        for (std::size_t slot = 0; slot < references.size(); ++slot) {
            const document::FrameSpriteReference& reference = references[slot];
            if (reference.spriteId == 255) {
                continue;
            }
            const QString assignedSprite =
                reference.spriteId >= document->spriteCount()
                    ? QStringLiteral("sprite %1").arg(static_cast<int>(reference.spriteId))
                    : spriteDisplayName(*document, reference.spriteId);
            const bool hdMissing = document->rendersHdPlaneOnly() &&
                                   reference.spriteId < document->spriteCount() &&
                                   !document->hasHdSprite(reference.spriteId);
            assignedSprites << QStringLiteral("%1: %2 [%3,%4-%5,%6]%7")
                                   .arg(static_cast<int>(slot))
                                   .arg(assignedSprite)
                                   .arg(reference.minX)
                                   .arg(reference.minY)
                                   .arg(reference.maxX)
                                   .arg(reference.maxY)
                                   .arg(hdMissing ? QStringLiteral(" %1: frame not rendered")
                                                        .arg(kNoPictureText)
                                                  : QString());
        }
        m_frameSpritesLabel->setText(
            assignedSprites.isEmpty()
                ? QStringLiteral("Current frame uses no sprites")
                : assignedSprites.join(QStringLiteral("\n")));
    }

    const bool hasSprite =
        document && state.spriteIndex && *state.spriteIndex < document->spriteCount();
    const std::optional<std::size_t> slotIndex = selectedSlotIndex();
    m_assignButton->setEnabled(document && frameIndex && hasSprite && slotIndex);
    m_captureButton->setEnabled(m_assignButton->isEnabled());
    m_captureSelectionButton->setEnabled(m_captureButton->isEnabled());
    m_clearCaptureSelectionButton->setEnabled(state.hasCaptureSelection);
    bool slotHasSprite = false;
    std::optional<document::FrameSpriteReference> selectedReference;
    if (document && frameIndex && slotIndex) {
        const std::vector<document::FrameSpriteReference>& references =
            document->frameSpriteReferences(*frameIndex);
        if (*slotIndex < references.size()) {
            selectedReference = references[*slotIndex];
            slotHasSprite = selectedReference->spriteId != 255;
        }
    }
    m_removeButton->setEnabled(slotHasSprite);
    m_applySpriteEditsButton->setEnabled(state.spriteEditsPending);
    const bool hasSlot = selectedReference.has_value();
    m_boundsOverlayButton->setEnabled(hasSlot);
    {
        QSignalBlocker minXBlocker(m_minXSpinBox);
        QSignalBlocker minYBlocker(m_minYSpinBox);
        QSignalBlocker maxXBlocker(m_maxXSpinBox);
        QSignalBlocker maxYBlocker(m_maxYSpinBox);
        // Bounds are pixels of the original frame.
        const render::Size originalSize =
            document ? document->originalFrameSize() : render::kSdFrameSize;
        m_minXSpinBox->setRange(0, originalSize.width - 1);
        m_minYSpinBox->setRange(0, originalSize.height - 1);
        m_maxXSpinBox->setRange(0, originalSize.width - 1);
        m_maxYSpinBox->setRange(0, originalSize.height - 1);
        // Bounds belong to an assigned sprite; an empty slot has none to edit.
        m_minXSpinBox->setEnabled(slotHasSprite);
        m_minYSpinBox->setEnabled(slotHasSprite);
        m_maxXSpinBox->setEnabled(slotHasSprite);
        m_maxYSpinBox->setEnabled(slotHasSprite);
        if (hasSlot) {
            m_minXSpinBox->setValue(static_cast<int>(selectedReference->minX));
            m_minYSpinBox->setValue(static_cast<int>(selectedReference->minY));
            m_maxXSpinBox->setValue(static_cast<int>(selectedReference->maxX));
            m_maxYSpinBox->setValue(static_cast<int>(selectedReference->maxY));
        } else {
            m_minXSpinBox->setValue(0);
            m_minYSpinBox->setValue(0);
            m_maxXSpinBox->setValue(originalSize.width - 1);
            m_maxYSpinBox->setValue(originalSize.height - 1);
        }
    }

    if (!hasSprite) {
        m_detectionAreasButton->setEnabled(false);
        m_spriteCanvasButton->setEnabled(false);
        m_captureButton->setEnabled(false);
        m_captureSelectionButton->setEnabled(false);
        m_clearCaptureSelectionButton->setEnabled(false);
        m_nameLineEdit->setEnabled(false);
        m_nameLineEdit->clear();
        m_applySpriteEditsButton->setEnabled(false);
        m_detectionAreaComboBox->setEnabled(false);
        m_clearDetectionAreaButton->setEnabled(false);
        m_detectionAreaXSpinBox->setEnabled(false);
        m_detectionAreaYSpinBox->setEnabled(false);
        m_detectionAreaWidthSpinBox->setEnabled(false);
        m_detectionAreaHeightSpinBox->setEnabled(false);
        m_selectedSpriteLabel->setText(QStringLiteral("Selected: None"));
        return;
    }

    const std::size_t spriteIndex = *state.spriteIndex;
    const document::SpriteRecord& sprite = document->sprite(spriteIndex);
    const QString label = sprite.name.empty()
                              ? QString::number(static_cast<int>(spriteIndex))
                              : QStringLiteral("%1 - %2")
                                    .arg(static_cast<int>(spriteIndex))
                                    .arg(QString::fromStdString(sprite.name));
    m_selectedSpriteLabel->setText(
        QStringLiteral("Selected: %1%2")
            .arg(label)
            .arg(extraDataSuffix(*document, sprite.hasHd)));
    {
        QSignalBlocker blocker(m_nameLineEdit);
        m_nameLineEdit->setText(QString::fromStdString(sprite.name));
        m_nameLineEdit->setEnabled(true);
    }
    m_spriteCanvasButton->setEnabled(true);
    {
        QSignalBlocker blocker(m_detectionAreaComboBox);
        const int previousIndex = m_detectionAreaComboBox->findData(previousAreaData);
        if (previousIndex >= 0) {
            m_detectionAreaComboBox->setCurrentIndex(previousIndex);
        } else {
            m_detectionAreaComboBox->setCurrentIndex(0);
        }
    }
    const std::size_t areaIndex = selectedDetectionAreaIndex();
    const bool hasAreaSelection = areaIndex < sprite.detectionAreas.size();
    std::optional<document::SpriteDetectionArea> selectedArea;
    if (hasAreaSelection) {
        selectedArea = sprite.detectionAreas[areaIndex];
    }
    const bool selectedAreaValid = selectedArea && isSpriteDetectionAreaSet(*selectedArea);
    m_detectionAreaComboBox->setEnabled(true);
    m_clearDetectionAreaButton->setEnabled(selectedAreaValid);
    {
        QSignalBlocker xBlocker(m_detectionAreaXSpinBox);
        QSignalBlocker yBlocker(m_detectionAreaYSpinBox);
        QSignalBlocker widthBlocker(m_detectionAreaWidthSpinBox);
        QSignalBlocker heightBlocker(m_detectionAreaHeightSpinBox);
        // An unset area is created by drawing it in the sprite editor, not by typing.
        m_detectionAreaXSpinBox->setEnabled(selectedAreaValid);
        m_detectionAreaYSpinBox->setEnabled(selectedAreaValid);
        m_detectionAreaWidthSpinBox->setEnabled(selectedAreaValid);
        m_detectionAreaHeightSpinBox->setEnabled(selectedAreaValid);
        if (selectedAreaValid) {
            m_detectionAreaXSpinBox->setValue(static_cast<int>(selectedArea->x));
            m_detectionAreaYSpinBox->setValue(static_cast<int>(selectedArea->y));
            m_detectionAreaWidthSpinBox->setValue(static_cast<int>(selectedArea->width));
            m_detectionAreaHeightSpinBox->setValue(static_cast<int>(selectedArea->height));
        } else {
            m_detectionAreaXSpinBox->setValue(0);
            m_detectionAreaYSpinBox->setValue(0);
            m_detectionAreaWidthSpinBox->setValue(1);
            m_detectionAreaHeightSpinBox->setValue(1);
        }
    }
    m_detectionAreasButton->setEnabled(std::any_of(sprite.detectionAreas.begin(),
                                                   sprite.detectionAreas.end(),
                                                   isSpriteDetectionAreaSet));
}

void SpritePanel::setSelectedDetectionAreaIndex(std::size_t areaIndex)
{
    const int comboIndex = m_detectionAreaComboBox->findData(static_cast<int>(areaIndex));
    if (comboIndex >= 0) {
        QSignalBlocker blocker(m_detectionAreaComboBox);
        m_detectionAreaComboBox->setCurrentIndex(comboIndex);
    }
}

void SpritePanel::setSpriteCanvasChecked(bool checked)
{
    QSignalBlocker blocker(m_spriteCanvasButton);
    m_spriteCanvasButton->setChecked(checked);
}

void SpritePanel::setCaptureSelectionChecked(bool checked)
{
    QSignalBlocker blocker(m_captureSelectionButton);
    m_captureSelectionButton->setChecked(checked);
}

void SpritePanel::setApplySpriteEditsEnabled(bool enabled)
{
    m_applySpriteEditsButton->setEnabled(enabled);
}

std::optional<std::size_t> SpritePanel::selectedSlotIndex() const
{
    const int slotIndex = m_slotComboBox->currentData().toInt();
    if (slotIndex < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(slotIndex);
}

void SpritePanel::setSelectedSlotIndex(std::size_t slotIndex)
{
    const int index = m_slotComboBox->findData(static_cast<int>(slotIndex));
    if (index >= 0) {
        QSignalBlocker blocker(m_slotComboBox);
        m_slotComboBox->setCurrentIndex(index);
    }
}

std::size_t SpritePanel::selectedDetectionAreaIndex() const
{
    return static_cast<std::size_t>(std::max(0, m_detectionAreaComboBox->currentData().toInt()));
}

SpriteSlotBounds SpritePanel::slotBounds() const
{
    return {m_minXSpinBox->value(),
            m_minYSpinBox->value(),
            m_maxXSpinBox->value(),
            m_maxYSpinBox->value()};
}

SpriteDetectionAreaInput SpritePanel::detectionAreaInput() const
{
    return {m_detectionAreaXSpinBox->value(),
            m_detectionAreaYSpinBox->value(),
            m_detectionAreaWidthSpinBox->value(),
            m_detectionAreaHeightSpinBox->value()};
}

bool SpritePanel::isCaptureSelectionChecked() const
{
    return m_captureSelectionButton->isChecked();
}

bool SpritePanel::isBoundsOverlayChecked() const
{
    return m_boundsOverlayButton->isChecked();
}

bool SpritePanel::isDetectionAreasChecked() const
{
    return m_detectionAreasButton->isChecked();
}

bool SpritePanel::isSpriteCanvasChecked() const
{
    return m_spriteCanvasButton->isChecked();
}

}  // namespace colorizing::app
