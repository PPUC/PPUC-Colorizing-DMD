#pragma once

#include <QByteArray>
#include <QString>
#include <QThread>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "document/ComponentId.h"
#include "team/Claims.h"
#include "team/GitHub.h"
#include "team/TeamChanges.h"

class QObject;

namespace colorizing::app {

class TeamRemote;

// Keeps a team project in step with its partners in the background, in a thread of its own
// with its own network access, so that the editor never waits for it: at an interval (and soon
// after the input changed) it writes this copy's claims and reads the partners', lists the
// partners' packages not merged yet and downloads them into a cache folder, from which a merge
// then reads them.
class TeamSync {
public:
    struct Input {
        // Nothing to do while false (no team project, signed out).
        bool active = false;
        QString clientId;
        QString token;
        QString login;
        QString repository;
        // What this copy claims: its changes since the common state, and what was edited
        // since the last save.
        team::ChangeSet claimed;
        std::vector<team::Reservation> reserved;
        std::vector<document::ComponentId> mergedPackages;
        // Where the partners' packages are downloaded to.
        QString cacheFolder;
    };
    struct Result {
        // The claims as written and read; nothing when they could not be.
        std::optional<team::Claims> claims;
        // The partners' packages not merged yet, oldest first; those that could be downloaded
        // are in the cache.
        std::vector<QString> pendingPackages;
        QString error;
        // The setInput() call the result answers; an older one's result is outdated.
        uint64_t input = 0;
    };
    // Makes the remote for an input, in the sync's thread (tests use a folder).
    using RemoteFactory =
        std::function<std::unique_ptr<TeamRemote>(const Input& input, HttpTransport& transport)>;

    // delivered is called on the thread that made the sync, for every sync done; context: the
    // object it is called through (it is not called once context is gone).
    TeamSync(QObject* context,
             std::function<void(Result)> delivered,
             std::chrono::milliseconds interval,
             RemoteFactory factory = {});
    ~TeamSync();
    TeamSync(const TeamSync&) = delete;
    TeamSync& operator=(const TeamSync&) = delete;

    // Takes the new input and syncs soon (now, or after the sync running). Returns its number.
    uint64_t setInput(Input input);
    void syncNow();

    // A package the sync downloaded, if it is in the cache.
    [[nodiscard]] static std::optional<QByteArray> cachedPackage(const QString& cacheFolder,
                                                                 const QString& name);

private:
    struct Worker;

    QThread m_thread;
    QObject* m_threadContext = nullptr;
    // Lives in the thread; made and deleted there.
    Worker* m_worker = nullptr;
    uint64_t m_inputs = 0;
};

}  // namespace colorizing::app
