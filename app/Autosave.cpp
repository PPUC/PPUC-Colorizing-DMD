#include "Autosave.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <exception>
#include <utility>

#include "legacy/LegacyProjectFileWriter.h"

namespace colorizing::app {

namespace {

const QString kAutoSuffix = QStringLiteral("(auto)");

QString autosaveSibling(const QString& path, const QString& extension)
{
    const QFileInfo info(path);
    return info.dir().absoluteFilePath(info.completeBaseName() + kAutoSuffix + QChar('.') +
                                       extension);
}

}  // namespace

AutosaveFiles autosaveFilesFor(const QString& cromPath, const QString& crpPath)
{
    // The cROM of a project opened from its .cRZ is saved as .cROM beside it; so is its
    // autosave.
    return AutosaveFiles{autosaveSibling(cromPath, QStringLiteral("cROM")),
                         autosaveSibling(crpPath, QStringLiteral("cRPX"))};
}

QString untitledAutosaveDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .absoluteFilePath(QStringLiteral("autosave"));
}

AutosaveFiles untitledAutosaveFiles(const std::string& romName)
{
    const QDir directory(untitledAutosaveDirectory());
    const QString base = QString::fromStdString(romName) + kAutoSuffix;
    return AutosaveFiles{directory.absoluteFilePath(base + QStringLiteral(".cROM")),
                         directory.absoluteFilePath(base + QStringLiteral(".cRPX"))};
}

bool isAutosavePath(const QString& path)
{
    return QFileInfo(path).completeBaseName().endsWith(kAutoSuffix);
}

std::optional<QDateTime> newerAutosaveTime(const QString& cromPath,
                                           const QString& crpPath,
                                           const AutosaveFiles& autosave)
{
    const QFileInfo autoCrom(autosave.cromPath);
    const QFileInfo autoCrp(autosave.crpPath);
    if (!autoCrom.isFile() || !autoCrp.isFile()) {
        return std::nullopt;
    }
    const QDateTime autosaved = std::min(autoCrom.lastModified(), autoCrp.lastModified());
    for (const QString& path : {cromPath, crpPath}) {
        const QFileInfo project(path);
        if (project.isFile() && project.lastModified() >= autosaved) {
            return std::nullopt;
        }
    }
    return autosaved;
}

std::vector<AutosaveFiles> untitledAutosaves()
{
    const QDir directory(untitledAutosaveDirectory());
    std::vector<std::pair<QDateTime, AutosaveFiles>> found;
    const QFileInfoList croms = directory.entryInfoList(
        {QStringLiteral("*") + kAutoSuffix + QStringLiteral(".cROM")}, QDir::Files);
    for (const QFileInfo& crom : croms) {
        const QFileInfo crp(directory.absoluteFilePath(crom.completeBaseName() +
                                                       QStringLiteral(".cRPX")));
        if (crp.isFile()) {
            found.emplace_back(std::max(crom.lastModified(), crp.lastModified()),
                               AutosaveFiles{crom.absoluteFilePath(), crp.absoluteFilePath()});
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    std::vector<AutosaveFiles> files;
    for (auto& entry : found) {
        files.push_back(std::move(entry.second));
    }
    return files;
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
    const QString directory = QFileInfo(run->job.files.cromPath).absolutePath();
    m_run = run;
    m_thread = QThread::create([run, directory]() {
        try {
            QDir().mkpath(directory);
            const legacy::LegacyProjectPaths paths{run->job.files.cromPath.toStdString(),
                                                   run->job.files.crpPath.toStdString(),
                                                   {}};
            run->result =
                legacy::LegacyProjectFileWriter().save(paths, run->job.snapshot, &run->progress);
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
            m_finished(run->job.files, run->job.revision, run->result);
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
