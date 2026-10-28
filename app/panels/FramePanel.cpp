#include "panels/FramePanel.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringListModel>
#include <QToolButton>
#include <QVBoxLayout>

#include "panels/InspectorForm.h"
#include "panels/SceneSettingsForm.h"
#include "PlaneLabels.h"
#include "edit/FrameTags.h"

namespace colorizing::app {

FramePanel::FramePanel(QWidget* parent)
    : QWidget(parent)
{
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
    m_durationSpinBox->setToolTip(tr("Per-frame duration"));
    m_durationSpinBox->setRange(0, kMaxFrameDurationMs);
    m_durationSpinBox->setSingleStep(10);
    m_durationSpinBox->setSuffix(tr(" ms"));
    m_durationSpinBox->setEnabled(false);
    // Tags: groups of frames ("Multiball", "Settings menu") to filter the frame strip by.
    m_tagsEdit = new QLineEdit(this);
    m_tagsEdit->setToolTip(tr("The frame's tags, separated by commas; a change adds or removes "
                              "them on all selected frames"));
    m_tagsEdit->setPlaceholderText(tr("e.g. Multiball, Settings menu"));
    m_tagsEdit->setEnabled(false);
    // Completes the tag being typed (after the last comma) from the project's tags.
    m_tagsCompleter = new QCompleter(this);
    m_tagsCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_tagsCompleter->setModel(new QStringListModel(m_tagsCompleter));
    m_tagsCompleter->setWidget(m_tagsEdit);
    connect(m_tagsEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        const QString typed = text.section(QLatin1Char(','), -1).trimmed();
        if (typed.isEmpty()) {
            m_tagsCompleter->popup()->hide();
            return;
        }
        m_tagsCompleter->setCompletionPrefix(typed);
        m_tagsCompleter->complete();
    });
    connect(m_tagsCompleter, static_cast<void (QCompleter::*)(const QString&)>(
                                 &QCompleter::activated),
            this, [this](const QString& tag) {
                // The tag replaces what is typed after the last comma.
                const QString text = m_tagsEdit->text();
                const int comma = text.lastIndexOf(QLatin1Char(','));
                const QString before =
                    comma < 0 ? QString() : text.left(comma + 1) + QLatin1Char(' ');
                m_tagsEdit->setText(before + tag);
            });
    connect(m_tagsEdit, &QLineEdit::editingFinished, this, [this]() {
        // Only a change counts (focus leaving the field changes nothing).
        if (m_tagsCompleter->popup()->isVisible() || m_tagsEdit->text() == m_shownTags) {
            return;
        }
        m_shownTags = m_tagsEdit->text();
        if (onTagsEdited) {
            onTagsEdited(m_tagsEdit->text());
        }
    });
    m_backgroundLayerButton = new QToolButton(this);
    m_backgroundLayerButton->setText(tr("Show Background"));
    m_backgroundLayerButton->setToolTip(tr("Show assigned background layer"));
    m_backgroundLayerButton->setCheckable(true);
    m_backgroundLayerButton->setChecked(true);
    m_backgroundLayerButton->setEnabled(false);
    m_spritesLayerButton = new QToolButton(this);
    m_spritesLayerButton->setText(tr("Show Sprites"));
    m_spritesLayerButton->setToolTip(
        tr("Show the sprites libserum finds on the frame, drawn over it"));
    m_spritesLayerButton->setCheckable(true);
    m_spritesLayerButton->setChecked(true);
    m_spritesLayerButton->setEnabled(false);
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

    // DOF events: what the frame passes to the DirectOutput Framework when it shows.
    m_dofEdit = new QLineEdit(this);
    m_dofEdit->setToolTip(
        tr("DOF events the frame emits when it shows, separated by spaces: a table element type "
           "(S solenoid, L lamp, W switch, D LED, ...), its number, '=' and the value; after ';' "
           "a value the next frame gives the element unless it sets it itself. They replace "
           "those of all selected frames"));
    m_dofEdit->setPlaceholderText(tr("e.g. S5=255 L12=1;0"));
    m_dofEdit->setEnabled(false);
    m_dofErrorLabel = new QLabel(this);
    m_dofErrorLabel->setWordWrap(true);
    m_dofErrorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_dofErrorLabel->setVisible(false);
    connect(m_dofEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_dofEdit->text() == m_shownDofEvents) {
            m_dofErrorLabel->setVisible(false);
            return;
        }
        std::string wrong;
        const std::optional<document::DofEvents> events =
            document::parseDofEvents(m_dofEdit->text().toStdString(), &wrong);
        if (!events) {
            m_dofErrorLabel->setText(
                tr("\"%1\" is not a DOF event such as S5=255 or L12=1;0")
                    .arg(QString::fromStdString(wrong)));
            m_dofErrorLabel->setVisible(true);
            return;
        }
        m_dofErrorLabel->setVisible(false);
        m_shownDofEvents = QString::fromStdString(document::formatDofEvents(*events));
        m_dofEdit->setText(m_shownDofEvents);
        m_dofEdit->setModified(false);
        if (onDofEventsEdited) {
            onDofEventsEdited(*events);
        }
    });

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    auto* form = makeInspectorForm();
    form->addRow(tr("Background ID"), m_backgroundIdSpinBox);
    form->addRow(tr("PUP trigger ID"), m_pupTriggerSpinBox);
    form->addRow(tr("Scene"), m_sceneComboBox);
    // Across the form: a check box has its text beside it, not a label.
    form->addRow(m_monochromeCheckBox);
    form->addRow(m_paletteMonochromeCheckBox);
    form->addRow(m_otherTriggerLabel);
    form->addRow(tr("Duration"), m_durationSpinBox);
    form->addRow(tr("Tags"), m_tagsEdit);
    form->addRow(tr("DOF events"), m_dofEdit);
    form->addRow(m_dofErrorLabel);
    pageLayout->addLayout(form);
    auto* layerLayout = new QHBoxLayout();
    layerLayout->addWidget(m_backgroundLayerButton);
    layerLayout->addWidget(m_spritesLayerButton);
    pageLayout->addLayout(layerLayout);
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
    connect(m_spritesLayerButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onSpritesLayerToggled) {
            onSpritesLayerToggled(checked);
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

void FramePanel::fillScenes(const document::ProjectDocument* document, uint32_t sceneTriggerId)
{
    m_sceneComboBox->clear();
    m_sceneComboBox->addItem(tr("None"), -1);
    bool listed = false;
    if (document && document->colorizationSettings()) {
        for (const document::SceneDefinition& scene : document->colorizationSettings()->scenes) {
            m_sceneComboBox->addItem(sceneText(scene), static_cast<int>(scene.sceneId));
            listed = listed || scene.sceneId == sceneTriggerId;
        }
    }
    if (sceneTriggerId != document::kNoTriggerId && !listed) {
        // Shown as it is; it cannot be chosen for another frame.
        m_sceneComboBox->addItem(tr("%1 (no such scene)").arg(sceneTriggerId),
                                 static_cast<int>(sceneTriggerId));
    }
    const int current = m_sceneComboBox->findData(
        sceneTriggerId == document::kNoTriggerId ? -1 : static_cast<int>(sceneTriggerId));
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
        m_durationSpinBox->setToolTip(tr("Per-frame duration"));
        m_tagsEdit->clear();
        m_shownTags.clear();
        m_tagsEdit->setEnabled(false);
        m_dofEdit->clear();
        m_shownDofEvents.clear();
        m_dofEdit->setEnabled(false);
        m_dofErrorLabel->setVisible(false);
        m_backgroundLayerButton->setEnabled(false);
        m_spritesLayerButton->setEnabled(false);
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
    // The three triggers apart: a PUP trigger, a scene and a monochrome trigger.
    const uint32_t pupTrigger = document->pupTriggerId(frameIndex);
    const uint32_t monochromeTrigger = document->monochromeTriggerId(frameIndex);
    m_pupTriggerSpinBox->setValue(pupTrigger <= document::kLastPupTriggerId
                                      ? static_cast<int>(pupTrigger)
                                      : -1);
    m_pupTriggerSpinBox->setEnabled(true);
    fillScenes(document, document->sceneTriggerId(frameIndex));
    m_sceneComboBox->setEnabled(m_sceneComboBox->count() > 1);
    m_monochromeCheckBox->setChecked(monochromeTrigger == document::kMonochromeTriggerId);
    m_monochromeCheckBox->setEnabled(true);
    m_paletteMonochromeCheckBox->setChecked(monochromeTrigger ==
                                            document::kPaletteMonochromeTriggerId);
    m_paletteMonochromeCheckBox->setEnabled(true);
    // A monochrome trigger of another value (a PUP trigger out of range cannot be set).
    const bool otherMonochrome = monochromeTrigger != document::kNoTriggerId &&
                                 monochromeTrigger != document::kMonochromeTriggerId &&
                                 monochromeTrigger != document::kPaletteMonochromeTriggerId;
    m_otherTriggerLabel->setText(
        tr("Trigger ID %1 is kept until another trigger is chosen").arg(monochromeTrigger));
    m_otherTriggerLabel->setVisible(otherMonochrome);
    m_shownTags = QString::fromStdString(edit::frameTagsText(document->frameTags(frameIndex)));
    // Not while it is being typed in: a refresh would throw the typing away.
    if (!m_tagsEdit->hasFocus() || !m_tagsEdit->isModified()) {
        m_tagsEdit->setText(m_shownTags);
        m_tagsEdit->setModified(false);
    }
    m_tagsEdit->setEnabled(true);
    m_shownDofEvents =
        QString::fromStdString(document::formatDofEvents(document->frameDofEvents(frameIndex)));
    if (!m_dofEdit->hasFocus() || !m_dofEdit->isModified()) {
        m_dofEdit->setText(m_shownDofEvents);
        m_dofEdit->setModified(false);
        m_dofErrorLabel->setVisible(false);
    }
    m_dofEdit->setEnabled(true);
    {
        QStringList known;
        for (const std::string& tag : document->allFrameTags()) {
            known << QString::fromStdString(tag);
        }
        static_cast<QStringListModel*>(m_tagsCompleter->model())->setStringList(known);
    }
    const uint32_t durationMs = document->frameDurationMs(frameIndex);
    m_durationSpinBox->setEnabled(true);
    m_durationSpinBox->setValue(durationMs <= static_cast<uint32_t>(kMaxFrameDurationMs)
                                    ? static_cast<int>(durationMs)
                                    : 0);
    m_durationSpinBox->setToolTip(
        durationMs <= static_cast<uint32_t>(kMaxFrameDurationMs)
            ? tr("Per-frame duration")
            : tr("Unsupported duration %1 ms is preserved until edited")
                  .arg(durationMs));
    m_backgroundLayerButton->setEnabled(backgroundData.has_value() && maxBackgroundId >= 0);
    // A sprite in one of the frame's slots.
    const std::vector<document::FrameSpriteReference>& spriteSlots =
        document->frameSpriteReferences(frameIndex);
    m_spritesLayerButton->setEnabled(
        std::any_of(spriteSlots.begin(), spriteSlots.end(),
                    [document](const document::FrameSpriteReference& reference) {
                        return reference.spriteId < document->spriteCount();
                    }));
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

void FramePanel::setImportImageEnabled(bool enabled)
{
    m_importImageButton->setEnabled(enabled);
}

bool FramePanel::isSpritesLayerChecked() const
{
    return m_spritesLayerButton->isChecked();
}

bool FramePanel::isBackgroundLayerChecked() const
{
    return m_backgroundLayerButton->isChecked();
}

}  // namespace colorizing::app
