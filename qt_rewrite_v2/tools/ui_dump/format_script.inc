// The editor file of this editor, title prefix "format: ". A project of the Win32 editor (.cROM
// and .cRP) is opened, saved (as .cROM and .cRPX, the .cRP renamed to .cRP.bak) and opened
// again from the recent list. Included at the very end of main() in dump.cpp, for a tree that
// has core/legacy/CrpxFile.h. After each dump it prints the status bar, where the project is
// saved to and the files.
{
    const QString workPath = QDir::temp().filePath(QStringLiteral("colorizingdmd_ui_dump_format"));
    QDir(workPath).removeRecursively();
    QDir().mkpath(workPath);
    const QDir work(workPath);
    const QString cromPath = work.filePath(QStringLiteral("win32.cROM"));
    const QString crpPath = work.filePath(QStringLiteral("win32.cRP"));

    const auto D = [&](const char* title) {
        dump(title, w);
        std::cout << "format: status '" << w.statusBar()->currentMessage().toStdString()
                  << "' | project "
                  << (w.m_document ? w.m_document->romName() + ", first frame lasts " +
                                         std::to_string(w.m_document->frameDurationMs(0)) + " ms" +
                                         (w.m_document->isDirty() ? ", changed" : ", saved")
                                   : std::string("none"))
                  << " | save to " << QFileInfo(w.m_currentCromPath).fileName().toStdString()
                  << " + " << QFileInfo(w.m_currentCrpPath).fileName().toStdString()
                  << " | files";
        for (const QFileInfo& file : work.entryInfoList(QDir::Files, QDir::Name)) {
            std::cout << ' ' << file.fileName().toStdString();
        }
        std::cout << '\n';
    };

    // A project as the Win32 editor saves it: the writer writes a .cRP when the name says so.
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 4;
    snapshot.romName = "win32";
    for (int i = 0; i < 2; ++i) {
        snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
            snapshot.geometry, snapshot.colorCount,
            document::makeIndexedFrame(render::kSdFrameSize, 0), 100));
    }
    if (!legacy::LegacyProjectFileWriter()
             .save({cromPath.toStdString(), crpPath.toStdString(), {}}, snapshot)
             .ok) {
        std::cerr << "format: could not write the Win32 project\n";
        std::exit(3);
    }

    w.loadProject(cromPath, crpPath);
    D("format: Win32 project opened");

    w.m_document->setFrameDurationMs(0, 60);
    w.refreshProjectSummary();
    w.saveProject();
    D("format: saved with a cRPX, the cRP renamed");
    QFile crpx(work.filePath(QStringLiteral("win32.cRPX")));
    std::cout << "format: the cRPX starts with '"
              << (crpx.open(QIODevice::ReadOnly) ? crpx.read(8).toStdString() : std::string())
              << "'\n";
    crpx.close();

    // The recent list still names the .cRP it was opened from.
    w.openRecentProject(cromPath + QChar('\t') + crpPath);
    D("format: opened again from the recent list");

    QDir(workPath).removeRecursively();
}
