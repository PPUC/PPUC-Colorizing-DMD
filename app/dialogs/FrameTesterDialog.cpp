#include "dialogs/FrameTesterDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <fstream>

#include "import/DumpFile.h"
#include "FrameImageUtils.h"
#include "dialogs/ProjectTaskDialog.h"
#include "legacy/LegacyProjectIo.h"
#include "platform/Utf8Path.h"

namespace colorizing::app {

namespace {

using runtime::RuntimePreview;

// Shown at a width of 512 pixels: 128 columns at 4, 256 at 2.
int scaleFor(int width)
{
    return std::max(1, 512 / std::max(1, width));
}

QPixmap planePixmap(const runtime::RuntimePlane* plane)
{
    if (!plane || !plane->image.isValid()) {
        return QPixmap();
    }
    return QPixmap::fromImage(
        renderRgb565FrameImage(plane->image, scaleFor(plane->image.size.width)));
}

}  // namespace

FrameTesterDialog::FrameTesterDialog(const document::ProjectDocument& document,
                                     std::vector<std::size_t> selectedFrames,
                                     QWidget* parent)
    : QDialog(parent),
      m_document(document),
      m_selectedFrames(std::move(selectedFrames))
{
    setWindowTitle(tr("Frame Tester"));
    m_sourceLabel = new QLabel(this);
    auto* loadButton = new QPushButton(tr("Load Dump..."), this);
    loadButton->setToolTip(tr("Test every frame of a frame dump (TXT)"));
    m_selectedButton = new QPushButton(tr("Selected Frames"), this);
    m_selectedButton->setToolTip(tr("Test the frames selected in the frame list"));

    m_rows32Label = new QLabel(this);
    m_rows32Label->setToolTip(tr("libserum's 32-row plane"));
    m_rows64Label = new QLabel(this);
    m_rows64Label->setToolTip(tr("libserum's 64-row plane"));
    m_originalLabel = new QLabel(this);
    m_originalLabel->setToolTip(tr("The ROM frame fed to libserum"));
    for (QLabel* label : {m_rows32Label, m_rows64Label, m_originalLabel}) {
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumSize(512, 128);
        label->setStyleSheet(QStringLiteral("background: black"));
    }
    m_infoLabel = new QLabel(this);
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setToolTip(tr("Position among the tested frames"));
    m_positionSpinBox = new QSpinBox(this);
    m_positionSpinBox->setToolTip(tr("Position among the tested frames"));
    m_backButton = new QPushButton(tr("◀ Play"), this);
    m_backButton->setToolTip(tr("Play backwards"));
    m_playButton = new QPushButton(tr("Play ▶"), this);
    m_speedComboBox = new QComboBox(this);
    m_speedComboBox->setToolTip(tr("Play speed"));
    for (const char* text : {"1/4x", "1/2x", "1x", "2x", "4x", "8x", "16x", "32x"}) {
        m_speedComboBox->addItem(QString::fromLatin1(text));
    }
    m_speedComboBox->setCurrentIndex(2);
    m_loopCheckBox = new QCheckBox(tr("Loop"), this);
    m_nextUnidentifiedButton = new QPushButton(tr("Next Unidentified"), this);
    m_nextUnidentifiedButton->setToolTip(tr("Go to the next frame libserum does not identify"));
    m_findSpinBox = new QSpinBox(this);
    m_findSpinBox->setToolTip(tr("Project frame to find among the tested frames"));
    m_findSpinBox->setRange(
        0, static_cast<int>(std::max<std::size_t>(1, document.frameCount()) - 1));
    m_underMaskCheckBox = new QCheckBox(tr("Under its mask"), this);
    m_underMaskCheckBox->setToolTip(
        tr("Leave out the pixels the frame's comparison mask covers"));
    m_findButton = new QPushButton(tr("Find Frame"), this);
    m_addButton = new QPushButton(tr("Add To Project"), this);
    m_addButton->setToolTip(tr("Add the dump frame behind the project's frames"));
    m_showButton = new QPushButton(tr("Show In Editor"), this);
    m_showButton->setToolTip(
        tr("Close the tester and show the frame libserum identified, or the tested project frame"));
    auto* closeButton = new QPushButton(tr("Close"), this);

    auto* sourceRow = new QHBoxLayout();
    sourceRow->addWidget(m_sourceLabel, 1);
    sourceRow->addWidget(m_selectedButton);
    sourceRow->addWidget(loadButton);
    auto* pictures = new QGridLayout();
    pictures->addWidget(m_rows32Label, 0, 0);
    pictures->addWidget(m_rows64Label, 0, 1);
    pictures->addWidget(m_originalLabel, 1, 0, 1, 2);
    auto* positionRow = new QHBoxLayout();
    positionRow->addWidget(m_slider, 1);
    positionRow->addWidget(m_positionSpinBox);
    auto* playRow = new QHBoxLayout();
    playRow->addWidget(m_backButton);
    playRow->addWidget(m_playButton);
    playRow->addWidget(m_speedComboBox);
    playRow->addWidget(m_loopCheckBox);
    playRow->addSpacing(12);
    playRow->addWidget(m_nextUnidentifiedButton);
    playRow->addStretch(1);
    auto* findRow = new QHBoxLayout();
    findRow->addWidget(new QLabel(tr("Project frame"), this));
    findRow->addWidget(m_findSpinBox);
    findRow->addWidget(m_underMaskCheckBox);
    findRow->addWidget(m_findButton);
    findRow->addStretch(1);
    findRow->addWidget(m_addButton);
    findRow->addWidget(m_showButton);
    findRow->addWidget(closeButton);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(sourceRow);
    layout->addLayout(pictures);
    layout->addWidget(m_infoLabel);
    layout->addLayout(positionRow);
    layout->addLayout(playRow);
    layout->addLayout(findRow);

    m_playTimer.setSingleShot(true);
    m_rotationTimer.setSingleShot(true);
    connect(&m_playTimer, &QTimer::timeout, this, [this]() { playStep(); });
    connect(&m_rotationTimer, &QTimer::timeout, this, [this]() { rotateStep(); });
    connect(loadButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Load Frame Dump"), QString(),
            tr("Frame dumps (*.txt *.gz *.zip);;All files (*)"));
        if (!path.isEmpty()) {
            loadDumpFile(path);
        }
    });
    connect(m_selectedButton, &QPushButton::clicked, this, [this]() { useSelectedFrames(); });
    connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
        if (value >= 0 && static_cast<std::size_t>(value) != m_position) {
            showPosition(static_cast<std::size_t>(value));
        }
    });
    connect(m_positionSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int value) {
                if (value >= 0 && static_cast<std::size_t>(value) != m_position) {
                    showPosition(static_cast<std::size_t>(value));
                }
            });
    connect(m_playButton, &QPushButton::clicked, this, [this]() {
        setPlaying(m_playDirection == 1 ? 0 : 1);
    });
    connect(m_backButton, &QPushButton::clicked, this, [this]() {
        setPlaying(m_playDirection == -1 ? 0 : -1);
    });
    connect(m_nextUnidentifiedButton, &QPushButton::clicked, this, [this]() {
        nextUnidentified();
    });
    connect(m_findButton, &QPushButton::clicked, this, [this]() {
        findProjectFrame(static_cast<std::size_t>(m_findSpinBox->value()),
                         m_underMaskCheckBox->isChecked());
    });
    connect(m_addButton, &QPushButton::clicked, this, [this]() {
        if (!m_dumpMode || m_frames.empty() || !onAddFrameRequested) {
            return;
        }
        setPlaying(0);
        const runtime::TesterFrame frame = m_frames[m_position];
        if (onAddFrameRequested(frame)) {
            // The project changed: libserum gets it again; the dump stays where it is.
            if (loadProject()) {
                feed(m_position);
            }
            m_findSpinBox->setRange(0, static_cast<int>(m_document.frameCount()) - 1);
        }
    });
    connect(m_showButton, &QPushButton::clicked, this, [this]() {
        if (m_result.frameIndex) {
            m_frameToShow = m_result.frameIndex;
        } else if (!m_frames.empty()) {
            m_frameToShow = m_frames[m_position].projectFrame;
        }
        accept();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    if (loadProject()) {
        useSelectedFrames();
    }
    updateControls();
}

FrameTesterDialog::~FrameTesterDialog()
{
    m_playTimer.stop();
    m_rotationTimer.stop();
    RuntimePreview::instance().close();
}

std::optional<std::size_t> FrameTesterDialog::frameToShow() const
{
    return m_frameToShow;
}

bool FrameTesterDialog::loadProject()
{
    RuntimePreview& preview = RuntimePreview::instance();
    preview.close();
    m_result = {};
    std::string error;
    try {
        if (preview.openProject(m_document, runtime::PlaneRequest::Both, &error)) {
            m_error.clear();
            return true;
        }
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    m_error = tr("libserum could not load the project: %1").arg(QString::fromStdString(error));
    m_infoLabel->setText(m_error);
    return false;
}

bool FrameTesterDialog::loadDumpFile(const QString& path)
{
    setPlaying(0);
    runtime::TesterDump dump;
    legacy::LegacyIoProgress progress;
    // A .gz or .zip dump is decompressed while it is read (importing::openDumpFile()).
    importing::DumpFile file = importing::openDumpFile(path.toStdString());
    progress.total = file.sizeBytes;
    progress.setStage(legacy::LegacyIoStage::Reading);
    const std::optional<std::string> failure =
        runProjectTask(this, tr("Loading Frame Dump"), progress, [&]() {
            if (!file.ok()) {
                dump.error = file.error;
                return;
            }
            dump = runtime::readTesterDump(*file.stream, m_document, [&progress](uint64_t bytes) {
                progress.done = bytes;
                return !progress.cancel.load();
            });
        });
    if (failure) {
        dump.error = *failure;
    }
    if (dump.cancelled) {
        return false;
    }
    if (!dump.error.empty()) {
        m_infoLabel->setText(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(),
                                                          QString::fromStdString(dump.error)));
        return false;
    }
    m_dumpMode = true;
    QString source = tr("Dump %1: %2 frame(s)")
                         .arg(QFileInfo(path).fileName())
                         .arg(static_cast<qulonglong>(dump.frames.size()));
    if (dump.truncatedFrameLine) {
        source += tr(" (the unfinished frame of line %1 is left out)")
                      .arg(static_cast<qulonglong>(*dump.truncatedFrameLine));
    }
    setFrames(std::move(dump.frames), source);
    return true;
}

void FrameTesterDialog::useSelectedFrames()
{
    setPlaying(0);
    m_dumpMode = false;
    setFrames(runtime::testerFramesOfProject(m_document, m_selectedFrames),
              tr("Selected frames: %1").arg(static_cast<qulonglong>(m_selectedFrames.size())));
}

void FrameTesterDialog::setFrames(std::vector<runtime::TesterFrame> frames, const QString& source)
{
    m_frames = std::move(frames);
    m_source = source;
    m_sourceLabel->setText(source);
    m_position = 0;
    {
        const QSignalBlocker sliderBlocker(m_slider);
        const QSignalBlocker spinBlocker(m_positionSpinBox);
        const int last = static_cast<int>(std::max<std::size_t>(1, m_frames.size()) - 1);
        m_slider->setRange(0, last);
        m_positionSpinBox->setRange(0, last);
        m_slider->setValue(0);
        m_positionSpinBox->setValue(0);
    }
    if (!m_frames.empty()) {
        feed(0);
    } else {
        m_result = {};
        showResult();
    }
    updateControls();
}

void FrameTesterDialog::showPosition(std::size_t position)
{
    if (position >= m_frames.size()) {
        return;
    }
    feed(position);
}

void FrameTesterDialog::feed(std::size_t position)
{
    m_position = position;
    {
        const QSignalBlocker sliderBlocker(m_slider);
        const QSignalBlocker spinBlocker(m_positionSpinBox);
        m_slider->setValue(static_cast<int>(position));
        m_positionSpinBox->setValue(static_cast<int>(position));
    }
    RuntimePreview& preview = RuntimePreview::instance();
    if (!m_error.isEmpty() || !preview.isOpen()) {
        m_rotationTimer.stop();
        m_result = {};
        m_result.error = "libserum has not loaded the project";
        showResult();
        return;
    }
    // The rotation step drives color rotations and scenes; it is scheduled as a machine's host
    // (libdmdutil) does. A step that is due runs before the frame: libserum would otherwise
    // restart its time when the frame is the one on display.
    if (m_rotationTimer.isActive() && m_rotationTimer.remainingTime() <= 0) {
        m_rotationTimer.stop();
        rotateStep();
    }
    m_result = preview.colorize(m_frames[position].original);
    const bool newPicture =
        m_result.ok() && !m_result.unchanged && (m_result.rows32 || m_result.rows64);
    if (newPicture) {
        m_rotationTimer.stop();
        if (m_result.nextRotationMs > 0) {
            m_rotationTimer.start(static_cast<int>(m_result.nextRotationMs));
        }
    } else if (!m_result.ok()) {
        m_rotationTimer.stop();
    }
    // "Same frame" and an unknown frame keep the running schedule: a scene or a rotation goes
    // on with its own time, as on a machine.
    showResult();
}

void FrameTesterDialog::rotateStep()
{
    const runtime::RuntimeFrame rotated = RuntimePreview::instance().rotate();
    if (!rotated.ok()) {
        return;
    }
    if (rotated.rows32) {
        m_result.rows32 = rotated.rows32;
    }
    if (rotated.rows64) {
        m_result.rows64 = rotated.rows64;
    }
    showResult();
    if (rotated.nextRotationMs > 0) {
        m_rotationTimer.start(static_cast<int>(rotated.nextRotationMs));
    }
}

void FrameTesterDialog::showResult()
{
    if (!m_result.unchanged) {
        m_rows32Label->setPixmap(planePixmap(m_result.plane(runtime::PlaneRequest::Rows32)));
        m_rows64Label->setPixmap(planePixmap(m_result.plane(runtime::PlaneRequest::Rows64)));
    }
    if (!m_frames.empty()) {
        const document::IndexedFrame& original = m_frames[m_position].original;
        m_originalLabel->setPixmap(QPixmap::fromImage(renderIndexedFrameImage(
            original, scaleFor(original.size.width), {}, m_document.colorCount())));
    } else {
        m_originalLabel->clear();
    }
    m_infoLabel->setText(infoText());
}

QString FrameTesterDialog::infoText() const
{
    if (!m_error.isEmpty()) {
        return m_error;
    }
    if (m_frames.empty()) {
        return tr("No frames to test");
    }
    const runtime::TesterFrame& frame = m_frames[m_position];
    QStringList parts;
    parts << QStringLiteral("%1 / %2")
                 .arg(static_cast<qulonglong>(m_position))
                 .arg(static_cast<qulonglong>(m_frames.size()));
    if (frame.projectFrame) {
        parts << tr("project frame %1").arg(static_cast<qulonglong>(*frame.projectFrame));
    } else {
        parts << tr("dump time 0x%1").arg(frame.timecode, 8, 16, QLatin1Char('0'));
    }
    parts << QStringLiteral("%1 ms").arg(frame.durationMs);
    if (!m_result.ok()) {
        parts << QString::fromStdString(m_result.error);
    } else if (m_result.unchanged) {
        parts << tr("libserum keeps the picture on display");
    } else if (!m_result.identified) {
        parts << tr("not identified");
    } else {
        QString identified =
            m_result.frameIndex
                ? tr("identified as frame %1").arg(static_cast<qulonglong>(*m_result.frameIndex))
                : tr("identified");
        if (m_result.skipped) {
            identified += tr(" (inactive: the display keeps its picture)");
        }
        parts << identified;
    }
    if (m_result.triggerId != document::kNoTriggerId) {
        parts << tr("trigger %1").arg(m_result.triggerId);
    }
    return parts.join(QStringLiteral("  ·  "));
}

QString FrameTesterDialog::errorText() const
{
    return m_error;
}

const document::Rgb565Frame* FrameTesterDialog::currentPicture(bool preferRows64) const
{
    // A frame libserum keeps the picture for: the planes of the call before still show.
    const runtime::PlaneRequest rows64 = runtime::PlaneRequest::Rows64;
    const runtime::PlaneRequest rows32 = runtime::PlaneRequest::Rows32;
    const runtime::RuntimePlane* first = m_result.plane(preferRows64 ? rows64 : rows32);
    const runtime::RuntimePlane* other = m_result.plane(preferRows64 ? rows32 : rows64);
    const runtime::RuntimePlane* plane = first ? first : other;
    return plane && plane->image.isValid() ? &plane->image : nullptr;
}

bool FrameTesterDialog::nextUnidentified()
{
    setPlaying(0);
    const std::size_t start = m_position;
    for (std::size_t position = start + 1; position < m_frames.size(); ++position) {
        feed(position);
        if (m_result.ok() && !m_result.identified && !m_result.unchanged) {
            return true;
        }
    }
    // None up to the end: back to where it was (fed again, as the Win32 editor does).
    if (start < m_frames.size()) {
        feed(start);
    }
    m_infoLabel->setText(infoText() + tr("  ·  every later frame is identified"));
    return false;
}

bool FrameTesterDialog::findProjectFrame(std::size_t frameIndex, bool underMask)
{
    if (frameIndex >= m_document.frameCount() || m_frames.empty()) {
        return false;
    }
    setPlaying(0);
    std::optional<std::size_t> found;
    if (m_dumpMode) {
        found = runtime::findProjectFrame(m_document, frameIndex, m_frames, m_position, underMask);
    } else {
        const auto it = std::find(m_selectedFrames.begin(), m_selectedFrames.end(), frameIndex);
        if (it != m_selectedFrames.end()) {
            found = static_cast<std::size_t>(it - m_selectedFrames.begin());
        }
    }
    if (!found) {
        m_infoLabel->setText(
            m_dumpMode ? tr("Frame %1 is not in the dump").arg(static_cast<qulonglong>(frameIndex))
                       : tr("Frame %1 is not among the selected frames")
                             .arg(static_cast<qulonglong>(frameIndex)));
        return false;
    }
    feed(*found);
    return true;
}

double FrameTesterDialog::speed() const
{
    static constexpr double kSpeeds[] = {0.25, 0.5, 1, 2, 4, 8, 16, 32};
    const int index = std::clamp(m_speedComboBox->currentIndex(), 0, 7);
    return kSpeeds[index];
}

void FrameTesterDialog::setPlaying(int direction)
{
    m_playDirection = m_frames.size() > 1 ? direction : 0;
    m_playTimer.stop();
    if (m_playDirection != 0) {
        m_playTimer.start(std::max(1, static_cast<int>(m_frames[m_position].durationMs / speed())));
    }
    updateControls();
}

void FrameTesterDialog::playStep()
{
    if (m_playDirection == 0 || m_frames.empty()) {
        return;
    }
    const std::size_t count = m_frames.size();
    std::size_t next = m_position;
    if (m_playDirection > 0) {
        if (m_position + 1 < count) {
            next = m_position + 1;
        } else if (m_loopCheckBox->isChecked()) {
            next = 0;
        } else {
            setPlaying(0);
            return;
        }
    } else {
        if (m_position > 0) {
            next = m_position - 1;
        } else if (m_loopCheckBox->isChecked()) {
            next = count - 1;
        } else {
            setPlaying(0);
            return;
        }
    }
    feed(next);
    m_playTimer.start(std::max(1, static_cast<int>(m_frames[next].durationMs / speed())));
}

void FrameTesterDialog::updateControls()
{
    const bool hasFrames = !m_frames.empty() && m_error.isEmpty();
    m_playButton->setText(m_playDirection == 1 ? tr("Pause") : tr("Play ▶"));
    m_backButton->setText(m_playDirection == -1 ? tr("Pause") : tr("◀ Play"));
    for (QWidget* widget : std::initializer_list<QWidget*>{m_slider, m_positionSpinBox,
                                                         m_playButton, m_backButton,
                                                         m_nextUnidentifiedButton, m_findButton,
                                                         m_showButton}) {
        widget->setEnabled(hasFrames);
    }
    m_underMaskCheckBox->setEnabled(hasFrames && m_dumpMode);
    m_addButton->setEnabled(hasFrames && m_dumpMode && onAddFrameRequested != nullptr);
    m_selectedButton->setEnabled(!m_selectedFrames.empty() && m_error.isEmpty());
}

bool FrameTesterDialog::isDumpMode() const
{
    return m_dumpMode;
}

std::size_t FrameTesterDialog::frameCount() const
{
    return m_frames.size();
}

std::size_t FrameTesterDialog::position() const
{
    return m_position;
}

const runtime::RuntimeFrame& FrameTesterDialog::lastResult() const
{
    return m_result;
}

}  // namespace colorizing::app
