// The team session (team/TeamSession.h) with a folder as the team's remote: package names,
// two copies sharing and merging through it, and the claims they leave there. A large change
// shared as a file: only its notice on the remote, the file added by the partner. The history of
// the shared changes, as Team -> History and the merge show it.

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QObject>
#include <QTemporaryDir>
#include <QTreeWidgetItem>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "project/ProjectFile.h"
#include "team/TeamHistoryDialog.h"
#include "team/TeamMerge.h"
#include "team/TeamSession.h"
#include "team/TeamSync.h"

namespace {

using namespace colorizing;
using document::ProjectDocument;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: team session: " << message << '\n';
        std::exit(1);
    }
}

void save(ProjectDocument& document, const QString& path, const std::string& author,
          int64_t time)
{
    project::ProjectFileOptions options{"test"};
    options.stamp = project::RevisionStamp{author, time};
    std::vector<document::StampedMeta> stamped;
    const legacy::LegacySaveResult result = project::saveProjectFile(
        path.toStdString(), document.toSnapshot(), options, nullptr, &stamped);
    expect(result.ok, "saved: " + result.error.message);
    document.adoptComponentMeta(stamped);
    document.markClean();
}

void testPackageNames()
{
    team::ChangePackage package;
    package.createdMs = 1761200000000;
    package.author = "serum-fan-2";
    package.packageId = document::ComponentId::generate();
    const QString name = app::changePackageName(package);
    const std::optional<app::PackageName> parsed = app::parsePackageName(name);
    expect(name.startsWith(QStringLiteral("1761200000000-serum-fan-2-")) && parsed &&
               parsed->createdMs == package.createdMs &&
               parsed->login == QStringLiteral("serum-fan-2") &&
               parsed->packageId == package.packageId,
           "a package's name says when and by whom: " + name.toStdString());
    expect(!app::parsePackageName(QStringLiteral("claims.json")) &&
               !app::parsePackageName(QStringLiteral("x-y-z.cdmdchanges")),
           "other names are no packages");
}

void testShareAndMerge()
{
    QTemporaryDir folder;
    const QString remotePath = QDir(folder.path()).filePath(QStringLiteral("remote"));
    const QString ownerPath = QDir(folder.path()).filePath(QStringLiteral("kraken.cdmd"));
    const QString partnerPath = QDir(folder.path()).filePath(QStringLiteral("enthusiast.cdmd"));
    app::FolderRemote remote(remotePath);

    ProjectDocument owner = ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    save(owner, ownerPath, "kraken", 1000);
    owner.setTeam(team::startTeam(owner, "folder", "kraken", {"enthusiast"}));
    save(owner, ownerPath, "kraken", 1000);
    std::filesystem::copy_file(ownerPath.toStdString(), partnerPath.toStdString());
    ProjectDocument partner = std::move(*project::loadProjectDocument(partnerPath.toStdString())
                                             .document);

    app::TeamSession ownerSession(remote, QStringLiteral("kraken"));
    app::TeamSession partnerSession(remote, QStringLiteral("enthusiast"));
    bool nothing = false;
    QString error = QStringLiteral("unset");
    ownerSession.share(owner, 1500, QString(), [&](QString message, bool none) {
        error = message;
        nothing = none;
    });
    expect(error.isEmpty() && nothing, "nothing to share at first");

    owner.setFrameDurationMs(2, 222);
    save(owner, ownerPath, "kraken", 2000);
    ownerSession.share(owner, 2000, QStringLiteral(" Durations of the attract mode \n"),
                       [&](QString message, bool none) {
        error = message;
        nothing = none;
    });
    expect(error.isEmpty() && !nothing && owner.team()->sent.size() == 1 && owner.isDirty(),
           "the change is shared and recorded as sent: " + error.toStdString());
    save(owner, ownerPath, "kraken", 2000);

    std::optional<team::Claims> claims;
    partnerSession.readClaims([&](std::optional<team::Claims> answer, QString) {
        claims = answer;
    });
    expect(claims &&
               team::partnerClaim(*claims, document::ComponentKind::Frame,
                                  partner.frameMeta(2).id, "enthusiast") &&
               team::partnerClaim(*claims, document::ComponentKind::Frame,
                                  partner.frameMeta(2).id, "enthusiast")->login == "kraken",
           "the partner sees the owner's claim of the frame");

    std::vector<QString> pending;
    ownerSession.pendingPackages(owner, [&](std::vector<QString> names, QString) {
        pending = names;
    });
    expect(pending.empty(), "the owner's own package is not pending for the owner");
    partnerSession.pendingPackages(partner, [&](std::vector<QString> names, QString) {
        pending = names;
    });
    expect(pending.size() == 1, "it is for the partner");
    std::optional<team::ChangePackage> package;
    partnerSession.fetchPackage(pending[0], [&](std::optional<team::ChangePackage> answer,
                                                QString) { package = std::move(answer); });
    expect(package.has_value() && package->comment == "Durations of the attract mode",
           "and fetched, with the owner's comment, trimmed");
    (void)team::mergeChangePackage(partner, *package, {}, "enthusiast");
    expect(partner.frameDurationMs(2) == 222, "and merged");
    save(partner, partnerPath, "enthusiast", 3000);
    partnerSession.pendingPackages(partner, [&](std::vector<QString> names, QString) {
        pending = names;
    });
    expect(pending.empty(), "a merged package is not pending any more");

    partner.setFrameDurationMs(5, 55);
    save(partner, partnerPath, "enthusiast", 4000);
    partnerSession.share(partner, 4000, QString(), [&](QString message, bool) { error = message; });
    expect(error.isEmpty(), "the partner shares");
    ownerSession.pendingPackages(owner, [&](std::vector<QString> names, QString) {
        pending = names;
    });
    ownerSession.fetchPackage(pending.at(0), [&](std::optional<team::ChangePackage> answer,
                                                 QString) { package = std::move(answer); });
    (void)team::mergeChangePackage(owner, *package, {}, "kraken");
    expect(owner.frameDurationMs(5) == 55 && owner.team()->sent.empty(),
           "the owner merges; its own package is done, as the partner had merged it");
    save(owner, ownerPath, "kraken", 5000);
    ownerSession.syncClaims(owner, {}, 5000, [&](std::optional<team::Claims> answer, QString) {
        claims = answer;
    });
    expect(claims && !team::partnerClaim(*claims, document::ComponentKind::Frame,
                                         owner.frameMeta(2).id, "enthusiast"),
           "the owner's claim is released once the change is common");
}

// The background sync: claims written and read in its thread, an unsaved edit claimed early,
// the partner's package downloaded into the cache.
void testBackgroundSync()
{
    QTemporaryDir folder;
    const QString remotePath = QDir(folder.path()).filePath(QStringLiteral("remote"));
    const QString ownerPath = QDir(folder.path()).filePath(QStringLiteral("kraken.cdmd"));
    const QString cachePath = QDir(folder.path()).filePath(QStringLiteral("cache"));
    app::FolderRemote remote(remotePath);
    ProjectDocument owner = ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    save(owner, ownerPath, "kraken", 1000);
    owner.setTeam(team::startTeam(owner, "folder", "kraken", {"enthusiast"}));
    save(owner, ownerPath, "kraken", 1000);
    const ProjectDocument partner = owner;
    owner.setFrameDurationMs(2, 222);
    save(owner, ownerPath, "kraken", 2000);
    app::TeamSession ownerSession(remote, QStringLiteral("kraken"));
    ownerSession.share(owner, 2000, QStringLiteral("Attract mode"), [](QString, bool) {});

    QObject context;
    std::vector<app::TeamSync::Result> results;
    app::TeamSync sync(
        &context, [&results](app::TeamSync::Result result) { results.push_back(result); },
        std::chrono::hours(1),
        [remotePath](const app::TeamSync::Input&, app::HttpTransport&) {
            return std::make_unique<app::FolderRemote>(remotePath);
        });
    app::TeamSync::Input input;
    input.active = true;
    input.login = QStringLiteral("enthusiast");
    input.repository = QStringLiteral("folder");
    // An edit of frame 5 not saved yet: claimed early.
    input.claimed.changed[0].emplace(partner.frameMeta(5).id, document::ComponentId{});
    input.mergedPackages = partner.team()->mergedPackages;
    input.cacheFolder = cachePath;
    const uint64_t number = sync.setInput(input);
    QElapsedTimer timer;
    timer.start();
    while (results.empty() && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    expect(results.size() == 1 && results[0].input == number && results[0].claims,
           "the sync answers on this thread: " +
               (results.empty() ? std::string("nothing") : results[0].error.toStdString()));
    const team::Claims& claims = *results[0].claims;
    expect(team::partnerClaim(claims, document::ComponentKind::Frame, partner.frameMeta(5).id,
                              "kraken") &&
               team::partnerClaim(claims, document::ComponentKind::Frame,
                                  partner.frameMeta(2).id, "enthusiast"),
           "the partner's unsaved edit is claimed, and the owner's claim is read");
    expect(results[0].pendingPackages.size() == 1 &&
               app::TeamSync::cachedPackage(cachePath, results[0].pendingPackages[0]),
           "the owner's package is waiting, downloaded into the cache");
    expect(results[0].notes.size() == 1 &&
               results[0].notes.begin()->second.comment == QStringLiteral("Attract mode") &&
               !results[0].notes.begin()->second.asFile,
           "with the owner's comment");
    QString error;
    expect(app::changePackageFrom(
               *app::TeamSync::cachedPackage(cachePath, results[0].pendingPackages[0]), error)
               .has_value(),
           "and readable from there: " + error.toStdString());
}

// Waits for the sync's next answer.
app::TeamSync::Result nextResult(app::TeamSync& sync,
                                 const app::TeamSync::Input& input,
                                 std::vector<app::TeamSync::Result>& results)
{
    results.clear();
    (void)sync.setInput(input);
    QElapsedTimer timer;
    timer.start();
    while (results.empty() && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    expect(!results.empty(), "the sync answers");
    return results.back();
}

void testShareAsFile()
{
    QTemporaryDir folder;
    const QString remotePath = QDir(folder.path()).filePath(QStringLiteral("remote"));
    const QString ownerPath = QDir(folder.path()).filePath(QStringLiteral("kraken.cdmd"));
    const QString cachePath = QDir(folder.path()).filePath(QStringLiteral("cache"));
    app::FolderRemote remote(remotePath);
    ProjectDocument owner = ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    save(owner, ownerPath, "kraken", 1000);
    owner.setTeam(team::startTeam(owner, "folder", "kraken", {"enthusiast"}));
    save(owner, ownerPath, "kraken", 1000);
    ProjectDocument partner = owner;
    owner.setFrameDurationMs(2, 222);
    save(owner, ownerPath, "kraken", 2000);

    // The owner shares as a file: the remote gets the notice only.
    app::TeamSession ownerSession(remote, QStringLiteral("kraken"));
    const app::TeamSession::PreparedShare prepared =
        ownerSession.prepareShare(owner, 2000, QStringLiteral("The new movie clips"));
    expect(prepared.package && !prepared.bytes.isEmpty(), "the package is made");
    QString error = QStringLiteral("unset");
    ownerSession.publish(owner, prepared, true, 2000, [&error](QString answer) {
        error = answer;
    });
    const QStringList onRemote =
        QDir(QDir(remotePath).filePath(QStringLiteral("packages"))).entryList(QDir::Files);
    expect(error.isEmpty() && owner.team()->sent.size() == 1 && onRemote.size() == 1 &&
               app::isPackageNotice(onRemote[0]),
           "only the notice is on the remote, and the package counts as sent");
    expect(app::packageFileName(onRemote[0]) == app::changePackageName(*prepared.package),
           "the notice names the package's file");

    // The partner's sync lists it, with the comment and the size, as not there yet.
    QObject context;
    std::vector<app::TeamSync::Result> results;
    app::TeamSync sync(
        &context, [&results](app::TeamSync::Result result) { results.push_back(result); },
        std::chrono::hours(1),
        [remotePath](const app::TeamSync::Input&, app::HttpTransport&) {
            return std::make_unique<app::FolderRemote>(remotePath);
        });
    app::TeamSync::Input input;
    input.active = true;
    input.login = QStringLiteral("enthusiast");
    input.repository = QStringLiteral("folder");
    input.mergedPackages = partner.team()->mergedPackages;
    input.cacheFolder = cachePath;
    app::TeamSync::Result result = nextResult(sync, input, results);
    expect(result.pendingPackages.size() == 1 && result.notes.count(onRemote[0]) == 1,
           "the partner sees the notice");
    app::PendingNote note = result.notes[onRemote[0]];
    expect(note.asFile && !note.available && note.comment == QStringLiteral("The new movie clips")
               && note.sizeBytes == prepared.bytes.size(),
           "a file not added yet, with its comment and size");

    // A file of another snapshot is refused; the right one goes into the cache.
    ProjectDocument stranger = partner;
    stranger.setTeam(team::startTeam(stranger, "other", "kraken", {"enthusiast"}));
    expect(!app::addPackageFileToCache(QByteArray("no zip"), partner, cachePath).isEmpty() &&
               !app::addPackageFileToCache(prepared.bytes, stranger, cachePath).isEmpty(),
           "no package, or another snapshot's, is refused");
    QString added;
    expect(app::addPackageFileToCache(prepared.bytes, partner, cachePath, &added).isEmpty() &&
               added == app::packageFileName(onRemote[0]),
           "the file goes into the cache under the package's name");
    result = nextResult(sync, input, results);
    expect(result.notes[onRemote[0]].available, "now the partner has it");

    // Merged from the cache, as a package that came through the remote.
    error.clear();
    const std::optional<team::ChangePackage> package = app::changePackageFrom(
        *app::TeamSync::cachedPackage(cachePath, app::packageFileName(onRemote[0])), error);
    expect(package.has_value(), "the added file reads: " + error.toStdString());
    (void)team::mergeChangePackage(partner, *package, {}, "enthusiast");
    expect(partner.frameDurationMs(2) == 222, "and merges");
    expect(!app::addPackageFileToCache(prepared.bytes, partner, cachePath).isEmpty(),
           "a merged package is not added again");
}

const QTreeWidgetItem* lineStartingWith(const app::SharedChangesView& view, const QString& start)
{
    for (int i = 0; i < view.topLevelItemCount(); ++i) {
        const QTreeWidgetItem* head = view.topLevelItem(i);
        for (int c = 0; c < head->childCount(); ++c) {
            if (head->child(c)->text(0).startsWith(start)) {
                return head->child(c);
            }
        }
    }
    return nullptr;
}

// The history: the owner's own share recorded, the partner's pending package read from the
// cache with its comment and changes, the merge recorded, and a merge of an older editor found
// in the cache.
void testHistory()
{
    expect(app::historyNumbersText({}) == QString() &&
               app::historyNumbersText({4}) == QStringLiteral("4") &&
               app::historyNumbersText({2, 3, 4, 7, 9, 10}) == QStringLiteral("2-4, 7, 9-10") &&
               app::historyNumbersText({1, 3, 5, 7}, 2) == QStringLiteral("1, 3 and 2 more"),
           "numbers with runs shortened, and a long list cut");

    QTemporaryDir folder;
    const QString remotePath = QDir(folder.path()).filePath(QStringLiteral("remote"));
    const QString ownerPath = QDir(folder.path()).filePath(QStringLiteral("kraken.cdmd"));
    const QString cachePath = QDir(folder.path()).filePath(QStringLiteral("cache"));
    app::FolderRemote remote(remotePath);
    ProjectDocument owner = ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    save(owner, ownerPath, "kraken", 1000);
    owner.setTeam(team::startTeam(owner, "folder", "kraken", {"enthusiast"}));
    save(owner, ownerPath, "kraken", 1000);
    ProjectDocument partner = owner;
    owner.setFrameDurationMs(2, 222);
    owner.setFrameDurationMs(3, 333);
    owner.setComparisonMaskPixel(1, 3, 3, 1);
    save(owner, ownerPath, "kraken", 2000);
    app::TeamSession ownerSession(remote, QStringLiteral("kraken"));
    QString error = QStringLiteral("unset");
    ownerSession.share(owner, 2000, QStringLiteral("Fixed the 8-digit score"),
                       [&error](QString answer, bool) { error = answer; });
    expect(error.isEmpty() && owner.team()->history.size() == 1 &&
               owner.team()->history[0].comment == "Fixed the 8-digit score" &&
               owner.team()->history[0].author == "kraken",
           "the owner's share is in the owner's history: " + error.toStdString());
    const std::vector<app::HistoryItem> own = app::teamHistoryItems(owner, cachePath, {}, {});
    expect(own.size() == 1 && !own[0].pending && own[0].entry &&
               own[0].comment == QStringLiteral("Fixed the 8-digit score"),
           "and in what the owner's history shows");

    // The partner's sync downloads it; the history and the merge show what it changes.
    QObject context;
    std::vector<app::TeamSync::Result> results;
    app::TeamSync sync(
        &context, [&results](app::TeamSync::Result result) { results.push_back(result); },
        std::chrono::hours(1),
        [remotePath](const app::TeamSync::Input&, app::HttpTransport&) {
            return std::make_unique<app::FolderRemote>(remotePath);
        });
    app::TeamSync::Input input;
    input.active = true;
    input.login = QStringLiteral("enthusiast");
    input.repository = QStringLiteral("folder");
    input.mergedPackages = partner.team()->mergedPackages;
    input.cacheFolder = cachePath;
    const app::TeamSync::Result result = nextResult(sync, input, results);
    expect(result.pendingPackages.size() == 1, "the partner has the package waiting");
    const std::vector<app::HistoryItem> waiting =
        app::teamHistoryItems(partner, cachePath, result.pendingPackages, result.notes);
    expect(waiting.size() == 1 && waiting[0].pending && waiting[0].entry &&
               waiting[0].author == QStringLiteral("kraken") && waiting[0].createdMs == 2000 &&
               waiting[0].comment == QStringLiteral("Fixed the 8-digit score"),
           "the pending package with its author, time, comment and changes");
    const std::vector<app::HistoryItem> unknown = app::teamHistoryItems(
        partner, QDir(folder.path()).filePath(QStringLiteral("none")), result.pendingPackages,
        result.notes);
    expect(unknown.size() == 1 && unknown[0].pending && !unknown[0].entry &&
               unknown[0].comment == QStringLiteral("Fixed the 8-digit score"),
           "not downloaded: the comment from the note, no changes");

    app::SharedChangesView view;
    view.setItems(waiting, partner);
    QStringList texts = view.texts();
    expect(texts.size() == 4 && texts[0].startsWith(QStringLiteral("kraken, ")) &&
               texts[0].endsWith(QStringLiteral(": waiting to be merged")) &&
               texts[1] == QStringLiteral("  Fixed the 8-digit score") &&
               texts[2] == QStringLiteral("  Frames changed: 2-3") &&
               texts[3] == QStringLiteral("  Comparison masks changed: 1"),
           "who, when, the comment and what changed: " +
               texts.join(QLatin1Char('|')).toStdString());
    std::vector<std::size_t> shown;
    view.onShowFrames = [&shown](const std::vector<std::size_t>& frames) { shown = frames; };
    const QTreeWidgetItem* frames = lineStartingWith(view, QStringLiteral("Frames changed"));
    const QTreeWidgetItem* masks = lineStartingWith(view, QStringLiteral("Comparison masks"));
    expect(frames && masks && view.framesOf(masks).empty(), "only frame lines select frames");
    Q_EMIT view.itemDoubleClicked(const_cast<QTreeWidgetItem*>(frames), 0);
    expect(shown == std::vector<std::size_t>{2, 3}, "double-clicking selects the frames");

    std::optional<team::ChangePackage> package;
    error.clear();
    package = app::changePackageFrom(
        *app::TeamSync::cachedPackage(cachePath, result.pendingPackages[0]), error);
    expect(package.has_value(), "the package reads: " + error.toStdString());
    app::MergePreviewDialog preview({app::pendingHistoryItem(*package)}, partner);
    expect(preview.view()->texts().size() == 4 &&
               preview.view()->texts()[1] == QStringLiteral("  Fixed the 8-digit score"),
           "the merge shows the comment before merging");

    ProjectDocument older = partner;
    (void)team::mergeChangePackage(partner, *package, {}, "enthusiast");
    const std::vector<app::HistoryItem> merged = app::teamHistoryItems(partner, cachePath, {}, {});
    expect(merged.size() == 1 && !merged[0].pending && merged[0].entry &&
               merged[0].entry->packageId == package->packageId,
           "merged: in the history, no longer waiting");
    view.setItems(merged, partner);
    texts = view.texts();
    expect(texts.size() == 4 && !texts[0].contains(QStringLiteral("waiting")),
           "the merged change without the note");

    // A merge of an editor that kept no history: found in the cache.
    (void)team::mergeChangePackage(older, *package, {}, "enthusiast");
    document::TeamState state = *older.team();
    state.history.clear();
    older.setTeam(state);
    const std::vector<app::HistoryItem> found = app::teamHistoryItems(older, cachePath, {}, {});
    expect(found.size() == 1 && !found[0].pending && found[0].entry &&
               found[0].comment == QStringLiteral("Fixed the 8-digit score"),
           "an older merge comes from the cache");
    expect(app::teamHistoryItems(older, QString(), {}, {}).empty(),
           "without the cache it is not known");
    app::TeamHistoryDialog dialog;
    dialog.setItems({}, older);
    expect(dialog.view()->texts().isEmpty(), "an empty history");
}

}  // namespace

void runTeamSessionWidgetTests()
{
    testPackageNames();
    testShareAndMerge();
    testBackgroundSync();
    testShareAsFile();
    testHistory();
}
