// Frame dumps and the same-frame tools: File -> New Project From Dump and Add Frames From
// Dump with their dialog, the first save of a new project, and the comparison side of the
// Mask page (same frames, Update Same List, deleting same frames, Spot Common, Common To
// Mask, the frames using a mask). Included at the very end of main() in dump.cpp, after
// structure_script.inc closed its project; all titles carry the prefix "dump import: ".
// Everything printed here comes after a dump of its own. After each dump the script prints
// what the dump does not hold: the window title, whether the project has files, the frames
// with their durations and masks, and the comparison masks.
//
// The dumps are generated into a temporary directory and removed at the end; only file
// names are printed.
{
    using render::Resolution;

    const QString workPath = QDir::temp().filePath(QStringLiteral("colorizingdmd_ui_dump_import"));
    QDir(workPath).removeRecursively();
    QDir().mkpath(workPath);
    const QDir work(workPath);

    // A frame of the size with shades below `shades`, different for every seed.
    const auto frameText = [](unsigned timecode, int seed, render::Size size, int shades) {
        static const char digits[] = "0123456789ABCDEF";
        std::string text = "0x";
        for (int shift = 28; shift >= 0; shift -= 4) text += digits[(timecode >> shift) & 0xf];
        text += "\r\n";
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                text += digits[(x * 7 + y * 3 + x / (seed + 2) + seed * 5) % shades];
            }
            text += "\r\n";
        }
        return text + "\r\n";
    };
    const auto writeDump = [&](const char* name, const std::string& text) {
        const QString path = work.filePath(QString::fromLatin1(name));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { std::cerr << "cannot write " << name << '\n'; std::exit(9); }
        file.write(text.data(), static_cast<qint64>(text.size()));
        return path;
    };
    const render::Size sd{128, 32};
    // Seeds 1 2 1 3 4: one repetition; seed 3 is shown for 5 ms; the last gap is too long.
    const QString firstDump = writeDump(
        "demo_rom.txt", frameText(1000, 1, sd, 4) + frameText(1040, 2, sd, 4) +
                            frameText(1100, 1, sd, 4) + frameText(1150, 3, sd, 4) +
                            frameText(1155, 4, sd, 4) + frameText(9000, 5, sd, 4));
    // Seed 2 again, seed 6 twice, seed 7, and the start of a frame the dump ends in.
    const QString secondDump = writeDump(
        "more.txt", frameText(50, 2, sd, 4) + frameText(90, 6, sd, 4) + frameText(150, 6, sd, 4) +
                        frameText(200, 7, sd, 4) + frameText(260, 8, sd, 4).substr(0, 700));
    const QString wideDump = writeDump("wide.txt", frameText(10, 1, render::Size{192, 64}, 16) +
                                                       frameText(60, 2, render::Size{192, 64}, 16));
    const QString bigDump = writeDump("big.txt", frameText(10, 1, render::Size{256, 64}, 4) +
                                                     frameText(60, 2, render::Size{256, 64}, 4));
    const QString brokenDump =
        writeDump("broken.txt", frameText(10, 1, sd, 4) + "0x00000020\r\nnot a row\r\n");

    const auto printState = [&]() {
        std::cout << "dump import: title='" << w.windowTitle().toStdString() << "' files="
                  << QFileInfo(w.m_currentCromPath).fileName().toStdString() << ','
                  << QFileInfo(w.m_currentCrpPath).fileName().toStdString() << " addAction="
                  << w.m_addFramesFromDumpAction->isEnabled() << " save="
                  << w.m_saveAction->isEnabled() << '\n';
        if (!w.m_document) {
            std::cout << "dump import: no document\n";
            return;
        }
        const document::ProjectDocument& d = *w.m_document;
        std::cout << "dump import: " << d.romName() << ' ' << d.frameCount() << " frames "
                  << d.colorCount() << " colors " << d.originalFrameSize().width << 'x'
                  << d.originalFrameSize().height << " dirty=" << d.isDirty() << " frames:";
        for (std::size_t f = 0; f < d.frameCount(); ++f) {
            const auto& original = d.originalFrame(f).pixels;
            std::cout << " f" << f << "[o=" << qHashBits(original.data(), original.size())
                      << " ms=" << d.frameDurationMs(f) << " mask=" << int(d.comparisonMaskId(f))
                      << " shape=" << int(d.shapeComparisonMode(f)) << " extra=" << d.hasHdFrame(f)
                      << ']';
        }
        std::cout << "\ndump import masks:";
        for (std::size_t m = 0; m < d.comparisonMaskCount(); ++m) {
            const auto& mask = d.comparisonMask(m).pixels;
            std::cout << " m" << m << "[set=" << std::count_if(mask.begin(), mask.end(), [](uint8_t v) { return v != 0; })
                      << " hash=" << qHashBits(mask.data(), mask.size()) << ']';
        }
        std::cout << "\ndump import selection:";
        for (std::size_t f : w.selectedFrameIndices()) std::cout << ' ' << f;
        std::cout << " overlay=" << w.m_preview->m_differenceOverlay.has_value() << '\n';
    };
    const auto D = [&](const char* title) {
        dump(title, w);
        printState();
    };
    const auto modalDialog = []() -> QDialog* {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (auto* candidate = qobject_cast<QDialog*>(top);
                    candidate && candidate->isVisible() && candidate->isModal()) {
                    dialog = candidate;
                }
            }
        }
        return dialog;
    };
    const auto answerMessageBox = [modalDialog](const QString& buttonText,
                                                const std::function<void()>& next = {}) {
        QTimer::singleShot(0, [buttonText, modalDialog, next]() {
            auto* box = qobject_cast<QMessageBox*>(modalDialog());
            if (!box) { std::cerr << "no message box\n"; std::exit(3); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | "
                      << box->text().toStdString() << " | "
                      << box->informativeText().toStdString() << '\n';
            for (QAbstractButton* button : box->buttons()) {
                if (button->text() == buttonText) {
                    if (next) next();
                    button->click();
                    return;
                }
            }
            std::cerr << "no button " << buttonText.toStdString() << '\n';
            std::exit(3);
        });
    };
    // What the import dialog shows once the dump is read; then `act` and accept or reject.
    const auto answerImportDialog = [modalDialog](bool accept,
                                                  const std::function<void(app::DumpImportDialog&)>& act = {}) {
        QTimer::singleShot(0, [accept, act, modalDialog]() {
            auto* dialog = dynamic_cast<app::DumpImportDialog*>(modalDialog());
            if (!dialog) { std::cerr << "no import dialog\n"; std::exit(3); }
            dialog->waitForAnalysis();
            if (act) {
                act(*dialog);
                dialog->waitForAnalysis();
            }
            QPushButton* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            std::cout << "import dialog: " << dialog->windowTitle().toStdString() << " | file "
                      << QFileInfo(dialog->dumpPath()).fileName().toStdString() << " | rom '"
                      << dialog->romName().toStdString() << "' | ok '" << ok->text().toStdString()
                      << "' en=" << ok->isEnabled() << " | problem '"
                      << dialog->acceptProblem().toStdString() << "'\n";
            for (const QLabel* label : dialog->findChildren<QLabel*>()) {
                if (label->isVisibleTo(dialog) && !label->text().isEmpty()) {
                    std::cout << "import dialog label: " << label->text().toStdString() << '\n';
                }
            }
            for (const QCheckBox* box : dialog->findChildren<QCheckBox*>()) {
                std::cout << "import dialog option: " << box->text().toStdString() << " vis="
                          << box->isVisibleTo(dialog) << " checked=" << box->isChecked() << '\n';
            }
            if (accept) ok->click(); else dialog->reject();
            if (dialog->isVisible()) { std::cerr << "import dialog still open\n"; std::exit(3); }
        });
    };
    // path: accept with that file and go on with `next`; empty: cancel.
    const auto answerFileDialog = [modalDialog](const QString& path,
                                                const std::function<void()>& next = {}) {
        QTimer::singleShot(0, [path, next, modalDialog]() {
            auto* dialog = qobject_cast<QFileDialog*>(modalDialog());
            if (!dialog) { std::cerr << "no file dialog\n"; std::exit(3); }
            std::cout << "file dialog: " << dialog->windowTitle().toStdString() << " | mode "
                      << int(dialog->acceptMode()) << " | filters "
                      << dialog->nameFilters().join(QStringLiteral(";;")).toStdString()
                      << " | default name "
                      << QFileInfo(dialog->selectedFiles().value(0)).fileName().toStdString() << '\n';
            if (path.isEmpty()) {
                dialog->reject();
                return;
            }
            dialog->selectFile(path);
            if (next) next();
            static_cast<QDialog*>(dialog)->accept();
            if (dialog->isVisible()) { std::cerr << "file dialog did not accept\n"; std::exit(3); }
        });
    };
    const auto guarded = [&](const std::function<void()>& call) {
        QTimer::singleShot(0, [modalDialog]() {
            if (QDialog* dialog = modalDialog()) {
                std::cout << "unexpected modal: " << dialog->windowTitle().toStdString() << '\n';
                dialog->reject();
            }
        });
        call();
        QApplication::processEvents();
    };
    const auto selectRows = [&](std::initializer_list<int> rows) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(*rows.begin());
        for (int r : rows) w.m_frameList->item(r)->setSelected(true);
    };
    UiDumpTabs* inspectorTabs = nullptr;
    for (UiDumpTabs* candidate : uiDumpChildren<UiDumpTabs>(w)) {
        for (int i = 0; i < candidate->count(); ++i) {
            if (candidate->tabText(i) == QLatin1String("Mask")) inspectorTabs = candidate;
        }
    }
    if (!inspectorTabs) { std::cerr << "no inspector tabs\n"; return 9; }
    const auto showTab = [&](const char* name) {
        for (int i = 0; i < inspectorTabs->count(); ++i) {
            if (inspectorTabs->tabText(i) == QLatin1String(name)) { inspectorTabs->setCurrentIndex(i); return; }
        }
        std::cerr << "no tab " << name << '\n';
        std::exit(9);
    };
    const auto clickTip = [&](const char* tip) { byTip<QToolButton>(w, tip)->click(); };
    const auto clickAnswered = [&](const char* tip, const QString& button) {
        answerMessageBox(button);
        clickTip(tip);
    };
    const char* const tipDeleteCurrent = "Delete the frames that are the same as the current frame";
    const char* const tipDeleteSelected = "Delete the frames that are the same as a selected frame";
    const char* const tipDeleteAll = "Delete every frame that is the same as an earlier frame";
    const char* const tipSpotCommon =
        "Mark the pixels that differ among the selected frames on the original frame";
    const char* const tipCommonToMask =
        "Make the comparison mask of the current frame cover exactly the pixels that differ "
        "among the selected frames";

    showTab("Frame");
    D("dump import: no project");

    // A new project: the file dialog, then the import dialog with its summary.
    answerFileDialog(QString());
    w.newProjectFromDump();
    D("dump import: new project, no file chosen");
    answerFileDialog(firstDump, [&]() { answerImportDialog(false); });
    w.newProjectFromDump();
    D("dump import: new project, import dialog cancelled");
    answerFileDialog(brokenDump, [&]() { answerImportDialog(false); });
    w.newProjectFromDump();
    D("dump import: new project, broken dump");
    answerFileDialog(firstDump, [&]() {
        answerImportDialog(true, [](app::DumpImportDialog& dialog) {
            // A ROM name of its own, and the frame that was shown for 5 ms left out.
            dialog.setRomName(QStringLiteral("demo"));
            importing::DumpImportOptions options;
            options.dropShortFrames = true;
            options.shortFrameLimitMs = 10;
            dialog.setOptions(options);
            dialog.startAnalysis();
        });
    });
    w.newProjectFromDump();
    D("dump import: new project from a dump");
    showTab("Mask");
    D("dump import: mask page of the new project");
    dumpFocusChain(w);

    // The first save asks where to; the next one does not.
    answerFileDialog(QString());
    std::cout << "dump import: save cancelled returns " << w.saveProject() << '\n';
    D("dump import: first save cancelled");
    answerFileDialog(work.filePath(QStringLiteral("demo.cROM")));
    std::cout << "dump import: first save returns " << w.saveProject() << '\n';
    D("dump import: new project saved");
    std::cout << "dump import: files on disk";
    for (const QString& name : work.entryList(QStringList{QStringLiteral("demo.*")}, QDir::Files, QDir::Name)) {
        std::cout << ' ' << name.toStdString();
    }
    const QStringList recent = QSettings().value(QStringLiteral("recentProjects")).toStringList();
    std::cout << " | first recent project "
              << QFileInfo(recent.value(0).section(QChar('\t'), 0, 0)).fileName().toStdString() << '\n';
    guarded([&]() { std::cout << "dump import: second save returns " << w.saveProject() << '\n'; });
    D("dump import: saved again without a question");

    // Frames of a second dump: one is in the project, one is repeated, the last is cut off.
    showTab("Frame");
    answerFileDialog(secondDump, [&]() { answerImportDialog(true); });
    w.addFramesFromDump();
    D("dump import: frames added");
    w.m_undoAction->trigger();
    D("dump import: adding frames undone");
    w.m_redoAction->trigger();
    D("dump import: adding frames redone");
    // A dump of another size cannot be added; the dialog says why and offers nothing.
    answerFileDialog(wideDump, [&]() { answerImportDialog(false); });
    w.addFramesFromDump();
    D("dump import: a dump of another size is refused");
    // The first dump again, compared without the masks: the frame that was left out as too
    // short is the one frame the project does not have.
    answerFileDialog(firstDump, [&]() {
        answerImportDialog(false, [](app::DumpImportDialog& dialog) {
            importing::DumpImportOptions options = dialog.options();
            options.useMasksAndShapeModes = false;
            dialog.setOptions(options);
            dialog.startAnalysis();
        });
    });
    w.addFramesFromDump();
    D("dump import: the first dump again, cancelled");

    // Same frames. Frames 0 and 1 get a mask over the pixels they differ in.
    showTab("Mask");
    selectRows({0, 1});
    D("dump import: two frames selected on the mask page");
    clickTip(tipSpotCommon);
    D("dump import: spot common on");
    guarded([&]() { clickTip(tipCommonToMask); });
    D("dump import: common to mask without a mask");
    byTip<QToolButton>(w, "Add an empty comparison mask")->click();
    selectRows({0, 1});
    byTip<QSpinBox>(w, "Assigned comparison mask ID")->setValue(0);
    D("dump import: mask 0 assigned to both frames");
    clickTip(tipCommonToMask);
    D("dump import: common to mask");
    w.m_undoAction->trigger();
    D("dump import: common to mask undone");
    w.m_redoAction->trigger();
    D("dump import: common to mask redone");
    clickTip(tipSpotCommon);
    D("dump import: spot common off");
    w.m_comparisonMaskOverlayButton->setChecked(false);
    clickTip("Count the same frames of the current frame for every mask, with and without shape mode");
    D("dump import: same list updated");
    {
        auto* sameCombo = byTip<QComboBox>(w, "Go to a frame that is the same");
        sameCombo->setCurrentIndex(0);
        sameCombo->activated(0);
    }
    D("dump import: went to the same frame");
    {
        auto* usingCombo = byTip<QComboBox>(w, "List the frames that use this mask");
        usingCombo->setCurrentIndex(1);
    }
    D("dump import: frames using mask 0 listed");
    clickTip("Select the frames that use the mask above");
    D("dump import: frames using mask 0 selected");

    w.m_frameList->clearSelection();
    w.m_frameList->setCurrentRow(0);
    clickAnswered(tipDeleteCurrent, QStringLiteral("Cancel"));
    D("dump import: delete same as current cancelled");
    clickAnswered(tipDeleteCurrent, QStringLiteral("Delete"));
    D("dump import: same frame deleted");
    w.m_undoAction->trigger();
    D("dump import: same-frame delete undone");
    // With the selected frames kept, nothing is left to delete for them.
    selectRows({0, 1});
    byTip<QCheckBox>(w, "Deleting same frames never deletes a selected frame")->click();
    guarded([&]() { clickTip(tipDeleteSelected); });
    D("dump import: delete same as selected, selected kept");
    byTip<QCheckBox>(w, "Deleting same frames never deletes a selected frame")->click();
    clickAnswered(tipDeleteSelected, QStringLiteral("Delete"));
    D("dump import: delete same as selected");
    w.m_undoAction->trigger();
    D("dump import: delete same as selected undone");
    w.m_frameList->clearSelection();
    w.m_frameList->setCurrentRow(3);
    clickAnswered(tipDeleteAll, QStringLiteral("Delete"));
    D("dump import: all duplicates deleted");
    guarded([&]() { clickTip(tipDeleteAll); });
    D("dump import: no duplicate left");
    w.m_undoAction->trigger();
    D("dump import: delete all duplicates undone");

    // A new project while the open one has changes: a question first.
    answerMessageBox(QStringLiteral("Cancel"));
    w.newProjectFromDump();
    D("dump import: new project refused at the question");
    answerMessageBox(QStringLiteral("Discard Changes"), [&]() {
        answerFileDialog(wideDump, [&]() { answerImportDialog(true); });
    });
    w.newProjectFromDump();
    D("dump import: 192x64 project from a dump");
    showTab("Frame");
    D("dump import: 192x64 project, frame page");
    answerMessageBox(QStringLiteral("Discard Changes"), [&]() {
        answerFileDialog(bigDump, [&]() { answerImportDialog(true); });
    });
    w.newProjectFromDump();
    D("dump import: 256x64 project from a dump");
    {
        // What a save writes for a project of each layout was checked in the tests; here
        // only that the new project serializes.
        std::ostringstream crom;
        std::string error;
        const bool written = legacy::writeLegacyCrom(crom, w.m_document->toSnapshot(), error);
        std::cout << "dump import: 256x64 saves=" << written << " bytes=" << crom.str().size() << '\n';
    }
    w.m_document.reset();
    w.m_currentCromPath.clear();
    w.m_currentCrpPath.clear();
    w.refreshProjectSummary();
    D("dump import: closed");
    QDir(workPath).removeRecursively();
}
