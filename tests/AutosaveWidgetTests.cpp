// Autosave: the file names, which autosave is offered, and the writer on its worker thread
// (a project file written from a snapshot while the document goes on, cancel).

#include <chrono>
#include <functional>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "Autosave.h"
#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "legacy/LegacyProjectFileReader.h"
#include "project/ProjectFile.h"

namespace {

using namespace colorizing;
using app::Autosaver;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: autosave: " << message << '\n';
        std::exit(1);
    }
}

void touch(const QString& path, const QDateTime& time)
{
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create " + path.toStdString());
    file.write("x");
    file.close();
    expect(file.open(QIODevice::ReadWrite) &&
               file.setFileTime(time, QFileDevice::FileModificationTime),
           "set the time of " + path.toStdString());
}

void testNames()
{
    expect(app::autosavePathFor(QStringLiteral("/p/afm_113b.cdmd")) ==
               QStringLiteral("/p/afm_113b(auto).cdmd"),
           "the Win32 editor's name beside the project, as a project file");
    expect(app::isAutosavePath(QStringLiteral("/p/afm_113b(auto).cdmd")) &&
               !app::isAutosavePath(QStringLiteral("/p/afm_113b.cdmd")),
           "an autosave is recognised by its name");
    const QString untitled = app::untitledAutosavePath("smb3");
    expect(QFileInfo(untitled).fileName() == QStringLiteral("smb3(auto).cdmd") &&
               QFileInfo(untitled).absolutePath() ==
                   QDir(app::untitledAutosaveDirectory()).absolutePath(),
           "a project without a project file has its autosave in the application's folder");
}

void testWhichAutosaveIsOffered()
{
    QTemporaryDir directory;
    const QString project = directory.filePath(QStringLiteral("rom.cdmd"));
    const QString autosave = app::autosavePathFor(project);
    const QDateTime base = QDateTime::currentDateTime().addSecs(-3600);
    touch(project, base);
    expect(!app::newerAutosaveTime(project, autosave), "no autosave, nothing offered");
    touch(autosave, base.addSecs(60));
    const std::optional<QDateTime> offered = app::newerAutosaveTime(project, autosave);
    expect(offered && offered->secsTo(base.addSecs(60)) == 0,
           "a newer autosave is offered with its time");
    touch(project, base.addSecs(600));
    expect(!app::newerAutosaveTime(project, autosave),
           "a project saved after the autosave is newer");
}

void testUntitledAutosaves()
{
    QDir(app::untitledAutosaveDirectory()).removeRecursively();
    expect(app::untitledAutosaves().empty(), "none at first");
    QDir().mkpath(app::untitledAutosaveDirectory());
    const QDateTime base = QDateTime::currentDateTime().addSecs(-3600);
    const QString older = app::untitledAutosavePath("older");
    const QString newer = app::untitledAutosavePath("newer");
    touch(older, base);
    touch(newer, base.addSecs(60));
    // An autosave of an earlier version is not offered.
    touch(QDir(app::untitledAutosaveDirectory()).filePath(QStringLiteral("old(auto).cRZ")),
          base.addSecs(120));
    const std::vector<QString> found = app::untitledAutosaves();
    expect(found.size() == 2 && found[0] == newer && found[1] == older,
           "the project files, newest first");
    QDir(app::untitledAutosaveDirectory()).removeRecursively();
}

bool waitFor(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return condition();
}

void testWriter()
{
    QTemporaryDir directory;
    const QString path = app::autosavePathFor(directory.filePath(QStringLiteral("sub/rom.cdmd")));
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3));
    document.setFrameDurationMs(0, 77);

    int jobs = 0;
    int finished = 0;
    uint64_t finishedRevision = 0;
    legacy::LegacySaveResult lastResult;
    Autosaver autosaver(
        nullptr,
        std::chrono::hours(1),
        [&]() -> std::optional<Autosaver::Job> {
            ++jobs;
            return Autosaver::Job{path, {}, document.toSnapshot(), document.revision()};
        },
        [&](const QString&, uint64_t revision, const legacy::LegacySaveResult& result) {
            ++finished;
            finishedRevision = revision;
            lastResult = result;
        });
    const uint64_t revision = document.revision();
    expect(autosaver.runNow(), "a run starts");
    expect(!autosaver.runNow(), "not a second one while it runs");
    // The document goes on while the file is written; it shows the snapshot.
    document.setFrameDurationMs(0, 99);
    expect(waitFor([&]() { return finished == 1; }), "the run finishes");
    expect(lastResult.ok && finishedRevision == revision,
           "and reports the snapshot's revision: " + lastResult.error.message);
    expect(!autosaver.isRunning(), "nothing runs afterwards");
    expect(QFileInfo::exists(path), "the autosave is a project file");

    const legacy::LegacyDocumentLoadResult loaded =
        project::loadProjectDocument(path.toStdString());
    expect(loaded.ok && loaded.document && loaded.document->frameCount() == 3 &&
               loaded.document->frameDurationMs(0) == 77 &&
               loaded.document->frameMeta(1).id == document.frameMeta(1).id,
           "the autosave opens as the project was when it started: " + loaded.error.message);
    expect(loaded.document->frameMeta(1).revision.isNull(),
           "an autosave gives no revisions: only a save does");

    // Cancelled: no report, and the earlier autosave is left as it was.
    const QByteArray before = [&]() {
        QFile file(path);
        expect(file.open(QIODevice::ReadOnly), "the autosave is there");
        return file.readAll();
    }();
    expect(autosaver.runNow(), "a second run starts");
    autosaver.cancelAndWait();
    QCoreApplication::processEvents();
    expect(finished == 1 && !autosaver.isRunning(), "a cancelled run reports nothing");
    QFile file(path);
    expect(file.open(QIODevice::ReadOnly), "the earlier autosave is still there");
    const QByteArray after = file.readAll();
    // The cancel may come after the write was done; then it holds the second snapshot.
    expect(after == before || after.size() == before.size(), "an intact autosave either way");
    expect(jobs == 2, "the source was asked once per run");
}

}  // namespace

void runAutosaveWidgetTests()
{
    QStandardPaths::setTestModeEnabled(true);
    testNames();
    testWhichAutosaveIsOffered();
    testUntitledAutosaves();
    testWriter();
    QStandardPaths::setTestModeEnabled(false);
}
