#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "team/Claims.h"
#include "team/GitHub.h"
#include "team/TeamChanges.h"

namespace colorizing::app {

// Where a team's claims and packages are: claims.json and the folder packages/. The GitHub
// repository in use (GitHubRemote); a plain folder for tests (FolderRemote). Every call
// answers through its callback; an error is a text for the user, empty on success.
class TeamRemote {
public:
    virtual ~TeamRemote() = default;
    // The claims file and its version (what writeClaims() must replace); nothing for none.
    virtual void readClaims(
        std::function<void(std::optional<QByteArray>, QString version, QString error)> done) = 0;
    // conflict: the file changed meanwhile.
    virtual void writeClaims(const QByteArray& text,
                             const QString& version,
                             std::function<void(QString error, bool conflict)> done) = 0;
    virtual void listPackages(std::function<void(std::vector<QString> names, QString error)>
                                  done) = 0;
    virtual void readPackage(const QString& name,
                             std::function<void(std::optional<QByteArray>, QString error)>
                                 done) = 0;
    virtual void writePackage(const QString& name,
                              const QByteArray& bytes,
                              std::function<void(QString error)> done) = 0;
};

class GitHubRemote final : public TeamRemote {
public:
    GitHubRemote(GitHubClient& client, QString repository);
    void readClaims(std::function<void(std::optional<QByteArray>, QString, QString)> done)
        override;
    void writeClaims(const QByteArray& text,
                     const QString& version,
                     std::function<void(QString, bool)> done) override;
    void listPackages(std::function<void(std::vector<QString>, QString)> done) override;
    void readPackage(const QString& name,
                     std::function<void(std::optional<QByteArray>, QString)> done) override;
    void writePackage(const QString& name,
                      const QByteArray& bytes,
                      std::function<void(QString)> done) override;

private:
    GitHubClient& m_client;
    QString m_repository;
};

// A folder as the remote, at once: for tests.
class FolderRemote final : public TeamRemote {
public:
    explicit FolderRemote(QString folder);
    void readClaims(std::function<void(std::optional<QByteArray>, QString, QString)> done)
        override;
    void writeClaims(const QByteArray& text,
                     const QString& version,
                     std::function<void(QString, bool)> done) override;
    void listPackages(std::function<void(std::vector<QString>, QString)> done) override;
    void readPackage(const QString& name,
                     std::function<void(std::optional<QByteArray>, QString)> done) override;
    void writePackage(const QString& name,
                      const QByteArray& bytes,
                      std::function<void(QString)> done) override;

private:
    QString m_folder;
};

// The name of a change package in packages/: "<created ms>-<login>-<package ID>.cdmdchanges",
// so that names sort by time and say who made them.
[[nodiscard]] QString changePackageName(const team::ChangePackage& package);
struct PackageName {
    int64_t createdMs = 0;
    QString login;
    document::ComponentId packageId;
};
[[nodiscard]] std::optional<PackageName> parsePackageName(const QString& name);

// A change package as the bytes of its file, and back (through a temporary file, as the zip
// code works on files).
[[nodiscard]] std::optional<QByteArray> changePackageBytes(const team::ChangePackage& package,
                                                           QString& error);
[[nodiscard]] std::optional<team::ChangePackage> changePackageFrom(const QByteArray& bytes,
                                                                   QString& error);

// The team steps that talk to the remote, for the open team project.
class TeamSession {
public:
    TeamSession(TeamRemote& remote, QString login);

    [[nodiscard]] const QString& login() const { return m_login; }

    // Sends the copy's changes as a package; on success the document records it as sent
    // (TeamState::sent) and the claims are brought up to date. The document must be saved;
    // it is unsaved afterwards (the team state changed).
    void share(document::ProjectDocument& document,
               int64_t nowMs,
               std::function<void(QString error, bool nothingToShare)> done);

    // The partners' packages this copy has not merged, oldest first.
    void pendingPackages(const document::ProjectDocument& document,
                         std::function<void(std::vector<QString> names, QString error)> done);
    // The same, without the document: the packages merged already given.
    void pendingPackages(std::vector<document::ComponentId> merged,
                         std::function<void(std::vector<QString> names, QString error)> done);
    void fetchPackage(const QString& name,
                      std::function<void(std::optional<team::ChangePackage>, QString error)>
                          done);

    // Makes this member's claims what the copy's changes and reservations are now, and
    // answers all claims; a write that comes too late is retried with the new claims.
    void syncClaims(const document::ProjectDocument& document,
                    const std::vector<team::Reservation>& reserved,
                    int64_t nowMs,
                    std::function<void(std::optional<team::Claims>, QString error)> done);
    // The same for a claim set worked out before (in the background: TeamSync).
    void syncClaims(team::ChangeSet claimed,
                    std::vector<team::Reservation> reserved,
                    int64_t nowMs,
                    std::function<void(std::optional<team::Claims>, QString error)> done);
    // The claims as they are, without writing.
    void readClaims(std::function<void(std::optional<team::Claims>, QString error)> done);

private:
    void syncClaimsAttempt(team::ChangeSet changes,
                           std::vector<team::Reservation> reserved,
                           int64_t nowMs,
                           int attempts,
                           std::function<void(std::optional<team::Claims>, QString)> done);

    TeamRemote& m_remote;
    QString m_login;
};

}  // namespace colorizing::app
