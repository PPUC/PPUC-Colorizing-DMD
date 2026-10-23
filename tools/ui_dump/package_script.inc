// Packages, title prefix "package: ". Two frames of a project go into a package, which is
// imported into the project itself (every frame known: Update Them, then undone), and into
// another project (added with their mask). Included at the very end of main() in dump.cpp,
// for a tree that has core/edit/PackageImport.h. After each dump it prints the status bar and
// the project's counts.
{
    const QString workPath = QDir::temp().filePath(QStringLiteral("colorizingdmd_ui_dump_package"));
    QDir(workPath).removeRecursively();
    QDir().mkpath(workPath);
    const QString packagePath = QDir(workPath).filePath(QStringLiteral("two.cdmdpkg"));

    const auto D = [&](const char* title) {
        dump(title, w);
        std::cout << "package: status '" << w.statusBar()->currentMessage().toStdString()
                  << "' | project "
                  << (w.m_document ? std::to_string(w.m_document->frameCount()) + " frames, " +
                                         std::to_string(w.m_document->comparisonMaskCount()) +
                                         " masks, frame 1 lasts " +
                                         std::to_string(w.m_document->frameDurationMs(1)) + " ms" +
                                         (w.m_document->isDirty() ? ", changed" : ", saved")
                                   : std::string("none"))
                  << '\n';
    };
    const auto answerNext = [](const QString& buttonText) {
        QTimer::singleShot(0, [buttonText]() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) { std::cerr << "no message box\n"; std::exit(3); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | "
                      << box->text().toStdString() << " | "
                      << box->informativeText().toStdString() << " | buttons:";
            QAbstractButton* chosen = nullptr;
            for (QAbstractButton* button : box->buttons()) {
                std::cout << ' ' << button->text().toStdString();
                if (button->text() == buttonText) chosen = button;
            }
            std::cout << '\n';
            if (!chosen) {
                std::cerr << "no button " << buttonText.toStdString() << '\n';
                std::exit(3);
            }
            chosen->click();
        });
    };

    const auto makeProject = [](const std::string& rom, bool withMask) {
        document::ProjectSnapshot snapshot;
        snapshot.colorCount = 4;
        snapshot.romName = rom;
        for (int i = 0; i < 3; ++i) {
            document::IndexedFrame original = document::makeIndexedFrame(render::kSdFrameSize, 0);
            original.pixels[static_cast<std::size_t>(i)] = 3;
            snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
                snapshot.geometry, snapshot.colorCount, std::move(original), 100));
        }
        if (withMask) {
            snapshot.comparisonMasks.push_back(document::makeIndexedFrame(render::kSdFrameSize, 0));
            snapshot.comparisonMasks[0].pixels[200] = 1;
            snapshot.frames[1].comparisonMaskId = 0;
        }
        snapshot.license.kind = document::LicenseKind::CcBy;
        snapshot.license.attribution = "Kraken";
        return document::ProjectDocument::fromSnapshot(std::move(snapshot));
    };
    const auto open = [&](document::ProjectDocument document) {
        w.m_document = std::move(document);
        w.m_projectPath = QDir(workPath).filePath(QStringLiteral("p.cdmd"));
        w.m_sourcePath.clear();
        w.m_undoHistories.clear();
        w.refreshProjectSummary();
    };

    document::ProjectDocument source = makeProject("source", true);
    source.setFrameDurationMs(1, 250);
    project::ProjectFileOptions options;
    options.package = true;
    if (!project::saveProjectFile(packagePath.toStdString(), edit::makePackage(source, {1, 2}),
                                  options)
             .ok) {
        std::cerr << "package: could not write the package\n";
        std::exit(3);
    }

    open(makeProject("source", true));
    answerNext(QStringLiteral("Update Them"));
    w.importPackageFrom(packagePath);
    D("package: imported into a project that has its frames, updated");
    w.m_undoAction->trigger();
    D("package: import undone");

    open(makeProject("other", false));
    w.m_document->setFrameDurationMs(0, 40);
    answerNext(QStringLiteral("Add Them As New"));
    w.importPackageFrom(packagePath);
    D("package: imported into another project, added");

    QDir(workPath).removeRecursively();
}
