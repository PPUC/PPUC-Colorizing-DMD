#include "dialogs/FrameSettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace colorizing::app {

namespace {

using edit::FrameSettingsPart;

const QString kSettingsKey = QStringLiteral("copyFrameSettings/parts");

// What is chosen at first: the settings an author copies to similar frames. Identification,
// trigger, lights, DOF events and duration change how or when a frame plays.
bool chosenAtFirst(FrameSettingsPart part)
{
    switch (part) {
        case FrameSettingsPart::Identification:
        case FrameSettingsPart::Trigger:
        case FrameSettingsPart::Lights:
        case FrameSettingsPart::DofEvents:
        case FrameSettingsPart::Duration:
            return false;
        default:
            return true;
    }
}

QString partKey(FrameSettingsPart part)
{
    return QString::number(static_cast<int>(part));
}

}  // namespace

QString frameSettingsPartName(FrameSettingsPart part)
{
    switch (part) {
        case FrameSettingsPart::DynamicZones:
            return QCoreApplication::translate("FrameSettingsDialog", "Dynamic zones");
        case FrameSettingsPart::DynamicColors:
            return QCoreApplication::translate("FrameSettingsDialog", "Dynamic set colors");
        case FrameSettingsPart::DynamicShadows:
            return QCoreApplication::translate("FrameSettingsDialog", "Dynamic shadows");
        case FrameSettingsPart::Background:
            return QCoreApplication::translate("FrameSettingsDialog", "Background");
        case FrameSettingsPart::BackgroundMask:
            return QCoreApplication::translate("FrameSettingsDialog", "Background mask");
        case FrameSettingsPart::Sprites:
            return QCoreApplication::translate("FrameSettingsDialog", "Sprites and their bounds");
        case FrameSettingsPart::ColorRotations:
            return QCoreApplication::translate("FrameSettingsDialog", "Color rotations");
        case FrameSettingsPart::Identification:
            return QCoreApplication::translate("FrameSettingsDialog",
                                               "Comparison mask and shape mode");
        case FrameSettingsPart::Trigger:
            return QCoreApplication::translate("FrameSettingsDialog", "Trigger");
        case FrameSettingsPart::Lights:
            return QCoreApplication::translate("FrameSettingsDialog", "Speaker lights");
        case FrameSettingsPart::DofEvents:
            return QCoreApplication::translate("FrameSettingsDialog", "DOF events");
        case FrameSettingsPart::Duration:
            return QCoreApplication::translate("FrameSettingsDialog", "Duration");
    }
    return {};
}

FrameSettingsDialog::FrameSettingsDialog(const document::ProjectDocument& document,
                                         std::size_t source,
                                         std::vector<std::size_t> targets,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Copy Frame Settings"));
    targets.erase(std::remove(targets.begin(), targets.end(), source), targets.end());
    m_sceneFrames = static_cast<std::size_t>(
        std::count_if(targets.begin(), targets.end(), [&document](std::size_t frame) {
            return document.sceneFrame(frame).has_value();
        }));

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("The chosen settings of frame %1 replace those of %n selected frame(s). The "
           "pictures stay; Copy and Paste is for those.",
           "", static_cast<int>(targets.size()))
            .arg(static_cast<qulonglong>(source)),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const QSettings settings;
    const QStringList stored = settings.value(kSettingsKey).toStringList();
    const bool hasStored = settings.contains(kSettingsKey);
    const auto group = [&](const QString& title, std::initializer_list<FrameSettingsPart> parts) {
        auto* box = new QGroupBox(title, this);
        auto* boxLayout = new QVBoxLayout(box);
        for (FrameSettingsPart part : parts) {
            const std::size_t changed =
                edit::framesChangedBy(document, source, targets, part);
            auto* checkBox = new QCheckBox(
                tr("%1 (changes %n frame(s))", "", static_cast<int>(changed))
                    .arg(frameSettingsPartName(part)),
                box);
            checkBox->setChecked(hasStored ? stored.contains(partKey(part))
                                           : chosenAtFirst(part));
            connect(checkBox, &QCheckBox::toggled, this, [this]() { updateNote(); });
            boxLayout->addWidget(checkBox);
            m_checkBoxes[part] = checkBox;
        }
        layout->addWidget(box);
    };
    group(tr("Dynamic content"),
          {FrameSettingsPart::DynamicZones, FrameSettingsPart::DynamicColors,
           FrameSettingsPart::DynamicShadows});
    group(tr("Background"), {FrameSettingsPart::Background, FrameSettingsPart::BackgroundMask});
    group(tr("Sprites and colors"),
          {FrameSettingsPart::Sprites, FrameSettingsPart::ColorRotations});
    group(tr("How and when the frame plays"),
          {FrameSettingsPart::Identification, FrameSettingsPart::Trigger,
           FrameSettingsPart::Lights, FrameSettingsPart::DofEvents,
           FrameSettingsPart::Duration});

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Copy"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    updateNote();
}

std::vector<FrameSettingsPart> FrameSettingsDialog::parts() const
{
    std::vector<FrameSettingsPart> chosen;
    for (FrameSettingsPart part : edit::kFrameSettingsParts) {
        if (m_checkBoxes.at(part)->isChecked()) {
            chosen.push_back(part);
        }
    }
    return chosen;
}

QCheckBox* FrameSettingsDialog::checkBox(FrameSettingsPart part) const
{
    return m_checkBoxes.at(part);
}

void FrameSettingsDialog::accept()
{
    QStringList chosen;
    for (FrameSettingsPart part : parts()) {
        chosen << partKey(part);
    }
    QSettings().setValue(kSettingsKey, chosen);
    QDialog::accept();
}

void FrameSettingsDialog::updateNote()
{
    QStringList notes;
    if (m_checkBoxes.at(FrameSettingsPart::Identification)->isChecked()) {
        notes << tr("The comparison mask and the shape mode decide how libserum finds the "
                    "frames: the targets may be identified differently afterwards.");
    }
    if (m_sceneFrames > 0) {
        notes << tr("%n target(s) are frames of scenes: they take only the background, its "
                    "mask, color rotations, lights, DOF events and the duration.",
                    "", static_cast<int>(m_sceneFrames));
    }
    m_note->setText(notes.join(QStringLiteral("\n")));
    m_note->setVisible(!notes.isEmpty());
}

}  // namespace colorizing::app
