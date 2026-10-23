// Autosave and its recovery when the project is opened again, title prefix "autosave: ".
// Included at the very end of main() in dump.cpp, for a tree that has app/Autosave.h.
{
    const QString workPath = QDir::temp().filePath(QStringLiteral("colorizingdmd_ui_dump_autosave"));
    QDir(workPath).removeRecursively();
    QDir().mkpath(workPath);
    const QDir work(workPath);
    const QString projectPath = work.filePath(QStringLiteral("auto.cdmd"));

    const auto D = [&](const char* title) {
        dump(title, w);
        std::cout << "autosave: status '" << w.statusBar()->currentMessage().toStdString()
                  << "' | project "
                  << (w.m_document ? w.m_document->romName() + ", first frame lasts " +
                                         std::to_string(w.m_document->frameDurationMs(0)) + " ms" +
                                         (w.m_document->isDirty() ? ", changed" : ", saved")
                                   : std::string("none"))
                  << " | save to " << QFileInfo(w.m_projectPath).fileName().toStdString()
                  << " | files";
        for (const QFileInfo& file : work.entryInfoList(QDir::Files, QDir::Name)) {
            std::cout << ' ' << file.fileName().toStdString();
        }
        std::cout << '\n';
    };
    const auto waitForAutosave = [&]() {
        QElapsedTimer timer;
        timer.start();
        while (w.m_autosaver->isRunning() && timer.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    };
    const auto answerNext = [](const QString& buttonText) {
        QTimer::singleShot(0, [buttonText]() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) { std::cerr << "no message box\n"; std::exit(3); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | "
                      << box->text().toStdString().substr(0, 40) << " | buttons:";
            QAbstractButton* chosen = nullptr;
            for (QAbstractButton* button : box->buttons()) {
                std::cout << ' ' << button->text().toStdString();
                if (button->text() == buttonText) chosen = button;
            }
            std::cout << '\n';
            if (!chosen) { std::cerr << "no button " << buttonText.toStdString() << '\n'; std::exit(3); }
            chosen->click();
        });
    };

    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 4;
    snapshot.romName = "auto";
    for (int i = 0; i < 2; ++i) {
        snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
            snapshot.geometry, snapshot.colorCount,
            document::makeIndexedFrame(render::kSdFrameSize, 0), 100));
    }
    w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
    w.m_projectPath = projectPath;
    w.m_sourcePath.clear();
    w.m_undoHistories.clear();
    w.refreshProjectSummary();
    w.saveProject();
    D("autosave: project saved");
    std::cout << "autosave: nothing to write while saved: " << !w.m_autosaver->runNow() << '\n';

    w.m_document->setFrameDurationMs(0, 60);
    w.refreshProjectSummary();
    std::cout << "autosave: a run starts: " << w.m_autosaver->runNow() << '\n';
    waitForAutosave();
    // The time of day would differ from dump to dump.
    QString autosaved = w.statusBar()->currentMessage();
    autosaved.replace(QRegularExpression(QStringLiteral(" at [^ ]+$")), QStringLiteral(" at <time>"));
    w.statusBar()->showMessage(autosaved);
    D("autosave: written beside the project");
    std::cout << "autosave: unchanged since, nothing to write: " << !w.m_autosaver->runNow()
              << '\n';

    // Make the project files older than the autosave, as after an hour of work.
    const QDateTime earlier = QDateTime::currentDateTime().addSecs(-3600);
    {
        QFile file(projectPath);
        if (!file.open(QIODevice::ReadWrite) ||
            !file.setFileTime(earlier, QFileDevice::FileModificationTime)) {
            std::cerr << "cannot set the time of " << projectPath.toStdString() << '\n';
            return 3;
        }
    }
    w.m_document->markClean();
    answerNext(QStringLiteral("Open Project"));
    w.loadProject(projectPath);
    D("autosave: offered, project opened");
    answerNext(QStringLiteral("Open Autosave"));
    w.loadProject(projectPath);
    D("autosave: offered, autosave opened as the project");

    w.m_unsavedChangesAnswer = [](const QString&) { return app::UnsavedChangesAnswer::Discard; };
    std::cout << "autosave: may close after discarding: "
              << w.confirmClosingProject(QStringLiteral("Open Project")) << '\n';
    w.m_unsavedChangesAnswer = {};
    // The offer must not come again; a box that shows up anyway ends the dump.
    QTimer::singleShot(3000, []() {
        if (QApplication::activeModalWidget()) { std::cerr << "autosave offered again\n"; std::exit(4); }
    });
    w.loadProject(projectPath);
    D("autosave: changes discarded, not offered again");

    QSettings().remove(QStringLiteral("autosave"));
    QDir(workPath).removeRecursively();
}
