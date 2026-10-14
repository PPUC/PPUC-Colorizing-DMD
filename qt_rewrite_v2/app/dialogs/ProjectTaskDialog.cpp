#include "dialogs/ProjectTaskDialog.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace colorizing::app {

namespace {

constexpr int kPollIntervalMs = 50;
constexpr int kProgressSteps = 1000;

int g_dialogDelayMs = 300;
std::function<void(ProjectTaskDialog&)> g_shownHook;
std::function<void()> g_workerStartHook;

QString megabytes(uint64_t bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 0);
}

}  // namespace

ProjectTaskDialog::ProjectTaskDialog(const QString& title,
                                     legacy::LegacyIoProgress& progress,
                                     std::function<bool()> finished,
                                     QWidget* parent)
    : QDialog(parent)
    , m_progress(progress)
    , m_finished(std::move(finished))
{
    setWindowTitle(title);
    setWindowModality(Qt::ApplicationModal);
    setMinimumWidth(380);

    m_label = new QLabel(this);
    m_label->setToolTip(QStringLiteral("What the editor is doing with the project files"));
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, kProgressSteps);
    m_bar->setTextVisible(false);
    m_bar->setToolTip(QStringLiteral("How much of the project files is done"));
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelButton->setAutoDefault(false);
    m_cancelButton->setToolTip(
        QStringLiteral("Stop: an open keeps the project that is open now, a save leaves the "
                       "files on disk as they were"));
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { requestCancel(); });

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_cancelButton);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_label);
    layout->addWidget(m_bar);
    layout->addLayout(buttons);

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (m_finished()) {
            m_timer->stop();
            accept();
            return;
        }
        updateProgress();
    });
    m_timer->start();
    updateProgress();
}

void ProjectTaskDialog::requestCancel()
{
    m_progress.cancel.store(true);
    updateProgress();
}

void ProjectTaskDialog::reject()
{
    // Closing does not abandon the work: it is asked to stop, and the dialog closes when it has.
    requestCancel();
}

void ProjectTaskDialog::updateProgress()
{
    const legacy::LegacyIoStage stage = m_progress.currentStage();
    const uint64_t done = m_progress.done.load();
    const uint64_t total = m_progress.total.load();
    const bool cancelling = m_progress.cancel.load();
    const bool measured = (stage == legacy::LegacyIoStage::Reading ||
                           stage == legacy::LegacyIoStage::Writing) &&
                          total > 0;
    QString text;
    switch (stage) {
    case legacy::LegacyIoStage::Starting:
        text = QStringLiteral("Starting…");
        break;
    case legacy::LegacyIoStage::Reading:
        text = QStringLiteral("Reading the project: %1 of %2 MB")
                   .arg(megabytes(done), megabytes(total));
        break;
    case legacy::LegacyIoStage::Decompressing:
        text = QStringLiteral("Unpacking the .cRZ archive…");
        break;
    case legacy::LegacyIoStage::Writing:
        text = QStringLiteral("Writing the .cROM and .cRP: %1 of %2 MB")
                   .arg(megabytes(done), megabytes(total));
        break;
    case legacy::LegacyIoStage::Compressing:
        text = QStringLiteral("Packing the .cRZ archive (this step cannot be interrupted)…");
        break;
    case legacy::LegacyIoStage::Replacing:
        text = QStringLiteral("Replacing the project files…");
        break;
    case legacy::LegacyIoStage::Generating:
        text = QStringLiteral("libserum generates the .cROMc (this step cannot be interrupted)…");
        break;
    }
    if (cancelling) {
        text = stage == legacy::LegacyIoStage::Decompressing ||
                       stage == legacy::LegacyIoStage::Compressing ||
                       stage == legacy::LegacyIoStage::Generating
                   ? QStringLiteral("Cancelling when this step ends…")
                   : QStringLiteral("Cancelling…");
    }
    m_label->setText(text);
    if (measured) {
        m_bar->setRange(0, kProgressSteps);
        m_bar->setValue(static_cast<int>(std::min<uint64_t>(done, total) * kProgressSteps /
                                         total));
    } else {
        // A step without a measure (libserum's archive code): a busy bar.
        m_bar->setRange(0, 0);
    }
    // Once the files are being replaced the save can no longer be undone.
    m_cancelButton->setEnabled(!cancelling && stage != legacy::LegacyIoStage::Replacing);
}

QString ProjectTaskDialog::statusText() const
{
    return m_label->text();
}

bool ProjectTaskDialog::isCancelEnabled() const
{
    return m_cancelButton->isEnabled();
}

std::optional<std::string> runProjectTask(QWidget* parent,
                                          const QString& title,
                                          legacy::LegacyIoProgress& progress,
                                          const std::function<void()>& work)
{
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::optional<std::string> failure;

    const std::function<void()> startHook = g_workerStartHook;
    std::thread worker([&]() {
        std::optional<std::string> message;
        try {
            if (startHook) {
                startHook();
            }
            work();
        } catch (const std::exception& ex) {
            message = ex.what();
        } catch (...) {
            message = "unknown error";
        }
        const std::lock_guard<std::mutex> lock(mutex);
        failure = std::move(message);
        finished = true;
        condition.notify_all();
    });

    bool done = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        done = condition.wait_for(lock, std::chrono::milliseconds(g_dialogDelayMs),
                                  [&finished]() { return finished; });
    }
    if (!done) {
        ProjectTaskDialog dialog(title, progress, [&]() {
            const std::lock_guard<std::mutex> lock(mutex);
            return finished;
        }, parent);
        dialog.show();
        if (g_shownHook) {
            g_shownHook(dialog);
        }
        dialog.exec();
    }
    worker.join();
    return failure;
}

int projectTaskDialogDelayMs()
{
    return g_dialogDelayMs;
}

void setProjectTaskDialogDelayMs(int milliseconds)
{
    g_dialogDelayMs = std::max(0, milliseconds);
}

void setProjectTaskDialogShownHook(std::function<void(ProjectTaskDialog&)> hook)
{
    g_shownHook = std::move(hook);
}

void setProjectTaskWorkerStartHook(std::function<void()> hook)
{
    g_workerStartHook = std::move(hook);
}

}  // namespace colorizing::app
