#include "Autosave.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <exception>
#include <utility>

#include "AppInfo.h"
#include "project/ProjectFile.h"

namespace colorizing::app {

namespace {

const QString kAutoSuffix = QStringLiteral("(auto)");
const QString kProjectSuffix = QStringLiteral(".cdmd");

}  // namespace

QString autosavePathFor(const QString& projectPath)
{
    const QFileInfo info(projectPath);
    return info.dir().absoluteFilePath(info.completeBaseName() + kAutoSuffix + kProjectSuffix);
}

QString untitledAutosaveDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .absoluteFilePath(QStringLiteral("autosave"));
}

QString untitledAutosavePath(const std::string& romName)
{
    return QDir(untitledAutosaveDirectory())
        .absoluteFilePath(QString::fromStdString(romName) + kAutoSuffix + kProjectSuffix);
}

bool isAutosavePath(const QString& path)
{
    return QFileInfo(path).completeBaseName().endsWith(kAutoSuffix);
}

std::optional<QDateTime> newerAutosaveTime(const QString& projectPath,
                                           const QString& autosavePath)
{
    const QFileInfo autosave(autosavePath);
    if (!autosave.isFile()) {
        return std::nullopt;
    }
    const QFileInfo project(projectPath);
    if (project.isFile() && project.lastModified() >= autosave.lastModified()) {
        return std::nullopt;
    }
    return autosave.lastModified();
}

std::vector<QString> untitledAutosaves()
{
    const QDir directory(untitledAutosaveDirectory());
    QFileInfoList found = directory.entryInfoList(
        {QStringLiteral("*") + kAutoSuffix + kProjectSuffix}, QDir::Files);
    std::sort(found.begin(), found.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified() > b.lastModified();
    });
    std::vector<QString> paths;
    for (const QFileInfo& file : found) {
        paths.push_back(file.absoluteFilePath());
    }
    return paths;
}

struct Autosaver::Run {
    Job job;
    legacy::LegacyIoProgress progress;
    legacy::LegacySaveResult result;
    bool abandoned = false;
};

Autosaver::Autosaver(QObject* parent,
                     std::chrono::milliseconds interval,
                     JobSource source,
                     Finished finished)
    : QObject(parent),
      m_source(std::move(source)),
      m_finished(std::move(finished))
{
    m_timer.setInterval(static_cast<int>(interval.count()));
    connect(&m_timer, &QTimer::timeout, this, [this]() { runNow(); });
    m_timer.start();
}

Autosaver::~Autosaver()
{
    cancelAndWait();
}

bool Autosaver::runNow()
{
    if (isRunning()) {
        return false;
    }
    std::optional<Job> job = m_source ? m_source() : std::nullopt;
    if (!job) {
        return false;
    }
    auto run = std::make_shared<Run>();
    run->job = std::move(*job);
    const QString directory = QFileInfo(run->job.path).absolutePath();
    m_run = run;
    m_thread = QThread::create([run, directory]() {
        try {
            QDir().mkpath(directory);
            project::ProjectFileOptions options;
            options.editorVersion = kApplicationVersion.toStdString();
            run->result = project::saveProjectFile(run->job.path.toStdString(),
                                                   run->job.snapshot, options, &run->progress);
        } catch (const std::exception& ex) {
            run->result = legacy::makeSaveFailure(ex.what());
        }
        // The snapshot's blocks are let go here, so that the document writes in place again.
        run->job.snapshot = {};
    });
    QThread* thread = m_thread;
    connect(thread, &QThread::finished, this, [this, run, thread]() {
        // An abandoned run was waited for and its thread released by cancelAndWait().
        if (run->abandoned) {
            return;
        }
        thread->deleteLater();
        m_thread = nullptr;
        m_run.reset();
        if (m_finished) {
            m_finished(run->job.path, run->job.revision, run->result);
        }
    });
    thread->start(QThread::LowPriority);
    return true;
}

bool Autosaver::isRunning() const
{
    return m_thread != nullptr;
}

void Autosaver::cancelAndWait()
{
    if (!m_thread) {
        return;
    }
    m_run->abandoned = true;
    m_run->progress.cancel = true;
    m_thread->wait();
    // The finished handler may still be queued; it finds the run abandoned and leaves the
    // thread alone.
    delete m_thread;
    m_thread = nullptr;
    m_run.reset();
}

std::chrono::milliseconds Autosaver::interval() const
{
    return std::chrono::milliseconds(m_timer.interval());
}

}  // namespace colorizing::app
