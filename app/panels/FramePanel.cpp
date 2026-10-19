#include "panels/FramePanel.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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
    m_pupTriggerSpinBox = new QSpinBox(this);
    m_pupTriggerSpinBox->setToolTip(
        tr("PUP trigger the frame fires, 0 to %1").arg(document::kLastPupTriggerId));
    m_pupTriggerSpinBox->setRange(-1, static_cast<int>(document::kLastPupTriggerId));
    m_pupTriggerSpinBox->setSpecialValueText(tr("None"));
    m_pupTriggerSpinBox->setValue(-1);
    m_pupTriggerSpinBox->setEnabled(false);
    m_sceneComboBox = new QComboBox(this);
    m_sceneComboBox->setToolTip(tr("Scene of the project the frame starts"));
    m_sceneComboBox->addItem(tr("None"), -1);
    m_sceneComboBox->setEnabled(false);
    m_monochromeCheckBox = new QCheckBox(tr("Monochrome fallback"), this);
    m_monochromeCheckBox->setToolTip(
        tr("From this frame on, libserum shows the frames in monochrome (trigger ID %1)")
            .arg(document::kMonochromeTriggerId));
    m_monochromeCheckBox->setEnabled(false);
    m_paletteMonochromeCheckBox = new QCheckBox(tr("Palette-based monochrome fallback"), this);
    m_paletteMonochromeCheckBox->setToolTip(
        tr("From this frame on, libserum shows the frames in the colors of this frame's "
           "palette (trigger ID %1)")
            .arg(document::kPaletteMonochromeTriggerId));
    m_paletteMonochromeCheckBox->setEnabled(false);
    m_otherTriggerLabel = new QLabel(this);
    m_otherTriggerLabel->setWordWrap(true);
    m_otherTriggerLabel->setVisible(false);
    m_durationSpinBox = new QSpinBox(this);
    m_durationSpinBox->setToolTip(tr("Per-frame cRP duration"));
    m_durationSpinBox->setRange(0, kMaxFrameDurationMs);
    m_durationSpinBox->setSingleStep(10);
    m_durationSpinBox->setSuffix(tr(" ms"));
    m_durationSpinBox->setEnabled(false);
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
    form->addRow(tr("Background ID"), m_backgroundIdSpinBox);
    form->addRow(tr("PUP trigger ID"), m_pupTriggerSpinBox);
    form->addRow(tr("Scene"), m_sceneComboBox);
    form->addRow(QString(), m_monochromeCheckBox);
    form->addRow(QString(), m_paletteMonochromeCheckBox);
    form->addRow(QString(), m_otherTriggerLabel);
    form->addRow(tr("Duration"), m_durationSpinBox);
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
    const auto report = [this](document::TriggerKind kind, uint32_t triggerId) {
        if (onTriggerEdited) {
            onTriggerEdited(kind, triggerId);
        }
    };
    connect(m_pupTriggerSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [report](int triggerId) {
                report(document::TriggerKind::Pup,
                       triggerId < 0 ? document::kNoTriggerId : static_cast<uint32_t>(triggerId));
            });
    connect(m_sceneComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this, report](int) {
                const int sceneId = m_sceneComboBox->currentData().toInt();
                report(document::TriggerKind::Scene,
                       sceneId < 0 ? document::kNoTriggerId : static_cast<uint32_t>(sceneId));
            });
    connect(m_monochromeCheckBox, &QCheckBox::toggled, this, [report](bool checked) {
        report(document::TriggerKind::Monochrome,
               checked ? document::kMonochromeTriggerId : document::kNoTriggerId);
    });
    connect(m_paletteMonochromeCheckBox, &QCheckBox::toggled, this, [report](bool checked) {
        report(document::TriggerKind::PaletteMonochrome,
               checked ? document::kPaletteMonochromeTriggerId : document::kNoTriggerId);
    });
    connect(m_durationSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int durationMs) {
                if (onDurationEdited) {
                    onDurationEdited(durationMs);
                }
            });
}

void FramePanel::fillScenes(const document::ProjectDocument* document, uint32_t triggerId)
{
    m_sceneComboBox->clear();
    m_sceneComboBox->addItem(tr("None"), -1);
    bool listed = false;
    if (document && document->colorizationSettings()) {
        for (const document::SceneDefinition& scene : document->colorizationSettings()->scenes) {
            m_sceneComboBox->addItem(QString::number(scene.sceneId),
                                     static_cast<int>(scene.sceneId));
            listed = listed || scene.sceneId == triggerId;
        }
    }
    if (document::triggerKind(triggerId) == document::TriggerKind::Scene && !listed) {
        // Shown as it is; it cannot be chosen for another frame.
        m_sceneComboBox->addItem(tr("%1 (no such scene)").arg(triggerId),
                                 static_cast<int>(triggerId));
    }
    const int current = m_sceneComboBox->findData(
        document::triggerKind(triggerId) == document::TriggerKind::Scene
            ? static_cast<int>(triggerId)
            : -1);
    m_sceneComboBox->setCurrentIndex(std::max(0, current));
}

void FramePanel::refresh(const FramePanelState& state)
{
    const document::ProjectDocument* document = state.document;
    QSignalBlocker backgroundBlocker(m_backgroundIdSpinBox);
    QSignalBlocker pupBlocker(m_pupTriggerSpinBox);
    QSignalBlocker sceneBlocker(m_sceneComboBox);
    QSignalBlocker monoBlocker(m_monochromeCheckBox);
    QSignalBlocker paletteMonoBlocker(m_paletteMonochromeCheckBox);
    QSignalBlocker durationBlocker(m_durationSpinBox);

    if (!document || !state.frameIndex) {
        m_backgroundIdSpinBox->setRange(-1, -1);
        m_backgroundIdSpinBox->setValue(-1);
        m_backgroundIdSpinBox->setEnabled(false);
        m_triggerId = document::kNoTriggerId;
        m_pupTriggerSpinBox->setValue(-1);
        m_pupTriggerSpinBox->setEnabled(false);
        fillScenes(nullptr, document::kNoTriggerId);
        m_sceneComboBox->setEnabled(false);
        m_monochromeCheckBox->setChecked(false);
        m_monochromeCheckBox->setEnabled(false);
        m_paletteMonochromeCheckBox->setChecked(false);
        m_paletteMonochromeCheckBox->setEnabled(false);
        m_otherTriggerLabel->setVisible(false);
        m_durationSpinBox->setValue(0);
        m_durationSpinBox->setEnabled(false);
        m_durationSpinBox->setToolTip(tr("Per-frame cRP duration"));
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
    // One trigger per frame: while the frame has one, only the input of its kind is open (an
    // ID of no kind leaves all of them open, any of them replaces it).
    const uint32_t triggerId = document->triggerId(frameIndex);
    m_triggerId = triggerId;
    const document::TriggerKind kind = document::triggerKind(triggerId);
    const auto open = [kind](document::TriggerKind input) {
        return kind == input || kind == document::TriggerKind::None ||
               kind == document::TriggerKind::Other;
    };
    m_pupTriggerSpinBox->setValue(kind == document::TriggerKind::Pup ? static_cast<int>(triggerId)
                                                                     : -1);
    m_pupTriggerSpinBox->setEnabled(open(document::TriggerKind::Pup));
    fillScenes(document, triggerId);
    m_sceneComboBox->setEnabled(open(document::TriggerKind::Scene) &&
                                m_sceneComboBox->count() > 1);
    m_monochromeCheckBox->setChecked(kind == document::TriggerKind::Monochrome);
    m_monochromeCheckBox->setEnabled(open(document::TriggerKind::Monochrome));
    m_paletteMonochromeCheckBox->setChecked(kind == document::TriggerKind::PaletteMonochrome);
    m_paletteMonochromeCheckBox->setEnabled(open(document::TriggerKind::PaletteMonochrome));
    m_otherTriggerLabel->setText(
        tr("Trigger ID %1 is kept until another trigger is chosen").arg(triggerId));
    m_otherTriggerLabel->setVisible(kind == document::TriggerKind::Other);
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

uint32_t FramePanel::triggerId() const
{
    return m_triggerId;
}

bool FramePanel::isBackgroundLayerChecked() const
{
    return m_backgroundLayerButton->isChecked();
}

}  // namespace colorizing::app
