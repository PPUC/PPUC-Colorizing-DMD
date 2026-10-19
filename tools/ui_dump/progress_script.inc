// Open and save on the worker thread with the progress dialog, title prefix "progress: ".
// Included at the very end of main() in dump.cpp, for a tree that has
// app/dialogs/ProjectTaskDialog.h. A quick save shows no dialog; for the others the dialog
// appears at once (delay 0) and the worker is held until it is up, so what is printed does not
// depend on timing. After each dump it prints the status bar, the project and the files.
{
    const QString workPath = QDir::temp().filePath(QStringLiteral("colorizingdmd_ui_dump_progress"));
    QDir(workPath).removeRecursively();
    QDir().mkpath(workPath);
    const QDir work(workPath);
    const QString cromPath = work.filePath(QStringLiteral("progress.cROM"));
    const QString crpPath = work.filePath(QStringLiteral("progress.cRPX"));

    // What the dialog hook saw; printed after the dump of the state it led to.
    std::string dialogLog;
    const auto D = [&](const char* title) {
        dump(title, w);
        std::cout << dialogLog;
        dialogLog.clear();
        std::cout << "progress: status '" << w.statusBar()->currentMessage().toStdString()
                  << "' | project "
                  << (w.m_document ? w.m_document->romName() + " with " +
                                         std::to_string(w.m_document->frameCount()) +
                                         " frames, first lasts " +
                                         std::to_string(w.m_document->frameDurationMs(0)) + " ms" +
                                         (w.m_document->isDirty() ? ", changed" : ", saved")
                                   : std::string("none"))
                  << " | files";
        for (const QFileInfo& file : work.entryInfoList(QDir::Files, QDir::Name)) {
            std::cout << ' ' << file.fileName().toStdString() << ':' << file.size();
        }
        std::cout << '\n';
    };

    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 4;
    snapshot.romName = "progress";
    for (int i = 0; i < 3; ++i) {
        document::IndexedFrame original = document::makeIndexedFrame(render::kSdFrameSize, 0);
        original.pixels[static_cast<std::size_t>(i)] = 3;
        snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
            snapshot.geometry, snapshot.colorCount, std::move(original), 100));
    }
    w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
    w.m_currentCromPath = cromPath;
    w.m_currentCrpPath = crpPath;
    w.m_undoHistories.clear();
    w.refreshProjectSummary();
    w.m_document->setFrameDurationMs(0, 60);
    w.refreshProjectSummary();

    std::atomic<bool> open{false};
    bool cancel = false;
    int dialogs = 0;
    app::setProjectTaskDialogShownHook([&](app::ProjectTaskDialog& dialog) {
        ++dialogs;
        dialogLog += "progress: dialog '" + dialog.windowTitle().toStdString() + "' | " +
                     dialog.statusText().toStdString() + " | modal " +
                     std::to_string(dialog.isModal()) + " | cancel " +
                     std::to_string(dialog.isCancelEnabled()) + '\n';
        if (cancel) {
            dialog.requestCancel();
            dialogLog += "progress: dialog after Cancel | " + dialog.statusText().toStdString() +
                         " | cancel " + std::to_string(dialog.isCancelEnabled()) + '\n';
        }
        open = true;
    });

    bool saved = w.saveProject();
    D("progress: saved without a dialog");
    std::cout << "progress: quick save returns " << saved << " after " << dialogs
              << " dialogs\n";

    app::setProjectTaskDialogDelayMs(0);
    app::setProjectTaskWorkerStartHook([&open]() {
        while (!open.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    w.m_document->setFrameDurationMs(0, 70);
    w.refreshProjectSummary();
    open = false;
    cancel = true;
    saved = w.saveProject();
    D("progress: save cancelled");
    std::cout << "progress: cancelled save returns " << saved << '\n';
    open = false;
    cancel = false;
    saved = w.saveProject();
    D("progress: saved through the dialog");
    std::cout << "progress: save returns " << saved << '\n';

    w.m_document->setFrameDurationMs(0, 80);
    w.refreshProjectSummary();
    w.m_document->markClean();
    open = false;
    cancel = true;
    w.loadProject(cromPath, crpPath);
    D("progress: open cancelled, the project stays");
    open = false;
    cancel = false;
    w.loadProject(cromPath, crpPath);
    D("progress: opened through the dialog");
    std::cout << "progress: " << dialogs << " dialogs shown\n";

    app::setProjectTaskDialogDelayMs(300);
    app::setProjectTaskWorkerStartHook({});
    app::setProjectTaskDialogShownHook({});
    QDir(workPath).removeRecursively();
}
