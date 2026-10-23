#include "team/TeamSession.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <exception>
#include <utility>

#include "AppInfo.h"

namespace colorizing::app {

namespace {

const QString kClaimsFile = QStringLiteral("claims.json");
const QString kPackagesFolder = QStringLiteral("packages");
const QString kPackageSuffix = QStringLiteral(".cdmdchanges");
const QString kNoticeSuffix = QStringLiteral(".cdmdnotice");

}  // namespace

GitHubRemote::GitHubRemote(GitHubClient& client, QString repository)
    : m_client(client),
      m_repository(std::move(repository))
{
}

void GitHubRemote::readClaims(
    std::function<void(std::optional<QByteArray>, QString, QString)> done)
{
    m_client.getFile(m_repository, kClaimsFile,
                     [done = std::move(done)](std::optional<GitHubClient::File> file,
                                              QString error) {
                         if (!file) {
                             done(std::nullopt, QString(), error);
                             return;
                         }
                         done(file->bytes, file->sha, QString());
                     });
}

void GitHubRemote::writeClaims(const QByteArray& text,
                               const QString& version,
                               std::function<void(QString, bool)> done)
{
    m_client.putFile(m_repository, kClaimsFile, text, QStringLiteral("Claims"), version,
                     [done = std::move(done)](QString, QString error, bool conflict) {
                         done(error, conflict);
                     });
}

void GitHubRemote::listPackages(std::function<void(std::vector<QString>, QString)> done)
{
    m_client.listFolder(m_repository, kPackagesFolder,
                        [done = std::move(done)](std::vector<GitHubClient::Entry> entries,
                                                 QString error) {
                            std::vector<QString> names;
                            for (const GitHubClient::Entry& entry : entries) {
                                names.push_back(entry.name);
                            }
                            done(std::move(names), error);
                        });
}

void GitHubRemote::readPackage(const QString& name,
                               std::function<void(std::optional<QByteArray>, QString)> done)
{
    m_client.getFile(m_repository, kPackagesFolder + QLatin1Char('/') + name,
                     [done = std::move(done)](std::optional<GitHubClient::File> file,
                                              QString error) {
                         if (!file) {
                             done(std::nullopt, error.isEmpty()
                                                    ? QStringLiteral("the package is gone")
                                                    : error);
                             return;
                         }
                         done(file->bytes, QString());
                     });
}

void GitHubRemote::writePackage(const QString& name,
                                const QByteArray& bytes,
                                std::function<void(QString)> done)
{
    m_client.putFile(m_repository, kPackagesFolder + QLatin1Char('/') + name, bytes,
                     QStringLiteral("Changes %1").arg(name), QString(),
                     [done = std::move(done)](QString, QString error, bool) { done(error); });
}

FolderRemote::FolderRemote(QString folder)
    : m_folder(std::move(folder))
{
}

namespace {

QString versionOf(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

std::optional<QByteArray> readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return file.readAll();
}

bool writeAll(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

}  // namespace

void FolderRemote::readClaims(std::function<void(std::optional<QByteArray>, QString, QString)> done)
{
    const std::optional<QByteArray> bytes = readAll(QDir(m_folder).filePath(kClaimsFile));
    done(bytes, bytes ? versionOf(*bytes) : QString(), QString());
}

void FolderRemote::writeClaims(const QByteArray& text,
                               const QString& version,
                               std::function<void(QString, bool)> done)
{
    const QString path = QDir(m_folder).filePath(kClaimsFile);
    const std::optional<QByteArray> current = readAll(path);
    if ((current ? versionOf(*current) : QString()) != version) {
        done(QStringLiteral("the claims changed meanwhile"), true);
        return;
    }
    done(writeAll(path, text) ? QString() : QStringLiteral("the claims could not be written"),
         false);
}

void FolderRemote::listPackages(std::function<void(std::vector<QString>, QString)> done)
{
    std::vector<QString> names;
    for (const QString& name :
         QDir(QDir(m_folder).filePath(kPackagesFolder)).entryList(QDir::Files, QDir::Name)) {
        names.push_back(name);
    }
    done(std::move(names), QString());
}

void FolderRemote::readPackage(const QString& name,
                               std::function<void(std::optional<QByteArray>, QString)> done)
{
    const std::optional<QByteArray> bytes =
        readAll(QDir(QDir(m_folder).filePath(kPackagesFolder)).filePath(name));
    done(bytes, bytes ? QString() : QStringLiteral("the package is gone"));
}

void FolderRemote::writePackage(const QString& name,
                                const QByteArray& bytes,
                                std::function<void(QString)> done)
{
    const bool written = writeAll(QDir(QDir(m_folder).filePath(kPackagesFolder)).filePath(name),
                                  bytes);
    done(written ? QString() : QStringLiteral("the package could not be written"));
}

QString changePackageName(const team::ChangePackage& package)
{
    return QStringLiteral("%1-%2-%3%4")
        .arg(package.createdMs, 13, 10, QLatin1Char('0'))
        .arg(QString::fromStdString(package.author),
             QString::fromStdString(package.packageId.toString()), kPackageSuffix);
}

QString changePackageNoticeName(const team::ChangePackage& package)
{
    return changePackageName(package).chopped(kPackageSuffix.size()) + kNoticeSuffix;
}

QByteArray changePackageNotice(const team::ChangePackage& package, qint64 sizeBytes)
{
    QJsonObject notice;
    notice.insert(QStringLiteral("author"), QString::fromStdString(package.author));
    notice.insert(QStringLiteral("created"), static_cast<double>(package.createdMs));
    notice.insert(QStringLiteral("comment"), QString::fromStdString(package.comment));
    notice.insert(QStringLiteral("size"), static_cast<double>(sizeBytes));
    return QJsonDocument(notice).toJson();
}

bool isPackageNotice(const QString& name)
{
    return name.endsWith(kNoticeSuffix);
}

QString packageFileName(const QString& name)
{
    return isPackageNotice(name) ? name.chopped(kNoticeSuffix.size()) + kPackageSuffix : name;
}

std::optional<PendingNote> pendingNoteFromNotice(const QByteArray& bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject notice = document.object();
    PendingNote note;
    note.asFile = true;
    note.available = false;
    note.comment = notice.value(QStringLiteral("comment")).toString();
    note.sizeBytes = static_cast<qint64>(notice.value(QStringLiteral("size")).toDouble());
    return note;
}

QString addPackageFileToCache(const QByteArray& bytes,
                              const document::ProjectDocument& document,
                              const QString& cacheFolder,
                              QString* name)
{
    QString error;
    const std::optional<team::ChangePackage> package = changePackageFrom(bytes, error);
    if (!package) {
        return QCoreApplication::translate("TeamSession", "The file is no shared change: %1")
            .arg(error);
    }
    if (!document.team() || package->snapshotId != document.team()->snapshotId) {
        return QCoreApplication::translate(
            "TeamSession",
            "The shared change belongs to another team project, or to another snapshot of it.");
    }
    const std::vector<document::ComponentId>& merged = document.team()->mergedPackages;
    if (std::find(merged.begin(), merged.end(), package->packageId) != merged.end()) {
        return QCoreApplication::translate("TeamSession",
                                           "The shared change is merged already.");
    }
    const QString fileName = changePackageName(*package);
    if (!QDir().mkpath(cacheFolder) || !writeAll(QDir(cacheFolder).filePath(fileName), bytes)) {
        return QCoreApplication::translate("TeamSession",
                                           "The shared change could not be stored in %1.")
            .arg(cacheFolder);
    }
    if (name) {
        *name = fileName;
    }
    return {};
}

std::optional<PackageName> parsePackageName(const QString& name)
{
    const QString suffix = isPackageNotice(name) ? kNoticeSuffix : kPackageSuffix;
    if (!name.endsWith(suffix)) {
        return std::nullopt;
    }
    const QString stem = name.chopped(suffix.size());
    // A UUID has 36 characters; the login (GitHub's: letters, digits, hyphens) lies between.
    const int firstDash = stem.indexOf(QLatin1Char('-'));
    if (firstDash < 0 || stem.size() < firstDash + 2 + 36) {
        return std::nullopt;
    }
    bool number = false;
    PackageName parsed;
    parsed.createdMs = stem.left(firstDash).toLongLong(&number);
    const std::optional<document::ComponentId> id =
        document::ComponentId::fromString(stem.right(36).toStdString());
    if (!number || !id || stem.at(stem.size() - 37) != QLatin1Char('-')) {
        return std::nullopt;
    }
    parsed.packageId = *id;
    parsed.login = stem.mid(firstDash + 1, stem.size() - 37 - firstDash - 1);
    return parsed;
}

std::optional<QByteArray> changePackageBytes(const team::ChangePackage& package, QString& error)
{
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("changes") + kPackageSuffix);
    const legacy::LegacySaveResult written =
        team::writeChangePackage(path.toStdString(), package, kApplicationVersion.toStdString());
    if (!folder.isValid() || !written.ok) {
        error = QString::fromStdString(written.error.message);
        return std::nullopt;
    }
    std::optional<QByteArray> bytes = readAll(path);
    if (!bytes) {
        error = QStringLiteral("the package could not be read back");
    }
    return bytes;
}

std::optional<team::ChangePackage> changePackageFrom(const QByteArray& bytes, QString& error)
{
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("changes") + kPackageSuffix);
    if (!folder.isValid() || !writeAll(path, bytes)) {
        error = QStringLiteral("no temporary folder for the package");
        return std::nullopt;
    }
    team::ChangePackageLoad loaded = team::readChangePackage(path.toStdString());
    if (!loaded.package) {
        error = QString::fromStdString(loaded.error);
    }
    return std::move(loaded.package);
}

TeamSession::TeamSession(TeamRemote& remote, QString login)
    : m_remote(remote),
      m_login(std::move(login))
{
}

TeamSession::PreparedShare TeamSession::prepareShare(
    const document::ProjectDocument& document,
    int64_t nowMs,
    const QString& comment,
    const std::map<std::string, uint64_t>& authorTimes) const
{
    PreparedShare prepared;
    try {
        if (team::changesSinceCommon(document).empty()) {
            prepared.nothing = true;
            return prepared;
        }
        prepared.package = team::makeChangePackage(document, m_login.toStdString(), nowMs);
        prepared.package->comment = comment.trimmed().toStdString();
        prepared.package->authorTimes = authorTimes;
    } catch (const std::exception& ex) {
        prepared.error = QString::fromUtf8(ex.what());
        prepared.package.reset();
        return prepared;
    }
    std::optional<QByteArray> bytes = changePackageBytes(*prepared.package, prepared.error);
    if (!bytes) {
        prepared.package.reset();
        return prepared;
    }
    prepared.bytes = std::move(*bytes);
    return prepared;
}

void TeamSession::publish(document::ProjectDocument& document,
                          const PreparedShare& prepared,
                          bool asFile,
                          int64_t nowMs,
                          std::function<void(QString)> done)
{
    if (!prepared.package) {
        done(prepared.error);
        return;
    }
    const team::ChangePackage& package = *prepared.package;
    const document::SentPackage sent = team::sentPackageOf(package);
    m_remote.writePackage(
        asFile ? changePackageNoticeName(package) : changePackageName(package),
        asFile ? changePackageNotice(package, prepared.bytes.size()) : prepared.bytes,
        [this, &document, sent, nowMs, done = std::move(done)](QString writeError) {
            if (!writeError.isEmpty()) {
                done(writeError);
                return;
            }
            document::TeamState team = *document.team();
            team.sent.push_back(sent);
            document.setTeam(std::move(team));
            syncClaims(document, {}, nowMs,
                       [done](std::optional<team::Claims>, QString claimsError) {
                           done(claimsError);
                       });
        });
}

void TeamSession::share(document::ProjectDocument& document,
                        int64_t nowMs,
                        const QString& comment,
                        std::function<void(QString error, bool nothingToShare)> done)
{
    const PreparedShare prepared = prepareShare(document, nowMs, comment);
    if (prepared.nothing) {
        done(QString(), true);
        return;
    }
    publish(document, prepared, false, nowMs,
            [done = std::move(done)](QString error) { done(error, false); });
}

void TeamSession::pendingPackages(const document::ProjectDocument& document,
                                  std::function<void(std::vector<QString>, QString)> done)
{
    pendingPackages(document.team() ? document.team()->mergedPackages
                                    : std::vector<document::ComponentId>{},
                    std::move(done));
}

void TeamSession::pendingPackages(std::vector<document::ComponentId> merged,
                                  std::function<void(std::vector<QString>, QString)> done)
{
    m_remote.listPackages([this, merged, done = std::move(done)](std::vector<QString> names,
                                                                QString error) {
        std::vector<QString> pending;
        for (const QString& name : names) {
            const std::optional<PackageName> parsed = parsePackageName(name);
            if (!parsed || parsed->login == m_login ||
                std::find(merged.begin(), merged.end(), parsed->packageId) != merged.end()) {
                continue;
            }
            pending.push_back(name);
        }
        std::sort(pending.begin(), pending.end());
        done(std::move(pending), error);
    });
}

void TeamSession::fetchPackage(
    const QString& name,
    std::function<void(std::optional<team::ChangePackage>, QString)> done)
{
    m_remote.readPackage(name, [done = std::move(done)](std::optional<QByteArray> bytes,
                                                        QString error) {
        if (!bytes) {
            done(std::nullopt, error);
            return;
        }
        QString readError;
        std::optional<team::ChangePackage> package = changePackageFrom(*bytes, readError);
        done(std::move(package), readError);
    });
}

void TeamSession::syncClaims(const document::ProjectDocument& document,
                             const std::vector<team::Reservation>& reserved,
                             int64_t nowMs,
                             std::function<void(std::optional<team::Claims>, QString)> done)
{
    team::ChangeSet changes;
    try {
        changes = team::changesSinceCommon(document);
    } catch (const std::exception& ex) {
        done(std::nullopt, QString::fromUtf8(ex.what()));
        return;
    }
    syncClaimsAttempt(std::move(changes), reserved, nowMs, 3, std::move(done));
}

void TeamSession::syncClaims(team::ChangeSet claimed,
                             std::vector<team::Reservation> reserved,
                             int64_t nowMs,
                             std::function<void(std::optional<team::Claims>, QString)> done)
{
    syncClaimsAttempt(std::move(claimed), std::move(reserved), nowMs, 3, std::move(done));
}

void TeamSession::syncClaimsAttempt(team::ChangeSet changes,
                                    std::vector<team::Reservation> reserved,
                                    int64_t nowMs,
                                    int attempts,
                                    std::function<void(std::optional<team::Claims>, QString)> done)
{
    m_remote.readClaims([this, changes, reserved, nowMs, attempts, done = std::move(done)](
                            std::optional<QByteArray> text, QString version, QString error) {
        if (!error.isEmpty()) {
            done(std::nullopt, error);
            return;
        }
        std::string parseError;
        const std::optional<team::Claims> current =
            team::parseClaims(text ? text->toStdString() : std::string(), parseError);
        if (!current) {
            done(std::nullopt, QString::fromStdString(parseError));
            return;
        }
        const team::Claims mine =
            team::withMyClaims(*current, m_login.toStdString(), changes, reserved, nowMs);
        if (mine == *current) {
            done(mine, QString());
            return;
        }
        m_remote.writeClaims(
            QByteArray::fromStdString(team::writeClaims(mine)), version,
            [this, changes, reserved, nowMs, attempts, mine, done](QString writeError,
                                                                   bool conflict) {
                if (conflict && attempts > 1) {
                    syncClaimsAttempt(changes, reserved, nowMs, attempts - 1, done);
                    return;
                }
                done(writeError.isEmpty() ? std::optional<team::Claims>(mine) : std::nullopt,
                     writeError);
            });
    });
}

void TeamSession::readClaims(std::function<void(std::optional<team::Claims>, QString)> done)
{
    m_remote.readClaims([done = std::move(done)](std::optional<QByteArray> text, QString,
                                                 QString error) {
        if (!error.isEmpty()) {
            done(std::nullopt, error);
            return;
        }
        std::string parseError;
        std::optional<team::Claims> claims =
            team::parseClaims(text ? text->toStdString() : std::string(), parseError);
        done(std::move(claims), QString::fromStdString(parseError));
    });
}

}  // namespace colorizing::app
