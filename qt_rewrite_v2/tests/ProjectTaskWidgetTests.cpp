// Open and save on a worker thread (app::runProjectTask, ProjectTaskDialog) with the core load
// and save MainWindow runs there: the dialog only for slow tasks, cancel, failure, no partial
// files, and the document unchanged by a save.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <QApplication>

#include "RuntimeFixtures.h"
#include "dialogs/ProjectTaskDialog.h"
#include "document/ProjectDocument.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::filesystem::path uniqueTempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           platform::pathFromUtf8("colorizing_qt_v2_task_\xce\xa9_" + run + "_" +
                                  std::to_string(counter++) + suffix);
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string serializedCrom(const document::ProjectDocument& document)
{
    std::ostringstream stream;
    std::string error;
    expect(legacy::writeLegacyCrom(stream, document.toSnapshot(), error), "serialize: " + error);
    return stream.str();
}

// Holds the worker until the dialog is up, and lets the dialog's hook decide what happens.
struct Gate {
    std::atomic<bool> open{false};
    std::atomic<int> dialogsShown{0};
};

void installGate(Gate& gate, bool cancel, std::string* firstText = nullptr)
{
    app::setProjectTaskDialogDelayMs(0);
    app::setProjectTaskWorkerStartHook([&gate]() {
        while (!gate.open.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    app::setProjectTaskDialogShownHook([&gate, cancel, firstText](app::ProjectTaskDialog& dialog) {
        ++gate.dialogsShown;
        if (firstText) {
            *firstText = dialog.statusText().toStdString();
        }
        if (cancel) {
            dialog.requestCancel();
            expect(!dialog.isCancelEnabled(), "Cancel is disabled once pressed");
            expect(dialog.statusText().startsWith(QStringLiteral("Cancelling")),
                   "the dialog says it is cancelling");
        }
        gate.open = true;
    });
}

void removeHooks()
{
    app::setProjectTaskDialogDelayMs(300);
    app::setProjectTaskWorkerStartHook({});
    app::setProjectTaskDialogShownHook({});
}

void testQuickTaskShowsNoDialog()
{
    int shown = 0;
    app::setProjectTaskDialogShownHook([&shown](app::ProjectTaskDialog&) { ++shown; });
    legacy::LegacyIoProgress progress;
    bool ran = false;
    const std::optional<std::string> failure =
        app::runProjectTask(nullptr, QStringLiteral("Quick"), progress, [&ran]() { ran = true; });
    expect(ran && !failure, "the work ran");
    expect(shown == 0, "a task shorter than the delay shows no dialog");
    removeHooks();
}

void testExceptionBecomesTheMessage()
{
    legacy::LegacyIoProgress progress;
    const std::optional<std::string> failure =
        app::runProjectTask(nullptr, QStringLiteral("Failing"), progress,
                            []() { throw std::runtime_error("disk on fire"); });
    expect(failure && *failure == "disk on fire", "the exception's message is returned");
}

void testSaveCancelledKeepsFilesAndDocument()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    document.setFrameDurationMs(0, 77);
    const uint64_t revision = document.revision();
    const std::string before = serializedCrom(document);
    const auto cromPath = uniqueTempPath(".cROM");
    const auto crpPath = uniqueTempPath(".cRP");
    const auto crzPath = uniqueTempPath(".cRZ");
    writeFile(cromPath, "old crom");
    writeFile(crpPath, "old crp");
    const legacy::LegacyProjectPaths paths{platform::utf8FromPath(cromPath),
                                           platform::utf8FromPath(crpPath),
                                           platform::utf8FromPath(crzPath)};

    Gate gate;
    std::string firstText;
    installGate(gate, true, &firstText);
    legacy::LegacySaveResult result;
    {
        const document::ProjectSnapshot snapshot = document.toSnapshot();
        legacy::LegacyIoProgress progress;
        const std::optional<std::string> failure =
            app::runProjectTask(nullptr, QStringLiteral("Saving Project"), progress, [&]() {
                result = legacy::LegacyProjectFileWriter().save(paths, snapshot, &progress);
            });
        expect(!failure, "no exception");
    }
    removeHooks();
    expect(gate.dialogsShown == 1, "the dialog was shown");
    expect(firstText == "Starting…", "the dialog starts with the first stage");
    expect(!result.ok && result.cancelled, "the save was cancelled");
    expect(readFile(cromPath) == "old crom" && readFile(crpPath) == "old crp" &&
               !std::filesystem::exists(crzPath),
           "a cancelled save leaves the files as they were");
    for (const auto& path : {cromPath, crpPath, crzPath}) {
        expect(!std::filesystem::exists(platform::pathFromUtf8(platform::utf8FromPath(path) +
                                                               ".saving")),
               "no temporary file is left");
    }
    expect(document.revision() == revision && document.isDirty(),
           "the document is unchanged and still has unsaved changes");
    expect(serializedCrom(document) == before, "the document's content is unchanged");

    // The same save through the dialog, not cancelled, writes the project.
    Gate second;
    installGate(second, false);
    {
        const document::ProjectSnapshot snapshot = document.toSnapshot();
        legacy::LegacyIoProgress progress;
        (void)app::runProjectTask(nullptr, QStringLiteral("Saving Project"), progress, [&]() {
            result = legacy::LegacyProjectFileWriter().save(paths, snapshot, &progress);
        });
    }
    removeHooks();
    expect(result.ok, "the save through the dialog succeeds: " + result.error.message);
    expect(readFile(cromPath) == before, "it wrote the document");
    expect(std::filesystem::exists(crzPath), "and the archive");
    expect(document.revision() == revision, "saving does not change the document");

    // A save that fails (no such folder) leaves no file either.
    const auto missing = uniqueTempPath("_missing_folder");
    const legacy::LegacyProjectPaths badPaths{
        platform::utf8FromPath(missing / "game.cROM"), platform::utf8FromPath(missing / "game.cRP"),
        platform::utf8FromPath(missing / "game.cRZ")};
    {
        const document::ProjectSnapshot snapshot = document.toSnapshot();
        legacy::LegacyIoProgress progress;
        (void)app::runProjectTask(nullptr, QStringLiteral("Saving Project"), progress, [&]() {
            result = legacy::LegacyProjectFileWriter().save(badPaths, snapshot, &progress);
        });
    }
    expect(!result.ok && !result.cancelled, "a save into a missing folder fails");
    expect(!std::filesystem::exists(missing), "and writes nothing");

    for (const auto& path : {cromPath, crpPath, crzPath}) {
        std::filesystem::remove(path);
    }
}

void testOpenCancelledAndOpened()
{
    const document::ProjectDocument source = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::NativeExtra));
    const auto cromPath = uniqueTempPath(".cROM");
    const auto crpPath = uniqueTempPath(".cRP");
    const legacy::LegacyProjectPaths paths{platform::utf8FromPath(cromPath),
                                           platform::utf8FromPath(crpPath)};
    expect(legacy::LegacyProjectFileWriter().save(paths, source.toSnapshot()).ok, "fixture");

    for (const bool cancel : {true, false}) {
        Gate gate;
        installGate(gate, cancel);
        legacy::LegacyDocumentLoadResult result;
        legacy::LegacyIoProgress progress;
        (void)app::runProjectTask(nullptr, QStringLiteral("Opening Project"), progress, [&]() {
            result = legacy::loadLegacyProjectDocument(paths, &progress);
        });
        removeHooks();
        expect(gate.dialogsShown == 1, "the open showed its dialog");
        if (cancel) {
            expect(!result.ok && result.cancelled && !result.document,
                   "a cancelled open gives no project");
        } else {
            expect(result.ok && result.document, "the open succeeds: " + result.error.message);
            expect(serializedCrom(*result.document) == serializedCrom(source),
                   "the opened project is the saved one");
        }
    }
    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);
}

void testDialogTexts()
{
    legacy::LegacyIoProgress progress;
    app::ProjectTaskDialog dialog(QStringLiteral("Saving Project"), progress, []() { return false; });
    progress.setStage(legacy::LegacyIoStage::Writing);
    progress.total = 4 * 1024 * 1024;
    progress.done = 1024 * 1024;
    dialog.updateProgress();
    expect(dialog.statusText() == QStringLiteral("Writing the .cROM and .cRP: 1 of 4 MB"),
           "writing shows the bytes: " + dialog.statusText().toStdString());
    progress.setStage(legacy::LegacyIoStage::Compressing);
    dialog.updateProgress();
    expect(dialog.statusText().startsWith(QStringLiteral("Packing the .cRZ archive")),
           "compressing has its text");
    expect(dialog.isCancelEnabled(), "compression can be cancelled (after it ends)");
    dialog.reject();
    expect(progress.cancel.load() && dialog.statusText() ==
                                         QStringLiteral("Cancelling when this step ends…"),
           "closing the dialog asks to cancel");
    progress.cancel = false;
    progress.setStage(legacy::LegacyIoStage::Replacing);
    dialog.updateProgress();
    expect(!dialog.isCancelEnabled(), "replacing the files cannot be cancelled");
}

}  // namespace

void runProjectTaskWidgetTests()
{
    testQuickTaskShowsNoDialog();
    testExceptionBecomesTheMessage();
    testSaveCancelledKeepsFilesAndDocument();
    testOpenCancelledAndOpened();
    testDialogTexts();
}
