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

namespace colorizing::app {

FramePanel::FramePanel(QWidget* parent)
    : QWidget(parent)
{
    m_paintTargetComboBox = new QComboBox(this);
    m_paintTargetComboBox->setToolTip(QStringLiteral("Drawing target"));
    m_paintTargetComboBox->addItem(QStringLiteral("Frame"),
                                   static_cast<int>(PaintTarget::Frame));
    m_paintTargetComboBox->addItem(QStringLiteral("Background"),
                                   static_cast<int>(PaintTarget::Background));
    m_paintTargetComboBox->setEnabled(false);
    m_backgroundIdSpinBox = new QSpinBox(this);
    m_backgroundIdSpinBox->setToolTip(QStringLiteral("Assigned background ID"));
    m_backgroundIdSpinBox->setMinimum(-1);
    m_backgroundIdSpinBox->setMaximum(-1);
    m_backgroundIdSpinBox->setSpecialValueText(QStringLiteral("None"));
    m_backgroundIdSpinBox->setEnabled(false);
    m_triggerIdSpinBox = new QSpinBox(this);
    m_triggerIdSpinBox->setToolTip(QStringLiteral("Per-frame trigger ID"));
    m_triggerIdSpinBox->setRange(-1, kMaxManualTriggerId);
    m_triggerIdSpinBox->setSpecialValueText(QStringLiteral("None"));
    m_triggerIdSpinBox->setValue(-1);
    m_triggerIdSpinBox->setEnabled(false);
    m_durationSpinBox = new QSpinBox(this);
    m_durationSpinBox->setToolTip(QStringLiteral("Per-frame cRP duration"));
    m_durationSpinBox->setRange(0, kMaxFrameDurationMs);
    m_durationSpinBox->setSingleStep(10);
    m_durationSpinBox->setSuffix(QStringLiteral(" ms"));
    m_durationSpinBox->setEnabled(false);
    m_switchToMonochromeCheckBox = new QCheckBox(QStringLiteral("Switch mono"), this);
    m_switchToMonochromeCheckBox->setToolTip(
        QStringLiteral("Use legacy switch-to-monochrome trigger ID 65432"));
    m_switchToMonochromeCheckBox->setEnabled(false);
    m_dmdPixelShapeComboBox = new QComboBox(this);
    m_dmdPixelShapeComboBox->setToolTip(
        QStringLiteral("DMD pixel shape for canvas and PNG export"));
    m_dmdPixelShapeComboBox->addItem(QStringLiteral("Squares"),
                                     static_cast<int>(DmdPixelShape::Square));
    m_dmdPixelShapeComboBox->addItem(QStringLiteral("Dots"),
                                     static_cast<int>(DmdPixelShape::Dot));
    m_dmdPixelSizeSpinBox = new QSpinBox(this);
    m_dmdPixelSizeSpinBox->setToolTip(QStringLiteral("Relative DMD pixel size"));
    m_dmdPixelSizeSpinBox->setRange(50, 100);
    m_dmdPixelSizeSpinBox->setSingleStep(10);
    m_dmdPixelSizeSpinBox->setSuffix(QStringLiteral("%"));
    m_dmdPixelSizeSpinBox->setValue(100);
    m_backgroundLayerButton = new QToolButton(this);
    m_backgroundLayerButton->setText(QStringLiteral("Show BG"));
    m_backgroundLayerButton->setToolTip(QStringLiteral("Show assigned background layer"));
    m_backgroundLayerButton->setCheckable(true);
    m_backgroundLayerButton->setChecked(true);
    m_backgroundLayerButton->setEnabled(false);
    m_importImageButton = new QToolButton(this);
    m_importImageButton->setText(QStringLiteral("Import Image"));
    m_importImageButton->setToolTip(QStringLiteral("Import an image into selected frame pixels"));
    m_importImageButton->setEnabled(false);
    m_createHdButton = new QToolButton(this);
    m_createHdButton->setText(QStringLiteral("Create HD"));
    m_createHdButton->setToolTip(QStringLiteral("Create HD frame from SD frame"));
    m_createHdButton->setEnabled(false);
    m_removeHdButton = new QToolButton(this);
    m_removeHdButton->setText(QStringLiteral("Remove HD"));
    m_removeHdButton->setToolTip(QStringLiteral("Remove HD frame data"));
    m_removeHdButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Target"), m_paintTargetComboBox);
    form->addRow(QStringLiteral("BG ID"), m_backgroundIdSpinBox);
    form->addRow(QStringLiteral("Trigger ID"), m_triggerIdSpinBox);
    form->addRow(QStringLiteral("Duration"), m_durationSpinBox);
    form->addRow(QString(), m_switchToMonochromeCheckBox);
    form->addRow(QStringLiteral("DMD Shape"), m_dmdPixelShapeComboBox);
    form->addRow(QStringLiteral("DMD Size"), m_dmdPixelSizeSpinBox);
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
    connect(m_dmdPixelShapeComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onDmdRenderOptionsChanged) {
                    onDmdRenderOptionsChanged();
                }
            });
    connect(m_dmdPixelSizeSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int value) {
                // The size snaps to steps of ten.
                const int roundedValue = std::clamp(((value + 5) / 10) * 10, 50, 100);
                if (roundedValue != value) {
                    QSignalBlocker blocker(m_dmdPixelSizeSpinBox);
                    m_dmdPixelSizeSpinBox->setValue(roundedValue);
                }
                if (onDmdRenderOptionsChanged) {
                    onDmdRenderOptionsChanged();
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
        m_triggerIdSpinBox->setToolTip(QStringLiteral("Per-frame trigger ID"));
        m_durationSpinBox->setValue(0);
        m_durationSpinBox->setEnabled(false);
        m_durationSpinBox->setToolTip(QStringLiteral("Per-frame cRP duration"));
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
            ? QStringLiteral("Unsupported legacy trigger ID %1 is preserved until edited")
                  .arg(triggerId)
            : QStringLiteral("Per-frame trigger ID"));
    const uint32_t durationMs = document->frameDurationMs(frameIndex);
    m_durationSpinBox->setEnabled(true);
    m_durationSpinBox->setValue(durationMs <= static_cast<uint32_t>(kMaxFrameDurationMs)
                                    ? static_cast<int>(durationMs)
                                    : 0);
    m_durationSpinBox->setToolTip(
        durationMs <= static_cast<uint32_t>(kMaxFrameDurationMs)
            ? QStringLiteral("Per-frame cRP duration")
            : QStringLiteral("Unsupported cRP duration %1 ms is preserved until edited")
                  .arg(durationMs));
    m_backgroundLayerButton->setEnabled(backgroundData.has_value() && maxBackgroundId >= 0);
    m_importImageButton->setEnabled(state.hasSelectedFrames);
    const bool hasHdFrame = document->hasHdFrame(frameIndex);
    showFrameHdActions(!hasHdFrame, hasHdFrame);
}

void FramePanel::showFrameHdActions(bool canCreate, bool canRemove)
{
    m_createHdButton->setText(QStringLiteral("Create HD"));
    m_createHdButton->setToolTip(QStringLiteral("Create HD frame from SD frame"));
    m_createHdButton->setEnabled(canCreate);
    m_removeHdButton->setText(QStringLiteral("Remove HD"));
    m_removeHdButton->setToolTip(QStringLiteral("Remove HD frame data"));
    m_removeHdButton->setEnabled(canRemove);
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

void FramePanel::showSpriteHdActions(bool spriteHasHd)
{
    m_createHdButton->setText(QStringLiteral("Create HD Sprite"));
    m_createHdButton->setToolTip(QStringLiteral("Create HD sprite from SD sprite"));
    m_createHdButton->setEnabled(!spriteHasHd);
    m_removeHdButton->setText(QStringLiteral("Remove HD Sprite"));
    m_removeHdButton->setToolTip(QStringLiteral("Remove HD sprite data"));
    m_removeHdButton->setEnabled(spriteHasHd);
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

DmdRenderOptions FramePanel::dmdRenderOptions() const
{
    DmdRenderOptions options;
    options.pixelShape =
        static_cast<DmdPixelShape>(m_dmdPixelShapeComboBox->currentData().toInt());
    options.pixelCoverage = static_cast<double>(m_dmdPixelSizeSpinBox->value()) / 100.0;
    return options;
}

}  // namespace colorizing::app
