// Autosave: the file names, which autosave is offered, and the writer on its worker thread
// (files written from a snapshot while the document goes on, cancel).

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

namespace {

using namespace colorizing;
using app::AutosaveFiles;
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
    const AutosaveFiles files = app::autosaveFilesFor(QStringLiteral("/p/afm_113b.cROM"),
                                                      QStringLiteral("/p/afm_113b.cRP"));
    expect(files.cromPath == QStringLiteral("/p/afm_113b(auto).cROM") &&
               files.crpPath == QStringLiteral("/p/afm_113b(auto).cRP"),
           "the Win32 editor's names beside the project");
    expect(app::isAutosavePath(files.cromPath) &&
               !app::isAutosavePath(QStringLiteral("/p/afm_113b.cROM")),
           "an autosave is recognised by its name");
    const AutosaveFiles untitled = app::untitledAutosaveFiles("smb3");
    expect(QFileInfo(untitled.cromPath).fileName() == QStringLiteral("smb3(auto).cROM") &&
               QFileInfo(untitled.cromPath).absolutePath() ==
                   QDir(app::untitledAutosaveDirectory()).absolutePath(),
           "a never saved project's autosave is in the application's folder");
}

void testWhichAutosaveIsOffered()
{
    QTemporaryDir directory;
    const QString crom = directory.filePath(QStringLiteral("rom.cROM"));
    const QString crp = directory.filePath(QStringLiteral("rom.cRP"));
    const AutosaveFiles autosave = app::autosaveFilesFor(crom, crp);
    const QDateTime base = QDateTime::currentDateTime().addSecs(-3600);
    touch(crom, base);
    touch(crp, base);
    expect(!app::newerAutosaveTime(crom, crp, autosave), "no autosave, nothing offered");
    touch(autosave.cromPath, base.addSecs(60));
    expect(!app::newerAutosaveTime(crom, crp, autosave), "only both files together");
    touch(autosave.crpPath, base.addSecs(120));
    const std::optional<QDateTime> offered = app::newerAutosaveTime(crom, crp, autosave);
    expect(offered && offered->secsTo(base.addSecs(60)) == 0,
           "a newer autosave is offered with the time of its older file");
    touch(crp, base.addSecs(600));
    expect(!app::newerAutosaveTime(crom, crp, autosave),
           "a project saved after the autosave is newer");
}

void testUntitledAutosaves()
{
    QDir(app::untitledAutosaveDirectory()).removeRecursively();
    expect(app::untitledAutosaves().empty(), "none at first");
    QDir().mkpath(app::untitledAutosaveDirectory());
    const QDateTime base = QDateTime::currentDateTime().addSecs(-3600);
    const AutosaveFiles older = app::untitledAutosaveFiles("older");
    const AutosaveFiles newer = app::untitledAutosaveFiles("newer");
    const AutosaveFiles half = app::untitledAutosaveFiles("half");
    touch(older.cromPath, base);
    touch(older.crpPath, base);
    touch(newer.cromPath, base.addSecs(60));
    touch(newer.crpPath, base.addSecs(60));
    touch(half.cromPath, base.addSecs(120));
    const std::vector<AutosaveFiles> found = app::untitledAutosaves();
    expect(found.size() == 2 && found[0].cromPath == newer.cromPath &&
               found[1].cromPath == older.cromPath,
           "complete ones, newest first");
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
    const AutosaveFiles files =
        app::autosaveFilesFor(directory.filePath(QStringLiteral("sub/rom.cROM")),
                              directory.filePath(QStringLiteral("sub/rom.cRP")));
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
            return Autosaver::Job{files, document.toSnapshot(), document.revision()};
        },
        [&](const AutosaveFiles&, uint64_t revision, const legacy::LegacySaveResult& result) {
            ++finished;
            finishedRevision = revision;
            lastResult = result;
        });
    const uint64_t revision = document.revision();
    expect(autosaver.runNow(), "a run starts");
    expect(!autosaver.runNow(), "not a second one while it runs");
    // The document goes on while the files are written; they show the snapshot.
    document.setFrameDurationMs(0, 99);
    expect(waitFor([&]() { return finished == 1; }), "the run finishes");
    expect(lastResult.ok && finishedRevision == revision, "and reports the snapshot's revision");
    expect(!autosaver.isRunning(), "nothing runs afterwards");

    const legacy::LegacyDocumentLoadResult loaded = legacy::loadLegacyProjectDocument(
        {files.cromPath.toStdString(), files.crpPath.toStdString(), {}}, nullptr);
    expect(loaded.ok && loaded.document && loaded.document->frameCount() == 3 &&
               loaded.document->frameDurationMs(0) == 77,
           "the autosave opens as the project was when it started: " + loaded.error.message);

    // Cancelled: no report, and the earlier autosave is left as it was.
    const QByteArray before = [&]() {
        QFile file(files.cromPath);
        expect(file.open(QIODevice::ReadOnly), "the autosave is there");
        return file.readAll();
    }();
    expect(autosaver.runNow(), "a second run starts");
    autosaver.cancelAndWait();
    QCoreApplication::processEvents();
    expect(finished == 1 && !autosaver.isRunning(), "a cancelled run reports nothing");
    QFile file(files.cromPath);
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
