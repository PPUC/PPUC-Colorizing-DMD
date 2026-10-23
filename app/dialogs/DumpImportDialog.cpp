#include "dialogs/DumpImportDialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace colorizing::app {

namespace {

constexpr int kPollIntervalMs = 40;
constexpr int kProgressSteps = 1000;

QString number(std::size_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}

}  // namespace

QString dumpFormatText(const importing::DumpImportResult& result)
{
    QString text = QCoreApplication::translate("DumpImportDialog", "%1x%2 frames")
                       .arg(result.dumpFrameSize.width)
                       .arg(result.dumpFrameSize.height);
    if (result.storedFrameSize != result.dumpFrameSize) {
        text += QCoreApplication::translate("DumpImportDialog",
                                            " (stored as %1x%2, 8 blank rows above and below)")
                    .arg(result.storedFrameSize.width)
                    .arg(result.storedFrameSize.height);
    }
    text += result.colorCount > 4
                ? QCoreApplication::translate("DumpImportDialog", ", 16 colors (shades above 3)")
                : QCoreApplication::translate("DumpImportDialog", ", 4 colors (no shade above 3)");
    text += QCoreApplication::translate("DumpImportDialog", ", %1 frames")
                .arg(number(result.counts.framesRead));
    if (result.truncatedFrameLine) {
        text += QCoreApplication::translate(
                    "DumpImportDialog",
                    ". The dump ends inside the frame at line %1; that frame is left out")
                    .arg(number(*result.truncatedFrameLine));
    }
    return text;
}

QString dumpCountsText(const importing::DumpImportResult& result, bool addsFrames)
{
    const importing::DumpImportCounts& counts = result.counts;
    QString text = QCoreApplication::translate(
                       "DumpImportDialog",
                       "%1 read, %2 duplicates, %3 too short, %4 with too many colors")
                       .arg(number(counts.framesRead), number(counts.duplicates),
                            number(counts.tooShort), number(counts.tooManyColors));
    if (counts.transitional > 0) {
        text += QCoreApplication::translate("DumpImportDialog", ", %1 transitional")
                    .arg(number(counts.transitional));
    }
    if (addsFrames) {
        text += QCoreApplication::translate("DumpImportDialog",
                                            ", %1 already in the project (%2 identical, %3 "
                                            "identified through a mask or shape mode), %4 to add")
                    .arg(number(counts.alreadyInProject()), number(counts.identicalInProject),
                         number(counts.identifiedByMaskOrShape), number(counts.framesKept));
    } else {
        text += QCoreApplication::translate("DumpImportDialog", ", %1 for the project")
                    .arg(number(counts.framesKept));
    }
    if (counts.hashCollisions > 0) {
        text += QCoreApplication::translate("DumpImportDialog",
                                            ". %1 of the dropped frames have other pixels and "
                                            "the same hash as the frame they were dropped for; "
                                            "the runtime cannot tell them apart either")
                    .arg(number(counts.hashCollisions));
    }
    return text;
}

DumpImportDialog::DumpImportDialog(Mode mode,
                                   const document::ProjectDocument* project,
                                   QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_project(mode == Mode::AddFrames ? project : nullptr)
{
    const bool addsFrames = m_mode == Mode::AddFrames;
    setWindowTitle(addsFrames ? tr("Add Frames From Dump") : tr("New Project From Dump"));
    setModal(true);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setToolTip(tr("Frame dump file"));
    auto* browseButton = new QPushButton(tr("Choose..."), this);
    browseButton->setToolTip(tr("Choose another frame dump file"));
    browseButton->setAutoDefault(false);
    m_romNameEdit = new QLineEdit(this);
    m_romNameEdit->setMaxLength(static_cast<int>(importing::kMaxRomNameLength));
    m_romNameEdit->setToolTip(
        tr("ROM name of the new project, as the runtime looks the colorization up"));
    m_romNameProblemLabel = new QLabel(this);
    m_romNameProblemLabel->setWordWrap(true);

    m_transitionalCheckBox =
        new QCheckBox(tr("Leave out transitional frames (WPC) shown for at most"), this);
    m_transitionalCheckBox->setToolTip(
        tr("A WPC display shows a mix of the old and the new picture for a moment while it "
           "changes: every pixel that changes between the frames before and after has the "
           "same in-between shade. Such ghost frames are left out"));
    m_transitionalSpinBox = new QSpinBox(this);
    m_transitionalSpinBox->setRange(static_cast<int>(importing::kMinTransitionalLimitMs),
                                    static_cast<int>(importing::kMaxTransitionalLimitMs));
    m_transitionalSpinBox->setSuffix(QStringLiteral(" ms"));
    m_transitionalSpinBox->setToolTip(
        tr("Longest duration of a transitional frame that is left out"));
    m_shortFramesCheckBox = new QCheckBox(tr("Leave out frames shown for less than"), this);
    m_shortFramesCheckBox->setToolTip(
        tr("A frame is shown until the next one of the dump arrives. The last frame and a frame "
           "whose time cannot be told (30 ms is assumed) are never left out"));
    m_shortFramesSpinBox = new QSpinBox(this);
    m_shortFramesSpinBox->setRange(static_cast<int>(importing::kMinShortFrameLimitMs),
                                   static_cast<int>(importing::kMaxShortFrameLimitMs));
    m_shortFramesSpinBox->setSuffix(QStringLiteral(" ms"));
    m_shortFramesSpinBox->setToolTip(tr("Shortest duration of a frame that is kept"));
    m_colorsCheckBox = new QCheckBox(tr("Leave out frames with more than"), this);
    m_colorsCheckBox->setToolTip(
        tr("Frames with many shades are mostly fades between two pictures"));
    m_colorsSpinBox = new QSpinBox(this);
    m_colorsSpinBox->setRange(static_cast<int>(importing::kMinColorLimit),
                              static_cast<int>(importing::kMaxColorLimit));
    m_colorsSpinBox->setSuffix(tr(" shades"));
    m_colorsSpinBox->setToolTip(tr("Largest number of shades of a frame that is kept"));
    m_masksCheckBox = new QCheckBox(
        tr("Leave out frames the project identifies with its masks and shape modes"), this);
    m_masksCheckBox->setToolTip(
        tr("On: a frame is left out if the runtime would identify it as a frame of the project. "
           "Off: only if a frame of the project has exactly the same original"));
    m_masksCheckBox->setVisible(addsFrames);
    m_masksCheckBox->setChecked(addsFrames);
    m_romNameEdit->setVisible(!addsFrames);
    m_romNameProblemLabel->setVisible(!addsFrames);

    m_analyzeButton = new QPushButton(tr("Read Dump Again"), this);
    m_analyzeButton->setToolTip(tr("Read the dump with the options as they are now"));
    m_analyzeButton->setAutoDefault(false);
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_stopButton->setToolTip(tr("Stop reading the dump"));
    m_stopButton->setAutoDefault(false);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, kProgressSteps);
    m_progressBar->setTextVisible(false);
    m_formatLabel = new QLabel(this);
    m_formatLabel->setWordWrap(true);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setMinimumWidth(460);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)
        ->setText(addsFrames ? tr("Add Frames") : tr("Create Project"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    auto* pathLayout = new QHBoxLayout();
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(browseButton);
    form->addRow(tr("Dump"), pathLayout);
    if (!addsFrames) {
        form->addRow(tr("ROM name"), m_romNameEdit);
        form->addRow(QString(), m_romNameProblemLabel);
    }
    layout->addLayout(form);
    auto* transitionalLayout = new QHBoxLayout();
    transitionalLayout->addWidget(m_transitionalCheckBox);
    transitionalLayout->addWidget(m_transitionalSpinBox);
    transitionalLayout->addStretch(1);
    layout->addLayout(transitionalLayout);
    auto* shortLayout = new QHBoxLayout();
    shortLayout->addWidget(m_shortFramesCheckBox);
    shortLayout->addWidget(m_shortFramesSpinBox);
    shortLayout->addStretch(1);
    layout->addLayout(shortLayout);
    auto* colorsLayout = new QHBoxLayout();
    colorsLayout->addWidget(m_colorsCheckBox);
    colorsLayout->addWidget(m_colorsSpinBox);
    colorsLayout->addStretch(1);
    layout->addLayout(colorsLayout);
    layout->addWidget(m_masksCheckBox);
    auto* progressLayout = new QHBoxLayout();
    progressLayout->addWidget(m_analyzeButton);
    progressLayout->addWidget(m_progressBar, 1);
    progressLayout->addWidget(m_stopButton);
    layout->addLayout(progressLayout);
    layout->addWidget(m_formatLabel);
    layout->addWidget(m_summaryLabel);
    layout->addStretch(1);
    layout->addWidget(m_buttons);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        const uint64_t total = m_totalBytes.load();
        if (total > 0) {
            m_progressBar->setValue(static_cast<int>(
                std::min<uint64_t>(kProgressSteps, m_bytesRead.load() * kProgressSteps / total)));
        }
        if (m_workerDone.load()) {
            finishAnalysis();
        }
    });
    connect(browseButton, &QPushButton::clicked, this, [this]() { chooseFile(); });
    connect(m_analyzeButton, &QPushButton::clicked, this, [this]() { startAnalysis(); });
    connect(m_stopButton, &QPushButton::clicked, this, [this]() { stopAnalysis(); });
    connect(m_romNameEdit, &QLineEdit::textChanged, this, [this]() { updateControls(); });
    for (QCheckBox* checkBox :
         {m_transitionalCheckBox, m_shortFramesCheckBox, m_colorsCheckBox, m_masksCheckBox}) {
        connect(checkBox, &QCheckBox::toggled, this, [this]() { markStale(); });
    }
    for (QSpinBox* spinBox : {m_transitionalSpinBox, m_shortFramesSpinBox, m_colorsSpinBox}) {
        connect(spinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
                [this]() { markStale(); });
    }
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (acceptProblem().isEmpty()) {
            accept();
        }
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    importing::DumpImportOptions defaults;
    defaults.useMasksAndShapeModes = addsFrames;
    setOptions(defaults);
    m_stale = false;
    updateControls();
}

DumpImportDialog::~DumpImportDialog()
{
    stopAnalysis();
}

void DumpImportDialog::setDumpPath(const QString& path)
{
    m_pathEdit->setText(path);
    if (m_mode == Mode::NewProject) {
        m_romNameEdit->setText(
            QString::fromStdString(importing::romNameFromDumpPath(path.toStdString())));
    }
    startAnalysis();
}

QString DumpImportDialog::dumpPath() const
{
    return m_pathEdit->text();
}

void DumpImportDialog::setRomName(const QString& name)
{
    m_romNameEdit->setText(name);
}

QString DumpImportDialog::romName() const
{
    return m_romNameEdit->text();
}

void DumpImportDialog::setOptions(const importing::DumpImportOptions& requested)
{
    const importing::DumpImportOptions shown = importing::clampedOptions(requested);
    m_transitionalCheckBox->setChecked(shown.dropTransitionalFrames);
    m_transitionalSpinBox->setValue(static_cast<int>(shown.transitionalLimitMs));
    m_shortFramesCheckBox->setChecked(shown.dropShortFrames);
    m_shortFramesSpinBox->setValue(static_cast<int>(shown.shortFrameLimitMs));
    m_colorsCheckBox->setChecked(shown.dropColorfulFrames);
    m_colorsSpinBox->setValue(static_cast<int>(shown.colorLimit));
    m_masksCheckBox->setChecked(m_mode == Mode::AddFrames && shown.useMasksAndShapeModes);
    markStale();
}

importing::DumpImportOptions DumpImportDialog::options() const
{
    importing::DumpImportOptions current;
    current.dropTransitionalFrames = m_transitionalCheckBox->isChecked();
    current.transitionalLimitMs = static_cast<uint32_t>(m_transitionalSpinBox->value());
    current.dropShortFrames = m_shortFramesCheckBox->isChecked();
    current.shortFrameLimitMs = static_cast<uint32_t>(m_shortFramesSpinBox->value());
    current.dropColorfulFrames = m_colorsCheckBox->isChecked();
    current.colorLimit = static_cast<uint32_t>(m_colorsSpinBox->value());
    current.useMasksAndShapeModes = m_mode == Mode::AddFrames && m_masksCheckBox->isChecked();
    return current;
}

QString frameDumpFileFilter()
{
    return QCoreApplication::translate("DumpImportDialog",
                                       "Frame dumps (*.txt *.gz *.zip);;All files (*)");
}

void DumpImportDialog::chooseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Choose Frame Dump"),
        QFileInfo(dumpPath()).absolutePath(),
        frameDumpFileFilter());
    if (!path.isEmpty()) {
        setDumpPath(path);
    }
}

void DumpImportDialog::startAnalysis()
{
    stopAnalysis();
    m_result.reset();
    m_failureText.clear();
    m_stale = false;
    if (dumpPath().isEmpty()) {
        updateControls();
        return;
    }
    m_bytesRead = 0;
    m_totalBytes = 0;
    m_stopRequested = false;
    m_workerDone = false;
    m_workerResult = {};
    m_progressBar->setValue(0);
    const std::string path = dumpPath().toStdString();
    const importing::DumpImportOptions importOptions = options();
    m_worker = std::thread([this, path, importOptions]() {
        uint64_t totalBytes = 0;
        // The total is known before the first byte is read.
        m_workerResult = importing::importFrameDumpFile(
            path, importOptions, m_project,
            [this, &totalBytes](uint64_t bytesRead) {
                m_totalBytes = totalBytes;
                m_bytesRead = bytesRead;
                return !m_stopRequested.load();
            },
            &totalBytes);
        m_workerDone = true;
    });
    m_pollTimer->start();
    updateControls();
}

void DumpImportDialog::stopAnalysis()
{
    if (!m_worker.joinable()) {
        return;
    }
    m_stopRequested = true;
    finishAnalysis();
}

void DumpImportDialog::waitForAnalysis()
{
    if (m_worker.joinable()) {
        finishAnalysis();
    }
}

bool DumpImportDialog::isAnalysisRunning() const
{
    return m_worker.joinable();
}

void DumpImportDialog::finishAnalysis()
{
    if (!m_worker.joinable()) {
        return;
    }
    m_worker.join();
    m_pollTimer->stop();
    if (m_workerResult.ok) {
        m_result = std::move(m_workerResult);
        m_progressBar->setValue(kProgressSteps);
    } else {
        m_failureText = m_workerResult.cancelled
                            ? tr("Reading the dump was stopped.")
                            : QString::fromStdString(m_workerResult.error.text());
        m_progressBar->setValue(0);
    }
    m_workerResult = {};
    updateControls();
}

void DumpImportDialog::markStale()
{
    if (m_result || !m_failureText.isEmpty()) {
        m_stale = true;
    }
    updateControls();
}

const importing::DumpImportResult* DumpImportDialog::result() const
{
    return m_result && !m_stale && !isAnalysisRunning() ? &*m_result : nullptr;
}

importing::DumpImportResult DumpImportDialog::takeResult()
{
    importing::DumpImportResult taken;
    if (result()) {
        taken = std::move(*m_result);
    }
    m_result.reset();
    updateControls();
    return taken;
}

QString DumpImportDialog::acceptProblem() const
{
    if (isAnalysisRunning()) {
        return tr("The dump is still being read.");
    }
    if (m_stale) {
        return tr("The options changed: read the dump again.");
    }
    if (!m_result) {
        return m_failureText.isEmpty() ? tr("No dump was read.") : m_failureText;
    }
    if (m_result->counts.framesKept == 0) {
        return m_mode == Mode::AddFrames
                   ? tr("The project has every frame of this dump already.")
                   : tr("No frame of the dump passes the filter.");
    }
    if (m_mode == Mode::NewProject) {
        return QString::fromStdString(importing::romNameProblem(romName().toStdString()));
    }
    return QString();
}

void DumpImportDialog::updateControls()
{
    const bool running = isAnalysisRunning();
    const bool addsFrames = m_mode == Mode::AddFrames;
    m_analyzeButton->setEnabled(!running && !dumpPath().isEmpty());
    m_stopButton->setEnabled(running);
    for (QWidget* option : {static_cast<QWidget*>(m_transitionalCheckBox),
                            static_cast<QWidget*>(m_shortFramesCheckBox),
                            static_cast<QWidget*>(m_colorsCheckBox),
                            static_cast<QWidget*>(m_masksCheckBox)}) {
        option->setEnabled(!running);
    }
    m_transitionalSpinBox->setEnabled(!running && m_transitionalCheckBox->isChecked());
    m_shortFramesSpinBox->setEnabled(!running && m_shortFramesCheckBox->isChecked());
    m_colorsSpinBox->setEnabled(!running && m_colorsCheckBox->isChecked());
    if (!addsFrames) {
        m_romNameProblemLabel->setText(
            QString::fromStdString(importing::romNameProblem(romName().toStdString())));
    }

    if (running) {
        m_formatLabel->setText(tr("Reading the dump..."));
        m_summaryLabel->setText(QString());
    } else if (m_result) {
        m_formatLabel->setText(dumpFormatText(*m_result));
        m_summaryLabel->setText(m_stale ? tr("The options changed: read the dump again to see "
                                             "what they do.")
                                        : dumpCountsText(*m_result, addsFrames));
    } else {
        m_formatLabel->setText(m_stale ? tr("The options changed: read the dump again.")
                                       : m_failureText);
        m_summaryLabel->setText(QString());
    }
    const QString problem = acceptProblem();
    QPushButton* okButton = m_buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(problem.isEmpty());
    okButton->setToolTip(problem);
}

void DumpImportDialog::done(int result)
{
    stopAnalysis();
    QDialog::done(result);
}

}  // namespace colorizing::app
