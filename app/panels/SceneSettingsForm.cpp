#include "panels/SceneSettingsForm.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "panels/InspectorForm.h"

namespace colorizing::app {

namespace {

// m_repeatComboBox: once, loop, a number of times (2 and more).
constexpr int kRepeatOnce = 0;
constexpr int kRepeatLoop = 1;
constexpr int kRepeatTimes = 2;

}  // namespace

SceneSettingsForm::SceneSettingsForm(QWidget* parent)
    : QWidget(parent)
{
    m_idSpinBox = new QSpinBox(this);
    m_idSpinBox->setToolTip(tr("The PUP trigger that starts the scene, from %1 to %2")
                                .arg(document::kFirstSceneId)
                                .arg(document::kLastSceneId));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setToolTip(tr("A name for the scene, shown with its ID (the project only)"));
    m_nameEdit->setPlaceholderText(tr("e.g. Jackpot"));
    m_framesOfComboBox = new QComboBox(this);
    m_framesOfComboBox->setToolTip(
        tr("A scene that plays the frames of another plays them with its own settings and frame "
           "duration, e.g. once in front and as a looping background. It has no frames of its "
           "own; for other frames add a new scene"));
    m_framesSpinBox = new QSpinBox(this);
    m_framesSpinBox->setRange(1, 65535);
    m_framesSpinBox->setToolTip(tr("Frames of each group"));
    m_groupsSpinBox = new QSpinBox(this);
    m_groupsSpinBox->setRange(1, 255);
    m_groupsSpinBox->setToolTip(
        tr("Sets of frames the scene can show; each time it plays, it shows one of them"));
    m_randomCheckBox = new QCheckBox(tr("Pick the group at random"), this);
    m_randomCheckBox->setToolTip(tr("Otherwise the groups play in turn"));
    m_durationSpinBox = new QSpinBox(this);
    m_durationSpinBox->setRange(1, 65535);
    m_durationSpinBox->setSuffix(tr(" ms"));
    m_durationSpinBox->setToolTip(tr("How long each frame shows"));

    m_repeatComboBox = new QComboBox(this);
    m_repeatComboBox->addItem(tr("Once"), kRepeatOnce);
    m_repeatComboBox->addItem(tr("In a loop"), kRepeatLoop);
    m_repeatComboBox->addItem(tr("Several times"), kRepeatTimes);
    m_timesSpinBox = new QSpinBox(this);
    m_timesSpinBox->setRange(2, 255);
    m_timesSpinBox->setSuffix(tr(" times"));
    m_backAndForthCheckBox = new QCheckBox(tr("Back and forth"), this);
    m_backAndForthCheckBox->setToolTip(
        tr("Each time, the frames play to the last and back again: 1 to 100, 99 to 2, then 1 to "
           "100 again; the last time ends on the first frame"));
    auto* repeatRow = new QHBoxLayout;
    repeatRow->addWidget(m_repeatComboBox, 1);
    repeatRow->addWidget(m_timesSpinBox);

    m_interruptableCheckBox = new QCheckBox(tr("Can be interrupted"), this);
    m_interruptableCheckBox->setToolTip(
        tr("A frame of the game identified while the scene plays ends it"));
    m_immediateCheckBox = new QCheckBox(tr("Start immediately"), this);
    m_immediateCheckBox->setToolTip(
        tr("Show the first frame when the trigger fires, without waiting for the next frame "
           "of the game"));
    m_finishComboBox = new QComboBox(this);
    m_finishComboBox->addItem(tr("Keep its last frame"), 0);
    m_finishComboBox->addItem(tr("Black"), document::kSceneBlackWhenFinished);
    m_finishComboBox->addItem(tr("The frame before the scene"),
                              document::kSceneShowPreviousFrameWhenFinished);
    m_finishComboBox->setToolTip(tr("What shows when the scene has played"));
    m_autoStartSpinBox = new QSpinBox(this);
    m_autoStartSpinBox->setRange(0, 255);
    m_autoStartSpinBox->setSuffix(tr(" s"));
    m_autoStartSpinBox->setSpecialValueText(tr("Off"));
    m_autoStartLabel = new QLabel(this);

    m_backgroundCheckBox = new QCheckBox(tr("Play as a background"), this);
    m_backgroundCheckBox->setToolTip(
        tr("The scene shows behind the frames of the game, where their background mask lets it "
           "through"));
    m_onlyDynamicCheckBox = new QCheckBox(tr("Keep only dynamic content in front"), this);
    m_onlyDynamicCheckBox->setToolTip(
        tr("Of the frames of the game, only dynamic zones, sprites and shadows show over the "
           "scene"));
    m_replaceBlackCheckBox = new QCheckBox(tr("Black dynamic pixels show the scene"), this);
    m_replaceBlackCheckBox->setToolTip(
        tr("Dynamic-zone pixels that are black let the background scene through and cast no "
           "shadow"));
    m_resumeCheckBox = new QCheckBox(tr("Resume when triggered again"), this);
    m_resumeCheckBox->setToolTip(
        tr("A trigger soon after the scene stopped continues it where it was instead of "
           "starting it over"));

    // A field below its label when the side bar is narrow.
    QFormLayout* frames = makeInspectorForm();
    frames->addRow(tr("Scene ID (PUP trigger)"), m_idSpinBox);
    frames->addRow(tr("Name"), m_nameEdit);
    frames->addRow(tr("Plays the frames of"), m_framesOfComboBox);
    frames->addRow(tr("Frames"), m_framesSpinBox);
    frames->addRow(tr("Groups"), m_groupsSpinBox);
    frames->addRow(QString(), m_randomCheckBox);
    frames->addRow(tr("Frame duration"), m_durationSpinBox);
    frames->addRow(tr("Plays"), repeatRow);
    frames->addRow(QString(), m_backAndForthCheckBox);
    QFormLayout* playback = makeInspectorForm();
    playback->addRow(QString(), m_interruptableCheckBox);
    playback->addRow(QString(), m_immediateCheckBox);
    playback->addRow(tr("When it has played"), m_finishComboBox);
    playback->addRow(m_autoStartLabel, m_autoStartSpinBox);
    auto* playbackBox = new QGroupBox(tr("Playback"), this);
    playbackBox->setLayout(playback);
    auto* backgroundLayout = new QVBoxLayout;
    backgroundLayout->addWidget(m_backgroundCheckBox);
    backgroundLayout->addWidget(m_onlyDynamicCheckBox);
    backgroundLayout->addWidget(m_replaceBlackCheckBox);
    backgroundLayout->addWidget(m_resumeCheckBox);
    auto* backgroundBox = new QGroupBox(tr("Options"), this);
    backgroundBox->setLayout(backgroundLayout);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_problemLabel = new QLabel(this);
    m_problemLabel->setWordWrap(true);
    m_problemLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(frames);
    layout->addWidget(playbackBox);
    layout->addWidget(backgroundBox);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_problemLabel);

    const auto changed = [this]() {
        updateState();
        if (!m_filling && onChanged) {
            onChanged();
        }
    };
    for (QSpinBox* spinBox : {m_idSpinBox, m_framesSpinBox, m_groupsSpinBox, m_durationSpinBox,
                              m_timesSpinBox, m_autoStartSpinBox}) {
        connect(spinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
                changed);
    }
    connect(m_nameEdit, &QLineEdit::textEdited, this, changed);
    for (QComboBox* comboBox : {m_repeatComboBox, m_finishComboBox, m_framesOfComboBox}) {
        connect(comboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this, changed);
    }
    for (QCheckBox* checkBox : {m_randomCheckBox, m_backAndForthCheckBox,
                                m_interruptableCheckBox, m_immediateCheckBox,
                                m_backgroundCheckBox, m_onlyDynamicCheckBox, m_resumeCheckBox,
                                m_replaceBlackCheckBox}) {
        connect(checkBox, &QCheckBox::toggled, this, changed);
    }
    setScene(document::SceneDefinition{}, nullptr);
}

void SceneSettingsForm::setScene(
    const document::SceneDefinition& scene,
    std::function<std::string(const document::SceneDefinition&)> problem,
    const std::vector<document::SceneDefinition>& projectScenes,
    bool newScene)
{
    m_filling = true;
    m_scene = scene;
    m_problem = std::move(problem);
    m_projectScenes = projectScenes;
    m_nameEdit->setText(QString::fromStdString(scene.name));
    m_framesOfComboBox->clear();
    m_framesOfComboBox->addItem(tr("Its own frames"), -1);
    for (const document::SceneDefinition& candidate : projectScenes) {
        if (!candidate.isVariant() && candidate.sceneId != scene.sceneId) {
            m_framesOfComboBox->addItem(tr("Scene %1").arg(sceneText(candidate)),
                                        static_cast<int>(candidate.sceneId));
        }
    }
    if (scene.baseSceneId &&
        m_framesOfComboBox->findData(static_cast<int>(*scene.baseSceneId)) < 0) {
        m_framesOfComboBox->addItem(tr("Scene %1").arg(*scene.baseSceneId),
                                    static_cast<int>(*scene.baseSceneId));
    }
    m_framesOfComboBox->setCurrentIndex(std::max(
        0, m_framesOfComboBox->findData(scene.baseSceneId ? static_cast<int>(*scene.baseSceneId)
                                                          : -1)));
    m_framesOfComboBox->setEnabled(newScene || scene.isVariant());
    // A scene of an imported pup.csv may have an ID outside the range for new IDs; it keeps it.
    const bool inRange =
        scene.sceneId >= document::kFirstSceneId && scene.sceneId <= document::kLastSceneId;
    m_idSpinBox->setRange(inRange ? document::kFirstSceneId : 0,
                          inRange ? document::kLastSceneId : 65535);
    m_idSpinBox->setValue(scene.sceneId);
    m_framesSpinBox->setValue(std::max<int>(1, scene.frameCount));
    m_groupsSpinBox->setValue(std::max<int>(1, scene.frameGroups));
    m_randomCheckBox->setChecked(scene.random);
    m_durationSpinBox->setValue(std::max<int>(1, scene.durationMs));
    m_repeatComboBox->setCurrentIndex(scene.repeat >= 2 ? kRepeatTimes : scene.repeat);
    m_timesSpinBox->setValue(scene.repeat >= 2 ? scene.repeat : 2);
    m_backAndForthCheckBox->setChecked(scene.options & document::kSceneBackAndForth);
    m_interruptableCheckBox->setChecked(scene.interruptable);
    m_immediateCheckBox->setChecked(scene.immediateStart);
    m_finishComboBox->setCurrentIndex(
        std::max(0, m_finishComboBox->findData(scene.options & document::kSceneFinishModeMask)));
    m_autoStartSpinBox->setValue(scene.autoStartSeconds);
    m_backgroundCheckBox->setChecked(scene.options & document::kSceneAsBackground);
    m_onlyDynamicCheckBox->setChecked(scene.options & document::kSceneOnlyDynamicContent);
    m_replaceBlackCheckBox->setChecked(scene.options & document::kSceneReplaceDynamicBlack);
    m_resumeCheckBox->setChecked(scene.options & document::kSceneResumeIfRetriggered);
    m_filling = false;
    updateState();
}

void SceneSettingsForm::updateState()
{
    m_timesSpinBox->setEnabled(m_repeatComboBox->currentData().toInt() == kRepeatTimes);
    // A variant has its base's frames and groups.
    const int base = m_framesOfComboBox->currentData().toInt();
    if (base >= 0) {
        for (const document::SceneDefinition& candidate : m_projectScenes) {
            if (candidate.sceneId == base) {
                const QSignalBlocker framesBlocker(m_framesSpinBox);
                const QSignalBlocker groupsBlocker(m_groupsSpinBox);
                m_framesSpinBox->setValue(std::max<int>(1, candidate.frameCount));
                m_groupsSpinBox->setValue(std::max<int>(1, candidate.frameGroups));
            }
        }
    }
    m_framesSpinBox->setEnabled(base < 0);
    m_groupsSpinBox->setEnabled(base < 0);
    m_randomCheckBox->setEnabled(m_groupsSpinBox->value() > 1);
    m_backAndForthCheckBox->setEnabled(m_framesSpinBox->value() > 1);
    m_replaceBlackCheckBox->setEnabled(m_backgroundCheckBox->isChecked());
    // libserum reads the column as an end hold for a scene that cannot be interrupted and
    // keeps its last frame, and as the inactivity before an automatic start otherwise.
    const bool endHold = !m_interruptableCheckBox->isChecked() &&
                         m_finishComboBox->currentData().toInt() == 0;
    m_autoStartLabel->setText(endHold ? tr("Hold the last frame for") : tr("Start by itself after"));
    m_autoStartSpinBox->setToolTip(
        endHold ? tr("How long the last frame stays when the scene has played")
                : tr("Start the scene when no new frame came for this long"));
    const document::SceneDefinition current = scene();
    m_summaryLabel->setText(
        current.baseSceneId
            ? tr("No frames of its own: it plays the %n frame(s) of scene %1", "",
                 static_cast<int>(current.totalFrames()))
                  .arg(*current.baseSceneId)
            : tr("Frames in the project: %1").arg(static_cast<qulonglong>(current.totalFrames())));
    const std::string reason = problem();
    m_problemLabel->setText(QString::fromStdString(reason));
    m_problemLabel->setVisible(!reason.empty());
}

std::string SceneSettingsForm::problem() const
{
    return m_problem ? m_problem(scene()) : std::string();
}

document::SceneDefinition SceneSettingsForm::scene() const
{
    document::SceneDefinition scene = m_scene;
    scene.sceneId = static_cast<uint16_t>(m_idSpinBox->value());
    scene.frameCount = static_cast<uint16_t>(m_framesSpinBox->value());
    scene.frameGroups = static_cast<uint8_t>(m_groupsSpinBox->value());
    scene.random = m_randomCheckBox->isChecked() && scene.frameGroups > 1;
    scene.durationMs = static_cast<uint16_t>(m_durationSpinBox->value());
    scene.name = m_nameEdit->text().trimmed().toStdString();
    const int base = m_framesOfComboBox->currentData().toInt();
    scene.baseSceneId =
        base >= 0 ? std::optional<uint16_t>(static_cast<uint16_t>(base)) : std::nullopt;
    switch (m_repeatComboBox->currentData().toInt()) {
        case kRepeatLoop:
            scene.repeat = 1;
            break;
        case kRepeatTimes:
            scene.repeat = static_cast<uint8_t>(m_timesSpinBox->value());
            break;
        default:
            scene.repeat = 0;
            break;
    }
    scene.interruptable = m_interruptableCheckBox->isChecked();
    scene.immediateStart = m_immediateCheckBox->isChecked();
    scene.autoStartSeconds = static_cast<uint8_t>(m_autoStartSpinBox->value());
    uint8_t options = static_cast<uint8_t>(m_scene.options & ~document::kSceneKnownOptions);
    options |= static_cast<uint8_t>(m_finishComboBox->currentData().toInt());
    if (m_backgroundCheckBox->isChecked()) {
        options |= document::kSceneAsBackground;
        if (m_replaceBlackCheckBox->isChecked()) {
            options |= document::kSceneReplaceDynamicBlack;
        }
    }
    if (m_onlyDynamicCheckBox->isChecked()) {
        options |= document::kSceneOnlyDynamicContent;
    }
    if (m_resumeCheckBox->isChecked()) {
        options |= document::kSceneResumeIfRetriggered;
    }
    if (m_backAndForthCheckBox->isChecked()) {
        options |= document::kSceneBackAndForth;
    }
    scene.options = options;
    return scene;
}

QString sceneText(const document::SceneDefinition& scene)
{
    return scene.name.empty()
               ? QString::number(scene.sceneId)
               : QStringLiteral("%1 %2").arg(scene.sceneId).arg(QString::fromStdString(scene.name));
}

}  // namespace colorizing::app
