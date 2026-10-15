#include "panels/FramePanel.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "PlaneLabels.h"

namespace colorizing::app {

FramePanel::FramePanel(QWidget* parent)
    : QWidget(parent)
{
    m_paintTargetComboBox = new QComboBox(this);
    m_paintTargetComboBox->setToolTip(tr("Drawing target"));
    m_paintTargetComboBox->addItem(tr("Frame"),
                                   static_cast<int>(PaintTarget::Frame));
    m_paintTargetComboBox->addItem(tr("Background"),
                                   static_cast<int>(PaintTarget::Background));
    m_paintTargetComboBox->setEnabled(false);
    m_backgroundIdSpinBox = new QSpinBox(this);
    m_backgroundIdSpinBox->setToolTip(tr("Assigned background ID"));
    m_backgroundIdSpinBox->setMinimum(-1);
    m_backgroundIdSpinBox->setMaximum(-1);
    m_backgroundIdSpinBox->setSpecialValueText(tr("None"));
    m_backgroundIdSpinBox->setEnabled(false);
    m_triggerIdSpinBox = new QSpinBox(this);
    m_triggerIdSpinBox->setToolTip(tr("Per-frame trigger ID"));
    m_triggerIdSpinBox->setRange(-1, kMaxManualTriggerId);
    m_triggerIdSpinBox->setSpecialValueText(tr("None"));
    m_triggerIdSpinBox->setValue(-1);
    m_triggerIdSpinBox->setEnabled(false);
    m_durationSpinBox = new QSpinBox(this);
    m_durationSpinBox->setToolTip(tr("Per-frame cRP duration"));
    m_durationSpinBox->setRange(0, kMaxFrameDurationMs);
    m_durationSpinBox->setSingleStep(10);
    m_durationSpinBox->setSuffix(tr(" ms"));
    m_durationSpinBox->setEnabled(false);
    m_switchToMonochromeCheckBox = new QCheckBox(tr("Switch mono"), this);
    m_switchToMonochromeCheckBox->setToolTip(
        tr("Use legacy switch-to-monochrome trigger ID 65432"));
    m_switchToMonochromeCheckBox->setEnabled(false);
    m_backgroundLayerButton = new QToolButton(this);
    m_backgroundLayerButton->setText(tr("Show BG"));
    m_backgroundLayerButton->setToolTip(tr("Show assigned background layer"));
    m_backgroundLayerButton->setCheckable(true);
    m_backgroundLayerButton->setChecked(true);
    m_backgroundLayerButton->setEnabled(false);
    m_importImageButton = new QToolButton(this);
    m_importImageButton->setText(tr("Import Image"));
    m_importImageButton->setToolTip(tr("Import an image into selected frame pixels"));
    m_importImageButton->setEnabled(false);
    m_createHdButton = new QToolButton(this);
    m_createHdButton->setText(tr("Create HD"));
    m_createHdButton->setToolTip(tr("Create HD frame from SD frame"));
    m_createHdButton->setEnabled(false);
    m_removeHdButton = new QToolButton(this);
    m_removeHdButton->setText(tr("Remove HD"));
    m_removeHdButton->setToolTip(tr("Remove HD frame data"));
    m_removeHdButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    auto* form = new QFormLayout();
    form->addRow(tr("Target"), m_paintTargetComboBox);
    form->addRow(tr("BG ID"), m_backgroundIdSpinBox);
    form->addRow(tr("Trigger ID"), m_triggerIdSpinBox);
    form->addRow(tr("Duration"), m_durationSpinBox);
    form->addRow(QString(), m_switchToMonochromeCheckBox);
    pageLayout->addLayout(form);
    pageLayout->addWidget(m_backgroundLayerButton);
    pageLayout->addWidget(m_importImageButton);
    auto* hdButtonLayout = new QHBoxLayout();
    hdButtonLayout->addWidget(m_createHdButton);
    hdButtonLayout->addWidget(m_removeHdButton);
    pageLayout->addLayout(hdButtonLayout);
    pageLayout->addStretch(1);

    connect(m_backgroundLayerButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onBackgroundLayerToggled) {
            onBackgroundLayerToggled(checked);
        }
    });
    connect(m_importImageButton, &QToolButton::clicked, this, [this]() {
        if (onImportImageRequested) {
            onImportImageRequested();
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
    connect(m_paintTargetComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onPaintTargetChanged) {
                    onPaintTargetChanged();
                }
            });
    connect(m_backgroundIdSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int backgroundId) {
                if (onBackgroundIdEdited) {
                    onBackgroundIdEdited(backgroundId);
                }
            });
    connect(m_triggerIdSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int triggerId) {
                if (onTriggerIdEdited) {
                    onTriggerIdEdited(triggerId);
                }
            });
    connect(m_durationSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int durationMs) {
                if (onDurationEdited) {
                    onDurationEdited(durationMs);
                }
            });
    connect(m_switchToMonochromeCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (onSwitchToMonochromeToggled) {
            onSwitchToMonochromeToggled(checked);
        }
    });
}

void FramePanel::refresh(const FramePanelState& state)
{
    const document::ProjectDocument* document = state.document;
    QSignalBlocker backgroundBlocker(m_backgroundIdSpinBox);
    QSignalBlocker triggerBlocker(m_triggerIdSpinBox);
    QSignalBlocker durationBlocker(m_durationSpinBox);
    QSignalBlocker monoBlocker(m_switchToMonochromeCheckBox);

    if (!document || !state.frameIndex) {
        m_backgroundIdSpinBox->setRange(-1, -1);
        m_backgroundIdSpinBox->setValue(-1);
        m_backgroundIdSpinBox->setEnabled(false);
        m_triggerIdSpinBox->setValue(-1);
        m_triggerIdSpinBox->setEnabled(false);
        m_triggerIdSpinBox->setToolTip(tr("Per-frame trigger ID"));
        m_durationSpinBox->setValue(0);
        m_durationSpinBox->setEnabled(false);
        m_durationSpinBox->setToolTip(tr("Per-frame cRP duration"));
        m_switchToMonochromeCheckBox->setChecked(false);
        m_switchToMonochromeCheckBox->setEnabled(false);
        m_backgroundLayerButton->setEnabled(false);
        m_importImageButton->setEnabled(false);
        showFrameHdActions(false, false);
        return;
    }

    const std::size_t frameIndex = *state.frameIndex;
    const int maxBackgroundId = document->backgroundCount() == 0
                                    ? -1
                                    : static_cast<int>(document->backgroundCount() - 1);
    m_backgroundIdSpinBox->setRange(-1, maxBackgroundId);
    int backgroundValue = -1;
    const std::optional<document::FrameBackgroundData>& backgroundData =
        document->backgroundData(frameIndex);
    if (backgroundData && backgroundData->backgroundId <= maxBackgroundId) {
        backgroundValue = static_cast<int>(backgroundData->backgroundId);
    }
    m_backgroundIdSpinBox->setValue(backgroundValue);
    m_backgroundIdSpinBox->setEnabled(backgroundData.has_value() && maxBackgroundId >= 0);
    const uint32_t triggerId = document->triggerId(frameIndex);
    const bool switchToMonochrome = triggerId == kSwitchToMonochromeTriggerId;
    m_switchToMonochromeCheckBox->setChecked(switchToMonochrome);
    m_switchToMonochromeCheckBox->setEnabled(true);
    const bool manualTriggerId = triggerId <= static_cast<uint32_t>(kMaxManualTriggerId);
    const bool unsupportedTriggerId =
        !manualTriggerId && !switchToMonochrome && triggerId != document::kNoTriggerId;
    m_triggerIdSpinBox->setEnabled(!switchToMonochrome);
    m_triggerIdSpinBox->setValue(manualTriggerId ? static_cast<int>(triggerId)
                                                 : unsupportedTriggerId ? 0 : -1);
    m_triggerIdSpinBox->setToolTip(
        unsupportedTriggerId
            ? tr("Unsupported legacy trigger ID %1 is preserved until edited")
                  .arg(triggerId)
            : tr("Per-frame trigger ID"));
    const uint32_t durationMs = document->frameDurationMs(frameIndex);
    m_durationSpinBox->setEnabled(true);
    m_durationSpinBox->setValue(durationMs <= static_cast<uint32_t>(kMaxFrameDurationMs)
                                    ? static_cast<int>(durationMs)
                                    : 0);
    m_durationSpinBox->setToolTip(
        durationMs <= static_cast<uint32_t>(kMaxFrameDurationMs)
            ? tr("Per-frame cRP duration")
            : tr("Unsupported cRP duration %1 ms is preserved until edited")
                  .arg(durationMs));
    m_backgroundLayerButton->setEnabled(backgroundData.has_value() && maxBackgroundId >= 0);
    m_importImageButton->setEnabled(state.hasSelectedFrames);
    const bool hasHdFrame = document->hasHdFrame(frameIndex);
    showFrameHdActions(!hasHdFrame, hasHdFrame);
    if (document->rendersHdPlaneOnly()) {
        const document::FrameHdGaps gaps = document->frameHdGaps(frameIndex);
        const QString size = nativePlaneText(*document);
        m_createHdButton->setText(tr("Create %1 Frame").arg(size));
        m_createHdButton->setToolTip(
            tr("Create what the frame lacks to be rendered: the %1 frame from the "
               "original frame in its plain colors, a black %1 image for the "
               "assigned background")
                .arg(size));
        m_createHdButton->setEnabled(gaps.frame || gaps.background);
    }
    showHdActionsFor(document->geometry().layout());
}

void FramePanel::showHdActionsFor(render::PlaneLayout layout)
{
    const bool canCreate = layout != render::PlaneLayout::NativeOriginal;
    const bool canRemove = layout == render::PlaneLayout::Ordinary;
    m_createHdButton->setVisible(canCreate);
    m_removeHdButton->setVisible(canRemove);
    if (!canCreate) {
        m_createHdButton->setEnabled(false);
    }
    if (!canRemove) {
        m_removeHdButton->setEnabled(false);
    }
}

void FramePanel::showFrameHdActions(bool canCreate, bool canRemove)
{
    m_createHdButton->setText(tr("Create HD"));
    m_createHdButton->setToolTip(tr("Create HD frame from SD frame"));
    m_createHdButton->setEnabled(canCreate);
    m_removeHdButton->setText(tr("Remove HD"));
    m_removeHdButton->setToolTip(tr("Remove HD frame data"));
    m_removeHdButton->setEnabled(canRemove);
    showHdActionsFor(render::PlaneLayout::Ordinary);
}

void FramePanel::setPaintTarget(PaintTarget target)
{
    const int index = m_paintTargetComboBox->findData(static_cast<int>(target));
    if (index >= 0) {
        QSignalBlocker blocker(m_paintTargetComboBox);
        m_paintTargetComboBox->setCurrentIndex(index);
    }
}

void FramePanel::setPaintTargetEnabled(bool enabled)
{
    m_paintTargetComboBox->setEnabled(enabled);
}

void FramePanel::setImportImageEnabled(bool enabled)
{
    m_importImageButton->setEnabled(enabled);
}

void FramePanel::showSpriteHdActions(bool spriteHasHd, render::PlaneLayout layout)
{
    if (layout == render::PlaneLayout::NativeExtra) {
        const QString size = planeSizeText(render::ProjectGeometry(layout).romFrameSize());
        m_createHdButton->setText(tr("Create %1 Sprite").arg(size));
        m_createHdButton->setToolTip(
            tr("Create the %1 sprite from the sprite original in its plain colors")
                .arg(size));
    } else {
        m_createHdButton->setText(tr("Create HD Sprite"));
        m_createHdButton->setToolTip(tr("Create HD sprite from SD sprite"));
    }
    m_createHdButton->setEnabled(!spriteHasHd);
    m_removeHdButton->setText(tr("Remove HD Sprite"));
    m_removeHdButton->setToolTip(tr("Remove HD sprite data"));
    m_removeHdButton->setEnabled(spriteHasHd);
    showHdActionsFor(layout);
}

PaintTarget FramePanel::paintTarget() const
{
    return static_cast<PaintTarget>(m_paintTargetComboBox->currentData().toInt());
}

int FramePanel::triggerId() const
{
    return m_triggerIdSpinBox->value();
}

bool FramePanel::isSwitchToMonochromeChecked() const
{
    return m_switchToMonochromeCheckBox->isChecked();
}

bool FramePanel::isBackgroundLayerChecked() const
{
    return m_backgroundLayerButton->isChecked();
}

}  // namespace colorizing::app
