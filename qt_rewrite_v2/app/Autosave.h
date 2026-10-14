#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectIo.h"

class QThread;

namespace colorizing::app {

// Where a project's autosave goes. As in the Win32 editor: "<name>(auto).cROM" and
// "<name>(auto).cRP" beside the project. A project that was never saved has its autosave in
// untitledAutosaveDirectory(), under its ROM name.
struct AutosaveFiles {
    QString cromPath;
    QString crpPath;
};

[[nodiscard]] AutosaveFiles autosaveFilesFor(const QString& cromPath, const QString& crpPath);
[[nodiscard]] QString untitledAutosaveDirectory();
[[nodiscard]] AutosaveFiles untitledAutosaveFiles(const std::string& romName);
// Whether the file is an autosave itself.
[[nodiscard]] bool isAutosavePath(const QString& path);
// When both autosave files exist and are newer than both project files: the time of the older
// autosave file. A .cRZ project is compared by its archive.
[[nodiscard]] std::optional<QDateTime> newerAutosaveTime(const QString& cromPath,
                                                         const QString& crpPath,
                                                         const AutosaveFiles& autosave);
// The autosaves of never saved projects that are complete (both files), newest first.
[[nodiscard]] std::vector<AutosaveFiles> untitledAutosaves();

// Writes the open project to its autosave files at an interval, on a worker thread, while the
// editor goes on. Each run takes a snapshot that shares its blocks with the document (copy on
// write), so it copies no pixels and later edits do not reach the files being written.
class Autosaver final : public QObject {
public:
    struct Job {
        AutosaveFiles files;
        document::ProjectSnapshot snapshot;
        // The document revision the snapshot shows.
        uint64_t revision = 0;
    };
    // Returns the job to run now, or nothing (no project, no change since the last autosave,
    // or the editor is busy).
    using JobSource = std::function<std::optional<Job>()>;
    using Finished = std::function<void(const AutosaveFiles& files,
                                        uint64_t revision,
                                        const legacy::LegacySaveResult& result)>;

    Autosaver(QObject* parent,
              std::chrono::milliseconds interval,
              JobSource source,
              Finished finished);
    ~Autosaver() override;

    // Starts a run now unless one is running; false when none was started.
    bool runNow();
    [[nodiscard]] bool isRunning() const;
    // Stops a running write (the files it was writing are left as they were) and waits for it.
    // The finished callback is not called for it.
    void cancelAndWait();
    [[nodiscard]] std::chrono::milliseconds interval() const;

private:
    struct Run;

    QTimer m_timer;
    JobSource m_source;
    Finished m_finished;
    std::shared_ptr<Run> m_run;
    QThread* m_thread = nullptr;
};

}  // namespace colorizing::app
