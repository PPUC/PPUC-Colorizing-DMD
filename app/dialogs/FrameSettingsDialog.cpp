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
const QString kDuplicateSettingsKey = QStringLiteral("duplicateFrame/parts");

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

// What a copy of a frame takes at first: everything it uses but what would make the copy fire
// with the frame (its trigger and DOF events).
bool duplicatedAtFirst(FrameSettingsPart part)
{
    return part != FrameSettingsPart::Trigger && part != FrameSettingsPart::DofEvents;
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
                                         QWidget* parent,
                                         std::optional<std::vector<FrameSettingsPart>> chosen)
    : QDialog(parent),
      m_keepsChoices(!chosen)
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
    addParts(document, source, targets,
             std::vector<FrameSettingsPart>(std::begin(edit::kFrameSettingsParts),
                                            std::end(edit::kFrameSettingsParts)),
             chosen);
    addNoteAndButtons(tr("Copy"));
}

FrameSettingsDialog::FrameSettingsDialog(const document::ProjectDocument& document,
                                         Duplicate duplicate,
                                         QWidget* parent)
    : QDialog(parent),
      m_duplicate(true)
{
    setWindowTitle(tr("Duplicate Frame"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        (duplicate.selection
             ? tr("Frame %1 is copied right behind it, with its original, duration and tags; "
                  "only the selected pixels take its picture and dynamic zones. Which of its "
                  "settings does the copy take?")
             : tr("Frame %1 is copied right behind it, with its original, picture, duration "
                  "and tags. Which of its settings does the copy take?"))
            .arg(static_cast<qulonglong>(duplicate.source)),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    addParts(document, duplicate.source, {},
             edit::usedFrameSettingsParts(document, duplicate.source), std::nullopt);
    addNoteAndButtons(tr("Duplicate"));
}

void FrameSettingsDialog::addParts(const document::ProjectDocument& document,
                                   std::size_t source,
                                   const std::vector<std::size_t>& targets,
                                   const std::vector<FrameSettingsPart>& offered,
                                   const std::optional<std::vector<FrameSettingsPart>>& chosen)
{
    auto* layout = static_cast<QVBoxLayout*>(this->layout());
    const QSettings settings;
    const QString key = m_duplicate ? kDuplicateSettingsKey : kSettingsKey;
    const QStringList stored = settings.value(key).toStringList();
    const bool hasStored = settings.contains(key);
    const auto group = [&](const QString& title, std::initializer_list<FrameSettingsPart> parts) {
        QGroupBox* box = nullptr;
        for (FrameSettingsPart part : parts) {
            if (std::find(offered.begin(), offered.end(), part) == offered.end()) {
                continue;
            }
            if (!box) {
                box = new QGroupBox(title, this);
                box->setLayout(new QVBoxLayout);
                layout->addWidget(box);
            }
            // A copy is a new frame: each part changes it, and only the parts the frame uses
            // are offered.
            const QString text =
                m_duplicate
                    ? frameSettingsPartName(part)
                    : tr("%1 (changes %n frame(s))", "",
                         static_cast<int>(edit::framesChangedBy(document, source, targets,
                                                                part)))
                          .arg(frameSettingsPartName(part));
            auto* checkBox = new QCheckBox(text, box);
            checkBox->setChecked(
                chosen ? std::find(chosen->begin(), chosen->end(), part) != chosen->end()
                : hasStored ? stored.contains(partKey(part))
                : m_duplicate ? duplicatedAtFirst(part)
                              : chosenAtFirst(part));
            connect(checkBox, &QCheckBox::toggled, this, [this]() { updateNote(); });
            box->layout()->addWidget(checkBox);
            m_checkBoxes[part] = checkBox;
        }
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
}

void FrameSettingsDialog::addNoteAndButtons(const QString& acceptLabel)
{
    auto* layout = static_cast<QVBoxLayout*>(this->layout());
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(acceptLabel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    updateNote();
}

std::vector<FrameSettingsPart> FrameSettingsDialog::parts() const
{
    std::vector<FrameSettingsPart> chosen;
    for (FrameSettingsPart part : edit::kFrameSettingsParts) {
        if (const QCheckBox* box = checkBox(part); box && box->isChecked()) {
            chosen.push_back(part);
        }
    }
    return chosen;
}

QCheckBox* FrameSettingsDialog::checkBox(FrameSettingsPart part) const
{
    const auto found = m_checkBoxes.find(part);
    return found == m_checkBoxes.end() ? nullptr : found->second;
}

void FrameSettingsDialog::accept()
{
    if (m_keepsChoices) {
        QStringList chosen;
        for (FrameSettingsPart part : parts()) {
            chosen << partKey(part);
        }
        QSettings().setValue(m_duplicate ? kDuplicateSettingsKey : kSettingsKey, chosen);
    }
    QDialog::accept();
}

void FrameSettingsDialog::updateNote()
{
    QStringList notes;
    const QCheckBox* identification = checkBox(FrameSettingsPart::Identification);
    if (m_duplicate) {
        // Same original: libserum finds the frame first, so the copy needs another mask.
        notes << tr("The copy has the frame's original: libserum finds the frame before it, "
                    "until the copy gets another comparison mask or shape mode.");
        if (const QCheckBox* trigger = checkBox(FrameSettingsPart::Trigger);
            trigger && trigger->isChecked()) {
            notes << tr("Once the copy is identified, it fires the trigger as well.");
        }
    } else if (identification && identification->isChecked()) {
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
