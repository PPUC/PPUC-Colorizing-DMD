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
#include <vector>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectIo.h"

class QThread;

namespace colorizing::app {

// Where a project's autosave goes. As in the Win32 editor, but as a project file of this
// editor: "<name>(auto).cdmd" beside the project. A project that has no project file yet (a
// new one, or one opened from the files of an earlier editor) has its autosave in
// untitledAutosaveDirectory(), under its ROM name.
[[nodiscard]] QString autosavePathFor(const QString& projectPath);
[[nodiscard]] QString untitledAutosaveDirectory();
[[nodiscard]] QString untitledAutosavePath(const std::string& romName);
// Whether the file is an autosave itself.
[[nodiscard]] bool isAutosavePath(const QString& path);
// When the autosave exists and is newer than the project file: its time.
[[nodiscard]] std::optional<QDateTime> newerAutosaveTime(const QString& projectPath,
                                                         const QString& autosavePath);
// The autosaves of projects without a project file, newest first.
[[nodiscard]] std::vector<QString> untitledAutosaves();

// Writes the open project to its autosave file at an interval, on a worker thread, while the
// editor goes on. Each run takes a snapshot that shares its blocks with the document (copy on
// write), so it copies no pixels and later edits do not reach the file being written.
class Autosaver final : public QObject {
public:
    struct Job {
        QString path;
        // Where a team project's common versions are (ProjectFileOptions::baseSourcePath).
        QString baseSourcePath;
        document::ProjectSnapshot snapshot;
        // The document revision the snapshot shows.
        uint64_t revision = 0;
    };
    // Returns the job to run now, or nothing (no project, no change since the last autosave,
    // or the editor is busy).
    using JobSource = std::function<std::optional<Job>()>;
    using Finished = std::function<void(const QString& path,
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
    // Stops a running write (the file it was writing is left as it was) and waits for it.
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
