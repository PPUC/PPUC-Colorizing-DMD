#include "panels/SpritePanel.h"

#include <algorithm>
#include <vector>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "ColorGridWidget.h"
#include "PlaneLabels.h"
#include "render/CanvasLayout.h"

namespace colorizing::app {

namespace {

QString spriteDisplayName(const document::ProjectDocument& document, std::size_t spriteIndex)
{
    const document::SpriteRecord& sprite = document.sprite(spriteIndex);
    const QString base =
        QCoreApplication::translate("SpritePanel", "Sprite %1").arg(static_cast<int>(spriteIndex));
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
    m_nameLineEdit->setToolTip(tr("Selected sprite name"));
    m_nameLineEdit->setMaxLength(31);
    m_nameLineEdit->setEnabled(false);
    m_slotComboBox = new QComboBox(this);
    m_slotComboBox->setToolTip(tr("Current frame sprite slot"));
    m_slotComboBox->setEnabled(false);
    m_assignButton = new QToolButton(this);
    m_assignButton->setText(tr("Assign"));
    m_assignButton->setToolTip(tr("Assign selected sprite to selected slot"));
    m_assignButton->setEnabled(false);
    m_captureButton = new QToolButton(this);
    m_captureButton->setText(tr("Capture"));
    m_captureButton->setToolTip(
        tr("Copy original/colorized pixels from current frame bounds into selected "
           "sprite"));
    m_captureButton->setEnabled(false);
    m_removeButton = new QToolButton(this);
    m_removeButton->setText(tr("Remove"));
    m_removeButton->setToolTip(
        tr("Remove the selected slot's sprite from the selected frames; the slots "
           "behind it move up"));
    m_removeButton->setEnabled(false);
    // The slot order is the drawing order: a later slot is drawn over an earlier one.
    m_moveUpButton = new QToolButton(this);
    m_moveUpButton->setText(tr("Move Up"));
    m_moveUpButton->setToolTip(
        tr("Move the selected slot's sprite one slot up in the selected frames: it is drawn "
           "under the sprite it changes places with"));
    m_moveUpButton->setEnabled(false);
    m_moveDownButton = new QToolButton(this);
    m_moveDownButton->setText(tr("Move Down"));
    m_moveDownButton->setToolTip(
        tr("Move the selected slot's sprite one slot down in the selected frames: it is drawn "
           "over the sprite it changes places with"));
    m_moveDownButton->setEnabled(false);
    m_captureSelectionButton = new QToolButton(this);
    m_captureSelectionButton->setText(tr("Select Capture"));
    m_captureSelectionButton->setToolTip(
        tr("Draw a shaped sprite capture selection on the original frame"));
    m_captureSelectionButton->setCheckable(true);
    m_captureSelectionButton->setEnabled(false);
    m_clearCaptureSelectionButton = new QToolButton(this);
    m_clearCaptureSelectionButton->setText(tr("Clear Sel"));
    m_clearCaptureSelectionButton->setToolTip(
        tr("Clear the current sprite capture selection"));
    m_clearCaptureSelectionButton->setEnabled(false);
    m_boundsOverlayButton = new QToolButton(this);
    m_boundsOverlayButton->setText(tr("Show Bounds"));
    m_boundsOverlayButton->setToolTip(
        tr("Show selected sprite slot bounds on original frame"));
    m_boundsOverlayButton->setCheckable(true);
    m_boundsOverlayButton->setChecked(false);
    m_boundsOverlayButton->setEnabled(false);
    // Always available: a frame without sprites shows no outline.
    m_outlinesButton = new QToolButton(this);
    m_outlinesButton->setText(tr("Outline Sprites"));
    m_outlinesButton->setToolTip(
        tr("Outline the pixels of the sprites libserum finds on the frame in green"));
    m_outlinesButton->setCheckable(true);
    m_outlinesButton->setChecked(true);
    m_detectionAreasButton = new QToolButton(this);
    m_detectionAreasButton->setText(tr("Show Det Areas"));
    m_detectionAreasButton->setToolTip(
        tr("Show sprite detection areas on original sprite preview"));
    m_detectionAreasButton->setCheckable(true);
    m_detectionAreasButton->setChecked(false);
    m_detectionAreasButton->setEnabled(false);
    m_spriteCanvasButton = new QToolButton(this);
    m_spriteCanvasButton->setText(tr("Edit Sprite"));
    m_spriteCanvasButton->setToolTip(tr("Show selected sprite in the main canvas"));
    m_spriteCanvasButton->setCheckable(true);
    m_spriteCanvasButton->setEnabled(false);
    m_applySpriteEditsButton = new QToolButton(this);
    m_applySpriteEditsButton->setText(tr("Apply Sprite"));
    m_applySpriteEditsButton->setToolTip(
        tr("Refresh frame previews from edited sprite data"));
    m_applySpriteEditsButton->setEnabled(false);
    m_createHdButton = new QToolButton(this);
    m_createHdButton->setText(tr("Create HD Sprite"));
    m_createHdButton->setToolTip(tr("Create the HD sprite from the SD sprite"));
    m_createHdButton->setEnabled(false);
    m_removeHdButton = new QToolButton(this);
    m_removeHdButton->setText(tr("Remove HD Sprite"));
    m_removeHdButton->setToolTip(tr("Remove the sprite's HD data"));
    m_removeHdButton->setEnabled(false);
    m_transparentPaintButton = new QToolButton(this);
    m_transparentPaintButton->setText(tr("Paint Transparent"));
    m_transparentPaintButton->setCheckable(true);
    m_transparentPaintButton->setEnabled(false);
    connect(m_transparentPaintButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onTransparentPaintToggled) {
            onTransparentPaintToggled(checked);
        }
    });
    connect(m_createHdButton, &QToolButton::clicked, this, [this]() {
        if (onCreateHdRequested) {
            onCreateHdRequested();
        }
    });
    connect(m_removeHdButton, &QToolButton::clicked, this, [this]() {
        if (onRemoveHdRequested) {
            onRemoveHdRequested();
        }
    });
    m_sourceFrameButton = new QToolButton(this);
    m_sourceFrameButton->setText(tr("Source Frame"));
    m_sourceFrameButton->setToolTip(tr("Go to the frame the sprite was taken from"));
    m_sourceFrameButton->setEnabled(false);
    m_editorSourceFrameButton = new QToolButton(this);
    m_editorSourceFrameButton->setText(tr("Source Frame"));
    m_editorSourceFrameButton->setToolTip(m_sourceFrameButton->toolTip());
    m_editorSourceFrameButton->setEnabled(false);
    m_minXSpinBox = new QSpinBox(this);
    m_minXSpinBox->setRange(0, 127);
    m_minXSpinBox->setToolTip(tr("Sprite slot bounding box minimum X"));
    m_minXSpinBox->setEnabled(false);
    m_minYSpinBox = new QSpinBox(this);
    m_minYSpinBox->setRange(0, 31);
    m_minYSpinBox->setToolTip(tr("Sprite slot bounding box minimum Y"));
    m_minYSpinBox->setEnabled(false);
    m_maxXSpinBox = new QSpinBox(this);
    m_maxXSpinBox->setRange(0, 127);
    m_maxXSpinBox->setToolTip(tr("Sprite slot bounding box maximum X"));
    m_maxXSpinBox->setEnabled(false);
    m_maxYSpinBox = new QSpinBox(this);
    m_maxYSpinBox->setRange(0, 31);
    m_maxYSpinBox->setToolTip(tr("Sprite slot bounding box maximum Y"));
    m_maxYSpinBox->setEnabled(false);
    // Where the sprite found in the bounding box is drawn, relative to where it is found.
    m_offsetXSpinBox = new QSpinBox(this);
    m_offsetXSpinBox->setRange(-127, 127);
    m_offsetXSpinBox->setToolTip(
        tr("Draw the sprite found in the bounding box this many pixels to the right (negative: "
           "to the left) of where it is found"));
    m_offsetXSpinBox->setEnabled(false);
    m_offsetYSpinBox = new QSpinBox(this);
    m_offsetYSpinBox->setRange(-31, 31);
    m_offsetYSpinBox->setToolTip(
        tr("Draw the sprite found in the bounding box this many pixels lower (negative: "
           "higher) than where it is found"));
    m_offsetYSpinBox->setEnabled(false);
    m_detectionAreaComboBox = new QComboBox(this);
    m_detectionAreaComboBox->setToolTip(tr("Sprite detection area"));
    for (std::size_t areaIndex = 0; areaIndex < document::kMaxSpriteDetectAreas; ++areaIndex) {
        m_detectionAreaComboBox->addItem(tr("Area %1")
                                             .arg(static_cast<int>(areaIndex)),
                                         static_cast<int>(areaIndex));
    }
    m_detectionAreaComboBox->setEnabled(false);
    m_clearDetectionAreaButton = new QToolButton(this);
    m_clearDetectionAreaButton->setText(tr("Clear Area"));
    m_clearDetectionAreaButton->setToolTip(
        tr("Disable selected sprite detection area"));
    m_clearDetectionAreaButton->setEnabled(false);
    m_detectionAreaXSpinBox = new QSpinBox(this);
    m_detectionAreaXSpinBox->setRange(0, static_cast<int>(document::kMaxSpriteWidth - 1));
    m_detectionAreaXSpinBox->setToolTip(tr("Sprite detection area X"));
    m_detectionAreaXSpinBox->setEnabled(false);
    m_detectionAreaYSpinBox = new QSpinBox(this);
    m_detectionAreaYSpinBox->setRange(0, static_cast<int>(document::kMaxSpriteHeight - 1));
    m_detectionAreaYSpinBox->setToolTip(tr("Sprite detection area Y"));
    m_detectionAreaYSpinBox->setEnabled(false);
    m_detectionAreaWidthSpinBox = new QSpinBox(this);
    m_detectionAreaWidthSpinBox->setRange(1, static_cast<int>(document::kMaxSpriteWidth));
    m_detectionAreaWidthSpinBox->setToolTip(tr("Sprite detection area width"));
    m_detectionAreaWidthSpinBox->setEnabled(false);
    m_detectionAreaHeightSpinBox = new QSpinBox(this);
    m_detectionAreaHeightSpinBox->setRange(1, static_cast<int>(document::kMaxSpriteHeight));
    m_detectionAreaHeightSpinBox->setToolTip(tr("Sprite detection area height"));
    m_detectionAreaHeightSpinBox->setEnabled(false);

    m_backToFrameButton = new QToolButton(this);
    m_backToFrameButton->setText(tr("Back to Frame"));
    m_backToFrameButton->setToolTip(tr("Leave the sprite editor and show the frame again"));

    // The frame canvas' page: the sprite slots of the frame.
    m_framePage = new QWidget(this);
    auto* frameLayout = new QVBoxLayout(m_framePage);
    frameLayout->setContentsMargins(6, 6, 6, 6);
    m_frameSpritesLabel = new QLabel(tr("No frame sprites"), m_framePage);
    m_frameSpritesLabel->setWordWrap(true);
    m_frameSpritesLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // The frame's sprites can be 16 lines: they scroll in a few lines' height (the left
    // sidebar's "Used by the current frame" lists them as well).
    auto* frameSpritesArea = new QScrollArea(m_framePage);
    frameSpritesArea->setWidget(m_frameSpritesLabel);
    frameSpritesArea->setWidgetResizable(true);
    frameSpritesArea->setFrameShape(QFrame::NoFrame);
    frameSpritesArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frameSpritesArea->setMaximumHeight(
        QFontMetrics(m_frameSpritesLabel->font()).lineSpacing() * 4 + 6);
    frameSpritesArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_selectedSpriteLabel = new QLabel(tr("Selected: None"), m_framePage);
    m_selectedSpriteLabel->setWordWrap(true);
    m_spriteCountWarningLabel = new QLabel(m_framePage);
    m_spriteCountWarningLabel->setWordWrap(true);
    m_spriteCountWarningLabel->setForegroundRole(QPalette::BrightText);
    m_spriteCountWarningLabel->setAutoFillBackground(true);
    {
        QPalette warning = m_spriteCountWarningLabel->palette();
        warning.setColor(QPalette::Window, QColor(0xb0, 0x50, 0x00));
        warning.setColor(QPalette::BrightText, Qt::white);
        m_spriteCountWarningLabel->setPalette(warning);
    }
    m_spriteCountWarningLabel->setContentsMargins(4, 2, 4, 2);
    m_spriteCountWarningLabel->hide();
    frameLayout->addWidget(m_selectedSpriteLabel);
    frameLayout->addWidget(m_spriteCountWarningLabel);
    frameLayout->addWidget(frameSpritesArea);
    frameLayout->addWidget(m_slotComboBox);
    auto* slotButtonLayout = new QHBoxLayout();
    slotButtonLayout->addWidget(m_assignButton);
    slotButtonLayout->addWidget(m_captureButton);
    slotButtonLayout->addWidget(m_removeButton);
    frameLayout->addLayout(slotButtonLayout);
    auto* slotOrderLayout = new QHBoxLayout();
    slotOrderLayout->addWidget(m_moveUpButton);
    slotOrderLayout->addWidget(m_moveDownButton);
    slotOrderLayout->addStretch(1);
    frameLayout->addLayout(slotOrderLayout);
    auto* captureSelectionLayout = new QHBoxLayout();
    captureSelectionLayout->addWidget(m_captureSelectionButton);
    captureSelectionLayout->addWidget(m_clearCaptureSelectionButton);
    frameLayout->addLayout(captureSelectionLayout);
    auto* frameButtonLayout = new QHBoxLayout();
    frameButtonLayout->addWidget(m_boundsOverlayButton);
    frameButtonLayout->addWidget(m_outlinesButton);
    frameButtonLayout->addWidget(m_sourceFrameButton);
    frameLayout->addLayout(frameButtonLayout);
    auto* boundsGrid = new QGridLayout();
    boundsGrid->setHorizontalSpacing(4);
    boundsGrid->setVerticalSpacing(4);
    boundsGrid->addWidget(new QLabel(tr("Min X"), m_framePage), 0, 0);
    boundsGrid->addWidget(m_minXSpinBox, 0, 1);
    boundsGrid->addWidget(new QLabel(tr("Min Y"), m_framePage), 0, 2);
    boundsGrid->addWidget(m_minYSpinBox, 0, 3);
    boundsGrid->addWidget(new QLabel(tr("Max X"), m_framePage), 1, 0);
    boundsGrid->addWidget(m_maxXSpinBox, 1, 1);
    boundsGrid->addWidget(new QLabel(tr("Max Y"), m_framePage), 1, 2);
    boundsGrid->addWidget(m_maxYSpinBox, 1, 3);
    boundsGrid->addWidget(new QLabel(tr("Offset X"), m_framePage), 2, 0);
    boundsGrid->addWidget(m_offsetXSpinBox, 2, 1);
    boundsGrid->addWidget(new QLabel(tr("Offset Y"), m_framePage), 2, 2);
    boundsGrid->addWidget(m_offsetYSpinBox, 2, 3);
    frameLayout->addLayout(boundsGrid);
    frameLayout->addWidget(m_spriteCanvasButton);
    frameLayout->addStretch(1);

    // The sprite editor's page: the sprite itself.
    m_recordPage = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(m_recordPage);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_editedSpriteLabel = new QLabel(tr("Selected: None"), m_recordPage);
    m_editedSpriteLabel->setWordWrap(true);
    pageLayout->addWidget(m_editedSpriteLabel);
    pageLayout->addWidget(m_nameLineEdit);
    pageLayout->addWidget(m_detectionAreasButton);
    auto* detectionAreaLayout = new QHBoxLayout();
    detectionAreaLayout->addWidget(m_detectionAreaComboBox);
    detectionAreaLayout->addWidget(m_clearDetectionAreaButton);
    pageLayout->addLayout(detectionAreaLayout);
    auto* detectionAreaGrid = new QGridLayout();
    detectionAreaGrid->setHorizontalSpacing(4);
    detectionAreaGrid->setVerticalSpacing(4);
    detectionAreaGrid->addWidget(new QLabel(tr("X"), m_recordPage), 0, 0);
    detectionAreaGrid->addWidget(m_detectionAreaXSpinBox, 0, 1);
    detectionAreaGrid->addWidget(new QLabel(tr("Y"), m_recordPage), 0, 2);
    detectionAreaGrid->addWidget(m_detectionAreaYSpinBox, 0, 3);
    detectionAreaGrid->addWidget(new QLabel(tr("W"), m_recordPage), 1, 0);
    detectionAreaGrid->addWidget(m_detectionAreaWidthSpinBox, 1, 1);
    detectionAreaGrid->addWidget(new QLabel(tr("H"), m_recordPage), 1, 2);
    detectionAreaGrid->addWidget(m_detectionAreaHeightSpinBox, 1, 3);
    pageLayout->addLayout(detectionAreaGrid);

    // Shape mode and dynamic content of the selected sprite.
    m_shapeModeCheckBox = new QCheckBox(tr("Shape mode"), this);
    m_shapeModeCheckBox->setToolTip(tr(
        "Find the sprite by its shape (lit or not) instead of its shades"));
    m_dynamicPaintButton = new QToolButton(this);
    m_dynamicPaintButton->setText(tr("Paint Dynamic"));
    m_dynamicPaintButton->setCheckable(true);
    m_dynamicPaintButton->setToolTip(tr(
        "On the sprite canvas, strokes put sprite pixels into the dynamic set (Shift or erase: "
        "out of it); dynamic pixels are tinted"));
    m_dynamicSetSpinBox = new QSpinBox(this);
    m_dynamicSetSpinBox->setRange(1, static_cast<int>(document::kMaxDynamicSetsPerSprite));
    m_dynamicSetSpinBox->setPrefix(tr("Set "));
    m_dynamicSetSpinBox->setToolTip(tr("Sprite dynamic set"));
    m_dynamicColorGrid = new ColorGridWidget(16, 8, this);
    m_dynamicColorGrid->setToolTip(tr(
        "The set's color for each original shade: a dynamic sprite pixel shows the color of the "
        "frame's shade under it. Click to change"));
    m_editColorsToSetButton = new QToolButton(this);
    m_editColorsToSetButton->setText(tr("Edit Colors > Set"));
    m_editColorsToSetButton->setToolTip(tr("Copy the edit colors into the set"));
    m_setToEditColorsButton = new QToolButton(this);
    m_setToEditColorsButton->setText(tr("Set > Edit Colors"));
    m_setToEditColorsButton->setToolTip(tr("Copy the set's colors to the edit colors"));
    auto* dynamicRow = new QHBoxLayout();
    dynamicRow->addWidget(m_shapeModeCheckBox);
    dynamicRow->addStretch(1);
    dynamicRow->addWidget(m_dynamicPaintButton);
    dynamicRow->addWidget(m_dynamicSetSpinBox);
    pageLayout->addLayout(dynamicRow);
    pageLayout->addWidget(m_dynamicColorGrid);
    auto* dynamicCopyRow = new QHBoxLayout();
    dynamicCopyRow->addWidget(m_editColorsToSetButton);
    dynamicCopyRow->addWidget(m_setToEditColorsButton);
    pageLayout->addLayout(dynamicCopyRow);
    connect(m_shapeModeCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (onShapeModeToggled) {
            onShapeModeToggled(checked);
        }
    });
    connect(m_dynamicPaintButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onDynamicPaintToggled) {
            onDynamicPaintToggled(checked);
        }
    });
    connect(m_dynamicSetSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int) { refreshDynamic(m_dynamicDocument, m_dynamicSprite); });
    m_dynamicColorGrid->setColorClickedHandler([this](std::size_t colorIndex) {
        if (onDynamicColorClicked) {
            onDynamicColorClicked(colorIndex);
        }
    });
    connect(m_editColorsToSetButton, &QToolButton::clicked, this, [this]() {
        if (onEditColorsToDynamicSet) {
            onEditColorsToDynamicSet();
        }
    });
    connect(m_setToEditColorsButton, &QToolButton::clicked, this, [this]() {
        if (onDynamicSetToEditColors) {
            onDynamicSetToEditColors();
        }
    });
    refreshDynamic(nullptr, std::nullopt);
    auto* hdLayout = new QHBoxLayout();
    hdLayout->addWidget(m_createHdButton);
    hdLayout->addWidget(m_removeHdButton);
    hdLayout->addWidget(m_transparentPaintButton);
    pageLayout->addLayout(hdLayout);
    auto* leaveLayout = new QHBoxLayout();
    leaveLayout->addWidget(m_applySpriteEditsButton);
    leaveLayout->addWidget(m_editorSourceFrameButton);
    leaveLayout->addWidget(m_backToFrameButton);
    pageLayout->addLayout(leaveLayout);
    pageLayout->addStretch(1);

    // Both pages, for a panel that is not split into sections (MainWindow moves them).
    auto* panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(m_framePage);
    panelLayout->addWidget(m_recordPage);
    // Tab goes through each page as laid out.
    for (const std::vector<QWidget*>& order :
         {std::vector<QWidget*>{m_slotComboBox, m_assignButton, m_captureButton, m_removeButton,
                                m_moveUpButton, m_moveDownButton,
                                m_captureSelectionButton, m_clearCaptureSelectionButton,
                                m_boundsOverlayButton, m_outlinesButton, m_sourceFrameButton,
                                m_minXSpinBox,
                                m_minYSpinBox, m_maxXSpinBox, m_maxYSpinBox,
                                m_offsetXSpinBox, m_offsetYSpinBox,
                                m_spriteCanvasButton},
          std::vector<QWidget*>{m_nameLineEdit, m_detectionAreasButton,
                                m_detectionAreaComboBox, m_clearDetectionAreaButton,
                                m_detectionAreaXSpinBox, m_detectionAreaYSpinBox,
                                m_detectionAreaWidthSpinBox, m_detectionAreaHeightSpinBox,
                                m_shapeModeCheckBox, m_dynamicPaintButton, m_dynamicSetSpinBox,
                                m_editColorsToSetButton, m_setToEditColorsButton,
                                m_createHdButton, m_removeHdButton, m_applySpriteEditsButton,
                                m_editorSourceFrameButton, m_backToFrameButton}}) {
        for (std::size_t index = 1; index < order.size(); ++index) {
            QWidget::setTabOrder(order[index - 1], order[index]);
        }
    }
    connect(m_backToFrameButton, &QToolButton::clicked, this, [this]() {
        // As Edit Sprite clicked up again.
        m_spriteCanvasButton->setChecked(false);
    });

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
    connect(m_moveUpButton, &QToolButton::clicked, this, [this]() {
        if (onMoveRequested) {
            onMoveRequested(-1);
        }
    });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this]() {
        if (onMoveRequested) {
            onMoveRequested(1);
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
    connect(m_outlinesButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onOutlinesToggled) {
            onOutlinesToggled(checked);
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
    connect(m_sourceFrameButton, &QToolButton::clicked, this, [this]() {
        if (onSourceFrameRequested) {
            onSourceFrameRequested();
        }
    });
    connect(m_editorSourceFrameButton, &QToolButton::clicked, this, [this]() {
        // Back to Frame, then to the source frame.
        m_spriteCanvasButton->setChecked(false);
        if (onSourceFrameRequested) {
            onSourceFrameRequested();
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
    for (QSpinBox* spinBox : {m_offsetXSpinBox, m_offsetYSpinBox}) {
        connect(spinBox,
                static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this,
                [this](int) {
                    if (onSlotOffsetEdited) {
                        onSlotOffsetEdited();
                    }
                });
    }
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

std::size_t SpritePanel::dynamicSet() const
{
    return static_cast<std::size_t>(m_dynamicSetSpinBox->value() - 1);
}

void SpritePanel::setDynamicPaintChecked(bool checked)
{
    QSignalBlocker blocker(m_dynamicPaintButton);
    m_dynamicPaintButton->setChecked(checked);
}

void SpritePanel::setTransparentPaintChecked(bool checked)
{
    QSignalBlocker blocker(m_transparentPaintButton);
    m_transparentPaintButton->setChecked(checked);
}

bool SpritePanel::isTransparentPaintChecked() const
{
    return m_transparentPaintButton->isChecked();
}

void SpritePanel::refreshDynamic(const document::ProjectDocument* document,
                                 std::optional<std::size_t> spriteIndex)
{
    m_dynamicDocument = document;
    m_dynamicSprite = spriteIndex;
    const bool hasSprite = document && spriteIndex && *spriteIndex < document->spriteCount();
    // HD of the sprite, as the project's layout has it (PlaneLabels.h).
    {
        const render::PlaneLayout layout =
            document ? document->geometry().layout() : render::PlaneLayout::Ordinary;
        const bool spriteHasHd = hasSprite && document->hasHdSprite(*spriteIndex);
        if (layout == render::PlaneLayout::NativeExtra) {
            const QString size = planeSizeText(render::ProjectGeometry(layout).romFrameSize());
            m_createHdButton->setText(tr("Create %1 Sprite").arg(size));
            m_createHdButton->setToolTip(
                tr("Create the %1 sprite from the sprite original in its plain colors")
                    .arg(size));
        }
        m_createHdButton->setVisible(layout != render::PlaneLayout::NativeOriginal);
        m_removeHdButton->setVisible(layout == render::PlaneLayout::Ordinary);
        m_createHdButton->setEnabled(hasSprite && !spriteHasHd);
        m_removeHdButton->setEnabled(hasSprite && spriteHasHd);
        //: %1: "HD", or the plane's size such as 256x64
        m_transparentPaintButton->setToolTip(
            tr("On the sprite canvas, strokes make pixels of the %1 sprite transparent (Shift: "
               "cover them again where the original is). The original, which the sprite is "
               "found by, stays")
                .arg(layout == render::PlaneLayout::NativeExtra
                         ? planeSizeText(render::ProjectGeometry(layout).romFrameSize())
                         : QStringLiteral("HD")));
        m_transparentPaintButton->setVisible(layout != render::PlaneLayout::NativeOriginal);
        m_transparentPaintButton->setEnabled(hasSprite && spriteHasHd);
    }
    for (QWidget* widget : std::initializer_list<QWidget*>{
             m_shapeModeCheckBox, m_dynamicPaintButton, m_dynamicSetSpinBox, m_dynamicColorGrid,
             m_editColorsToSetButton, m_setToEditColorsButton}) {
        widget->setEnabled(hasSprite);
    }
    if (!hasSprite) {
        m_dynamicColorGrid->setColors({}, 0);
        return;
    }
    {
        QSignalBlocker blocker(m_shapeModeCheckBox);
        m_shapeModeCheckBox->setChecked(document->sprite(*spriteIndex).shapeMode > 0);
    }
    std::vector<uint16_t> colors;
    for (std::size_t shade = 0; shade < document->colorCount(); ++shade) {
        colors.push_back(document->spriteDynamicColor(*spriteIndex, dynamicSet(), shade));
    }
    m_dynamicColorGrid->setColors(colors, colors.size());
}

void SpritePanel::refresh(const SpritePanelState& state)
{
    refreshDynamic(state.document, state.spriteIndex);
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
                    reference.spriteId == document::kNoSpriteId
                        ? QString()
                        : static_cast<std::size_t>(reference.spriteId) >=
                                document->spriteCount()
                            ? tr("Sprite %1")
                                  .arg(static_cast<int>(reference.spriteId))
                        : spriteDisplayName(*document, reference.spriteId);
                m_slotComboBox->addItem(
                    reference.spriteId == document::kNoSpriteId
                        ? tr("Slot %1: Empty").arg(static_cast<int>(slot))
                        : tr("Slot %1: %2")
                              .arg(static_cast<int>(slot))
                              .arg(assignedSprite),
                    static_cast<int>(slot));
            }
        }
        if (m_slotComboBox->count() == 0) {
            m_slotComboBox->addItem(tr("No slots"), -1);
            m_slotComboBox->setCurrentIndex(0);
        } else if (previousSlotData.isValid()) {
            const int previousIndex = m_slotComboBox->findData(previousSlotData);
            m_slotComboBox->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        } else {
            m_slotComboBox->setCurrentIndex(0);
        }
    }
    m_slotComboBox->setEnabled(document && frameIndex.has_value());

    std::size_t usedSlots = 0;
    if (!document || !frameIndex) {
        m_frameSpritesLabel->setText(tr("No frame sprites"));
    } else {
        const std::vector<document::FrameSpriteReference>& references =
            document->frameSpriteReferences(*frameIndex);
        QStringList assignedSprites;
        for (std::size_t slot = 0; slot < references.size(); ++slot) {
            const document::FrameSpriteReference& reference = references[slot];
            if (reference.spriteId == document::kNoSpriteId) {
                continue;
            }
            ++usedSlots;
            const QString assignedSprite =
                reference.spriteId >= document->spriteCount()
                    ? tr("sprite %1").arg(static_cast<int>(reference.spriteId))
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
                                   .arg(hdMissing ? tr(" %1: frame not rendered")
                                                        .arg(noPictureText())
                                                  : QString());
        }
        m_frameSpritesLabel->setText(
            assignedSprites.isEmpty()
                ? tr("Current frame uses no sprites")
                : assignedSprites.join(QStringLiteral("\n")));
    }
    if (usedSlots > document::kSpritesPerFrameWarning) {
        m_spriteCountWarningLabel->setText(
            tr("The frame uses %1 sprites.").arg(usedSlots) + QLatin1Char(' ') +
            manySpritesAdvice());
        m_spriteCountWarningLabel->show();
    } else {
        m_spriteCountWarningLabel->clear();
        m_spriteCountWarningLabel->hide();
    }

    const bool hasSprite =
        document && state.spriteIndex && *state.spriteIndex < document->spriteCount();
    const std::optional<std::size_t> slotIndex = selectedSlotIndex();
    const bool severalSprites = state.selectedSpriteCount > 1;
    m_assignButton->setEnabled(document && frameIndex && hasSprite &&
                               (slotIndex || severalSprites));
    m_assignButton->setToolTip(
        severalSprites ? tr("Put each selected sprite on the selected frames: a frame keeps a "
                            "sprite it has, others go to its first empty slots")
                       : tr("Assign selected sprite to selected slot"));
    m_captureButton->setEnabled(document && frameIndex && hasSprite && slotIndex);
    m_captureSelectionButton->setEnabled(m_captureButton->isEnabled());
    m_clearCaptureSelectionButton->setEnabled(state.hasCaptureSelection);
    bool slotHasSprite = false;
    std::optional<document::FrameSpriteReference> selectedReference;
    if (document && frameIndex && slotIndex) {
        const std::vector<document::FrameSpriteReference>& references =
            document->frameSpriteReferences(*frameIndex);
        if (*slotIndex < references.size()) {
            selectedReference = references[*slotIndex];
            slotHasSprite = selectedReference->spriteId != document::kNoSpriteId;
        }
    }
    m_removeButton->setEnabled(slotHasSprite);
    // A neighbour that holds a sprite to change places with.
    const auto usedSlot = [&](std::size_t slot) {
        const std::vector<document::FrameSpriteReference>& references =
            document->frameSpriteReferences(*frameIndex);
        return slot < references.size() && references[slot].spriteId != document::kNoSpriteId;
    };
    m_moveUpButton->setEnabled(slotHasSprite && *slotIndex > 0 && usedSlot(*slotIndex - 1));
    m_moveDownButton->setEnabled(slotHasSprite && usedSlot(*slotIndex + 1));
    m_applySpriteEditsButton->setEnabled(state.spriteEditsPending);
    const bool hasSlot = selectedReference.has_value();
    m_boundsOverlayButton->setEnabled(hasSlot);
    {
        QSignalBlocker minXBlocker(m_minXSpinBox);
        QSignalBlocker minYBlocker(m_minYSpinBox);
        QSignalBlocker maxXBlocker(m_maxXSpinBox);
        QSignalBlocker maxYBlocker(m_maxYSpinBox);
        QSignalBlocker offsetXBlocker(m_offsetXSpinBox);
        QSignalBlocker offsetYBlocker(m_offsetYSpinBox);
        // Bounds are pixels of the original frame.
        const render::Size originalSize =
            document ? document->originalFrameSize() : render::kSdFrameSize;
        m_minXSpinBox->setRange(0, originalSize.width - 1);
        m_minYSpinBox->setRange(0, originalSize.height - 1);
        m_maxXSpinBox->setRange(0, originalSize.width - 1);
        m_maxYSpinBox->setRange(0, originalSize.height - 1);
        // An offset that moves the sprite out of the frame entirely draws nothing.
        m_offsetXSpinBox->setRange(-(originalSize.width - 1), originalSize.width - 1);
        m_offsetYSpinBox->setRange(-(originalSize.height - 1), originalSize.height - 1);
        // Bounds belong to an assigned sprite; an empty slot has none to edit.
        m_minXSpinBox->setEnabled(slotHasSprite);
        m_minYSpinBox->setEnabled(slotHasSprite);
        m_maxXSpinBox->setEnabled(slotHasSprite);
        m_maxYSpinBox->setEnabled(slotHasSprite);
        m_offsetXSpinBox->setEnabled(slotHasSprite);
        m_offsetYSpinBox->setEnabled(slotHasSprite);
        m_offsetXSpinBox->setValue(slotHasSprite ? selectedReference->offsetX : 0);
        m_offsetYSpinBox->setValue(slotHasSprite ? selectedReference->offsetY : 0);
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

    const std::optional<std::size_t> sourceFrame =
        document && hasSprite && *state.spriteIndex < document->spriteCount() &&
                document->spriteSourceFrame(*state.spriteIndex) < document->frameCount()
            ? std::optional<std::size_t>(document->spriteSourceFrame(*state.spriteIndex))
            : std::nullopt;
    for (QToolButton* button : {m_sourceFrameButton, m_editorSourceFrameButton}) {
        button->setEnabled(sourceFrame.has_value());
        button->setToolTip(sourceFrame
                               ? tr("Go to frame %1, the frame the sprite was taken from")
                                     .arg(static_cast<qulonglong>(*sourceFrame))
                               : tr("Go to the frame the sprite was taken from"));
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
        m_selectedSpriteLabel->setText(tr("Selected: None"));
        m_editedSpriteLabel->setText(tr("Selected: None"));
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
        tr("Selected: %1%2")
            .arg(label)
            .arg(extraDataSuffix(*document, sprite.hasHd)));
    m_editedSpriteLabel->setText(m_selectedSpriteLabel->text());
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

std::pair<int, int> SpritePanel::slotOffset() const
{
    return {m_offsetXSpinBox->value(), m_offsetYSpinBox->value()};
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

bool SpritePanel::isOutlinesChecked() const
{
    return m_outlinesButton->isChecked();
}

bool SpritePanel::isSpriteCanvasChecked() const
{
    return m_spriteCanvasButton->isChecked();
}

QString SpritePanel::manySpritesAdvice()
{
    return tr("Sprites work the same past %1 per frame, but every sprite more adds to the "
              "time libserum needs for each frame. Stay within %1 wherever you can: use more "
              "only on a frame that cannot be done otherwise, never as a habit. In the old "
              "Williams System 3 to 6 games, such as Flash, a few milliseconds of latency do "
              "not hurt; in a video mode avoid it.")
        .arg(document::kSpritesPerFrameWarning);
}

QString SpritePanel::spriteCountWarning() const
{
    return m_spriteCountWarningLabel->text();
}

}  // namespace colorizing::app
