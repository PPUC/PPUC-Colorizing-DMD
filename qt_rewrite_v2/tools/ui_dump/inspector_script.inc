// Frame / Mask / BG / Dyn inspector coverage. Included at the end of main() in dump.cpp.
{
    using render::CanvasHit;
    using render::CanvasRegion;
    using render::Resolution;
    using Tool = app::MainWindow::DrawingTool;

    document::ProjectSnapshot is;
    is.colorCount = 16;
    is.romName = "inspector";
    for (int m = 0; m < 3; ++m) {
        document::IndexedFrame mask = document::makeIndexedFrame(render::kSdFrameSize, 0);
        for (int y = 2 + m; y < 10 + m * 3; ++y) {
            for (int x = 4 * m; x < 30 + 10 * m; ++x) {
                mask.pixels[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)] = 1;
            }
        }
        is.comparisonMasks.push_back(mask);
    }
    const auto patternFrame = [](render::Size size, int seed) {
        document::Rgb565Frame frame = document::makeRgb565Frame(size, 0);
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                frame.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
                             static_cast<std::size_t>(x)] =
                    static_cast<uint16_t>(((x / 8) * 2113 + (y / 4) * 797 + seed * 4099) & 0xffff);
            }
        }
        return frame;
    };
    is.backgrounds.push_back(document::BackgroundRecord{patternFrame(render::kSdFrameSize, 11),
                                                        patternFrame(render::kHdFrameSize, 12)});
    is.backgrounds.push_back(document::BackgroundRecord{patternFrame(render::kSdFrameSize, 13), std::nullopt});
    is.backgrounds.push_back(document::BackgroundRecord{patternFrame(render::kSdFrameSize, 14), std::nullopt});
    const auto makeDynamic = [](int seed, bool withShadows) {
        document::FrameDynamicData dyn{
            document::makeIndexedFrame(render::kSdFrameSize, 255),
            document::makeIndexedFrame(render::kHdFrameSize, 255),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
        };
        for (int y = 12; y < 20; ++y) {
            for (int x = 40; x < 60; ++x) {
                dyn.maskSd.pixels[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)] =
                    static_cast<uint8_t>(x < 50 ? 0 : 2);
            }
        }
        for (int y = 24; y < 40; ++y) {
            for (int x = 80; x < 120; ++x) {
                dyn.maskHd.pixels[static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x)] =
                    static_cast<uint8_t>(x < 100 ? 0 : 2);
            }
        }
        for (std::size_t i = 0; i < dyn.colorsSd.size(); ++i) {
            dyn.colorsSd[i] = static_cast<uint16_t>((i * 331 + static_cast<std::size_t>(seed) * 977) & 0xffff);
            dyn.colorsHd[i] = static_cast<uint16_t>((i * 173 + static_cast<std::size_t>(seed) * 1543) & 0xffff);
        }
        if (withShadows) {
            dyn.shadowDirectionsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
            dyn.shadowColorsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
            dyn.shadowDirectionsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
            dyn.shadowColorsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
            dyn.shadowDirectionsSd[0] = 0x81;
            dyn.shadowColorsSd[0] = 0x07e0;
            dyn.shadowDirectionsSd[2] = 0x10;
            dyn.shadowColorsSd[2] = 0xf81f;
            dyn.shadowDirectionsHd[0] = 0x08;
            dyn.shadowColorsHd[0] = 0x001f;
        }
        return dyn;
    };
    const auto makeBackgroundData = [](uint16_t id, int seed) {
        document::FrameBackgroundData data{
            id,
            document::makeIndexedFrame(render::kSdFrameSize, 0),
            document::makeIndexedFrame(render::kHdFrameSize, 0),
        };
        for (int y = 0; y < 16; ++y) {
            for (int x = seed; x < 64 + seed; ++x) {
                data.maskSd.pixels[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)] = 1;
                data.maskHd.pixels[static_cast<std::size_t>(y * 2) * 256 + static_cast<std::size_t>(x * 2)] = 1;
            }
        }
        return data;
    };
    for (int i = 0; i < 5; ++i) {
        document::ProjectFrameSnapshot frame{
            document::makeIndexedFrame(render::kSdFrameSize, 0),
            patternFrame(render::kSdFrameSize, i),
            std::nullopt,
            0,
            255,
        };
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 128; ++x) {
                frame.originalSd.pixels[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)] =
                    static_cast<uint8_t>((x / 6 + y / 5 + i) % 16);
            }
        }
        is.frames.push_back(frame);
    }
    is.frames[0].colorizedHd = patternFrame(render::kHdFrameSize, 20);
    is.frames[0].comparisonMaskId = 0;
    is.frames[0].dynamicData = makeDynamic(1, true);
    is.frames[0].backgroundData = makeBackgroundData(0, 3);
    is.frames[0].triggerId = 5;
    is.frames[0].durationMs = 100;
    is.frames[0].spriteReferences.assign(document::kMaxSpritesPerFrame, document::FrameSpriteReference{});
    is.frames[0].spriteReferences[0] = document::FrameSpriteReference{0, 10, 5, 40, 20};
    is.frames[1].colorizedHd = patternFrame(render::kHdFrameSize, 21);
    is.frames[1].comparisonMaskId = 1;
    is.frames[1].shapeComparisonMode = 1;
    is.frames[1].dynamicData = makeDynamic(2, false);
    is.frames[1].backgroundData = makeBackgroundData(1, 9);
    is.frames[1].triggerId = 65432;
    is.frames[1].durationMs = 40000;
    is.frames[2].shapeComparisonMode = 3;
    is.frames[2].backgroundData = makeBackgroundData(0xffff, 0);
    is.frames[2].triggerId = 70000;
    is.frames[3].comparisonMaskId = 7;
    is.frames[3].dynamicData = makeDynamic(3, true);
    is.frames[3].backgroundData = makeBackgroundData(9, 20);
    is.frames[3].durationMs = 5000;
    is.frames[4].durationMs = 20;
    {
        document::SpriteRecord sprite;
        sprite.name = "one";
        sprite.originalSd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedSd = document::makeRgb565Frame({256, 64}, 0);
        sprite.maskHd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedHd = document::makeRgb565Frame({256, 64}, 0);
        sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
        sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
        sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, document::SpriteDetectionArea{});
        for (std::size_t index = 0; index < 256 * 6; ++index) {
            sprite.originalSd.pixels[index] = static_cast<uint8_t>(index % 16);
            sprite.colorizedSd.pixels[index] = static_cast<uint16_t>(index * 7);
        }
        is.sprites.push_back(sprite);
    }
    document::PaletteRecord ip0;
    ip0.colors.assign(document::kPaletteColorCount, 0x2468);
    is.palettes = {ip0};
    std::string inspectorError;
    if (!document::isValidProjectSnapshot(is, &inspectorError)) {
        std::cerr << "invalid inspector snapshot: " << inspectorError << '\n';
        return 5;
    }
    const auto loadInspector = [&]() {
        w.m_undoHistories = edit::UndoHistories();
        w.m_document = document::ProjectDocument::fromSnapshot(is);
        w.m_document->markClean();
        w.refreshProjectSummary();
    };
    const auto loadPlain = [&]() {
        w.m_undoHistories = edit::UndoHistories();
        w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
        w.m_document->markClean();
        w.refreshProjectSummary();
    };

    // Look the controls up while they still carry their construction-time tool tips.
    w.m_document.reset();
    w.refreshProjectSummary();
    const auto toolButtonByText = [&](const char* textPrefix) {
        return toolButtonByTextPrefix(w, textPrefix);
    };
    auto* inMaskId = byTip<QSpinBox>(w, "Assigned comparison mask ID");
    auto* inShape = byTip<QComboBox>(w, "Shape comparison mode");
    auto* inAssignMask = byTip<QToolButton>(w, "Assign selected comparison mask to selected frames");
    auto* inEditMask = byTip<QToolButton>(w, "Show and edit the assigned comparison mask");
    auto* inDynSet = byTip<QSpinBox>(w, "Dynamic mask set to paint");
    auto* inShadowColor = toolButtonByText("Shadow Color");
    std::vector<QToolButton*> inDynColors(16, nullptr);
    std::vector<QToolButton*> inShadowDirs;
    for (QToolButton* button :
         inShadowColor->parentWidget()->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly)) {
        bool isNumber = false;
        const int number = button->text().toInt(&isNumber);
        if (isNumber && number >= 0 && number < 16) inDynColors[static_cast<std::size_t>(number)] = button;
    }
    for (const char* label : {"TL", "T", "TR", "L", "R", "BL", "B", "BR"}) {
        QToolButton* found = nullptr;
        for (QToolButton* button :
             inShadowColor->parentWidget()->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (button->text() == QLatin1String(label)) found = button;
        }
        if (!found) { std::cerr << "missing shadow direction " << label << '\n'; return 5; }
        inShadowDirs.push_back(found);
    }
    for (QToolButton* button : inDynColors) {
        if (!button) { std::cerr << "missing dynamic color button\n"; return 5; }
    }
    auto* inBgId = byTip<QSpinBox>(w, "Assigned background ID");
    auto* inTrigger = byTip<QSpinBox>(w, "Per-frame trigger ID");
    auto* inDuration = byTip<QSpinBox>(w, "Per-frame cRP duration");
    auto* inMono = byTip<QCheckBox>(w, "Use legacy switch-to-monochrome trigger ID 65432");
    auto* inBgCombo = byTip<QComboBox>(w, "Loaded background");
    auto* inAssignBg = byTip<QToolButton>(w, "Assign selected background to selected frames");
    auto* inEditBg = byTip<QToolButton>(w, "Switch drawing target to the selected background");
    auto* inFrameToBg = byTip<QToolButton>(w, "Copy the current frame image into the selected background");
    auto* inBgToFrame = byTip<QToolButton>(w, "Copy the selected background image into selected frames");
    auto* inImportBg = byTip<QToolButton>(w, "Import an image into the selected background");
    auto* inExportBg = byTip<QToolButton>(w, "Export the selected background as PNG");
    auto* inShowBg = byTip<QToolButton>(w, "Show assigned background layer");
    auto* inImportFrame = byTip<QToolButton>(w, "Import an image into selected frame pixels");
    auto* inCreateHd = toolButtonByText("Create HD");
    auto* inRemoveHd = toolButtonByText("Remove HD");
    auto* inTarget = byTip<QComboBox>(w, "Drawing target");
    auto* inDmdShape = byTip<QComboBox>(w, "DMD pixel shape for canvas and PNG export");
    auto* inDmdSize = byTip<QSpinBox>(w, "Relative DMD pixel size");

    QTabWidget* tabs = nullptr;
    for (QTabWidget* candidate : w.findChildren<QTabWidget*>()) {
        for (int i = 0; i < candidate->count(); ++i) {
            if (candidate->tabText(i) == QLatin1String("Dyn")) tabs = candidate;
        }
    }
    if (!tabs) { std::cerr << "no inspector tabs\n"; return 5; }
    const auto showTab = [&](const char* name) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == QLatin1String(name)) { tabs->setCurrentIndex(i); return; }
        }
        std::cerr << "no tab " << name << '\n';
        std::exit(5);
    };
    const auto showLeft = [&](const char* name) {
        for (int i = 0; i < w.m_leftBrowserTabs->count(); ++i) {
            if (w.m_leftBrowserTabs->tabText(i) == QLatin1String(name)) {
                w.m_leftBrowserTabs->setCurrentIndex(i);
                return;
            }
        }
        std::cerr << "no left tab " << name << '\n';
        std::exit(5);
    };
    const auto D = [&](const char* title) { dump(title, w); };
    const auto row = [&](int r) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(r);
    };
    const auto selectRows = [&](std::initializer_list<int> rows) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(*rows.begin());
        for (int r : rows) w.m_frameList->item(r)->setSelected(true);
    };
    const auto col = [](int x, int y, Resolution resolution = Resolution::Sd) {
        return CanvasHit{CanvasRegion::ColorizedFrame, resolution, x, y};
    };
    const auto orig = [](int x, int y) {
        return CanvasHit{CanvasRegion::OriginalFrame, Resolution::Sd, x, y};
    };
    const auto undo = [&](const char* title) { w.m_undoAction->trigger(); dump(title, w); };
    const auto redo = [&](const char* title) { w.m_redoAction->trigger(); dump(title, w); };

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
    // color: accept with that color; no color: cancel.
    const auto answerColorDialog = [&](std::optional<QColor> color) {
        QTimer::singleShot(0, [color, modalDialog]() {
            auto* dialog = qobject_cast<QColorDialog*>(modalDialog());
            if (!dialog) { std::cerr << "no color dialog\n"; std::exit(3); }
            std::cout << "color dialog: " << dialog->windowTitle().toStdString() << " | current "
                      << dialog->currentColor().name().toStdString() << '\n';
            if (color) {
                dialog->setCurrentColor(*color);
                static_cast<QDialog*>(dialog)->accept();
            } else {
                dialog->reject();
            }
        });
    };
    // toggles: indices of the option check boxes to click before answering.
    const auto answerCreateHdDialog = [&](bool accept, std::vector<int> toggles) {
        QTimer::singleShot(0, [accept, toggles, modalDialog]() {
            QDialog* dialog = modalDialog();
            if (!dialog) { std::cerr << "no create hd dialog\n"; std::exit(3); }
            std::cout << "dialog: " << dialog->windowTitle().toStdString();
            for (QLabel* label : dialog->findChildren<QLabel*>()) {
                std::cout << " | label '" << label->text().toStdString() << "'";
            }
            const QList<QCheckBox*> boxes = dialog->findChildren<QCheckBox*>();
            for (QCheckBox* box : boxes) {
                std::cout << " | '" << box->text().toStdString() << "' tip='" << box->toolTip().toStdString()
                          << "' en=" << box->isEnabled() << " checked=" << box->isChecked();
            }
            for (QAbstractButton* button : dialog->findChild<QDialogButtonBox*>()->buttons()) {
                std::cout << " | button " << button->text().toStdString();
            }
            std::cout << '\n';
            for (int index : toggles) boxes.at(index)->click();
            if (accept) dialog->accept(); else dialog->reject();
        });
    };
    const auto answerMessageBox = [modalDialog](const QString& buttonText) {
        QTimer::singleShot(0, [buttonText, modalDialog]() {
            auto* box = qobject_cast<QMessageBox*>(modalDialog());
            if (!box) { std::cerr << "no message box\n"; std::exit(3); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | " << box->text().toStdString() << '\n';
            for (QAbstractButton* button : box->buttons()) {
                if (button->text() == buttonText) { button->click(); return; }
            }
            std::cerr << "no button " << buttonText.toStdString() << '\n';
            std::exit(3);
        });
    };
    // path: accept with that file; empty: cancel. messageBoxButton answers a message box that
    // follows the accepted dialog.
    const auto answerFileDialog = [&](const QString& path, const QString& messageBoxButton = QString()) {
        QTimer::singleShot(0, [path, messageBoxButton, modalDialog, answerMessageBox]() {
            auto* dialog = qobject_cast<QFileDialog*>(modalDialog());
            if (!dialog) { std::cerr << "no file dialog\n"; std::exit(3); }
            std::cout << "file dialog: " << dialog->windowTitle().toStdString() << " | mode "
                      << int(dialog->acceptMode()) << '/' << int(dialog->fileMode()) << " | filters "
                      << dialog->nameFilters().join(QStringLiteral(";;")).toStdString() << " | default name "
                      << QFileInfo(dialog->selectedFiles().value(0)).fileName().toStdString() << '\n';
            if (path.isEmpty()) {
                dialog->reject();
                return;
            }
            dialog->selectFile(path);
            if (!messageBoxButton.isEmpty()) answerMessageBox(messageBoxButton);
            static_cast<QDialog*>(dialog)->accept();
            if (dialog->isVisible()) { std::cerr << "file dialog did not accept " << path.toStdString() << '\n'; std::exit(3); }
        });
    };
    // For handlers that are expected to return before they open anything.
    const auto closeUnexpectedModalLater = [&]() {
        QTimer::singleShot(0, [modalDialog]() {
            if (QDialog* dialog = modalDialog()) {
                std::cout << "unexpected modal: " << dialog->windowTitle().toStdString() << '\n';
                dialog->reject();
            }
        });
    };

    // Each call that could open a dialog gets its own "close whatever opened" timer; a timer
    // nothing consumed is flushed before the next call.
    const auto guarded = [&](const std::function<void()>& call) {
        closeUnexpectedModalLater();
        call();
        QApplication::processEvents();
    };

    QTemporaryDir ioTemporaryDir;
    if (!ioTemporaryDir.isValid()) { std::cerr << "cannot create temporary directory\n"; return 5; }
    const QString ioDir = ioTemporaryDir.path();
    const QString importPng = ioDir + QStringLiteral("/import.png");
    {
        QImage image(64, 16, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixelColor(x, y, QColor((x * 4) & 255, (y * 16) & 255, ((x + y) * 8) & 255));
            }
        }
        if (!image.save(importPng, "PNG")) { std::cerr << "cannot write import png\n"; return 5; }
    }
    const QString notAnImage = ioDir + QStringLiteral("/broken.png");
    {
        QFile file(notAnImage);
        if (!file.open(QIODevice::WriteOnly)) { std::cerr << "cannot write broken png\n"; return 5; }
        file.write("not an image");
    }
    const auto printImageFile = [](const char* what, const QString& path) {
        const QImage image = QImage(path).convertToFormat(QImage::Format_ARGB32);
        std::cout << what << ": " << image.width() << 'x' << image.height() << ':'
                  << (image.isNull() ? 0 : qHashBits(image.constBits(), static_cast<size_t>(image.sizeInBytes())))
                  << '\n';
    };

    // ===== overview: every page, every frame, SD and HD
    loadInspector();
    D("inspector loaded with the sprite toggles of the sprite script");
    for (const char* tip : {"Show selected sprite in the main canvas",
                            "Draw a shaped sprite capture selection on the original frame",
                            "Show selected sprite slot bounds on original frame",
                            "Show sprite detection areas on original sprite preview"}) {
        byTip<QToolButton>(w, tip)->setChecked(false);
    }
    showLeft("Frames");
    showTab("Frame");
    D("inspector loaded, frame page");
    dumpFocusChain(w);
    showTab("Mask");
    D("mask page");
    showTab("BG");
    D("bg page");
    showTab("Dyn");
    D("dyn page");
    dumpFocusChain(w);
    showTab("Frame");
    for (int r = 0; r < 5; ++r) {
        row(r);
        std::cout << "frame row " << r << " sd\n";
        D("frame row sd");
    }
    w.m_hdButton->click();
    for (int r = 0; r < 5; ++r) {
        row(r);
        std::cout << "frame row " << r << " hd\n";
        D("frame row hd");
    }
    w.m_sdButton->click();
    w.m_frameList->selectAll();
    D("inspector all frames selected");
    selectRows({1, 3});
    D("inspector frames 1 and 3 selected");
    row(0);

    // ===== Frame page
    w.setDrawingTool(Tool::Point);
    w.setActiveDrawColor(0x07ff);
    inTarget->setCurrentIndex(1);
    D("target background");
    w.m_preview->m_pixelClickedHandler(col(3, 3), false);
    D("background target pixel painted");
    undo("background target undo");
    redo("background target redo");
    inTarget->setCurrentIndex(0);
    D("target frame");
    w.m_preview->m_pixelClickedHandler(col(4, 4), false);
    D("frame pixel painted");
    undo("frame pixel undo");

    inBgId->setValue(1);
    D("bg id 1");
    inBgId->setValue(-1);
    D("bg id none");
    inBgId->setValue(2);
    D("bg id 2");
    row(4);
    inBgId->setValue(0);
    D("bg id on frame without background data");
    w.m_frameList->selectAll();
    inBgId->setValue(1);
    D("bg id multi frame with a frame without data");
    selectRows({0, 1});
    inBgId->setValue(0);
    D("bg id multi frame");
    row(0);

    row(1);
    inTrigger->setValue(7);
    D("trigger edit ignored on mono frame");
    inMono->click();
    D("mono off takes trigger spin value");
    inMono->click();
    D("mono on");
    row(0);
    inTrigger->setValue(123);
    D("trigger 123");
    inMono->click();
    D("mono on frame 0");
    inMono->click();
    D("mono off frame 0");
    row(2);
    inTrigger->setValue(9);
    D("trigger on unsupported legacy id");
    w.m_frameList->selectAll();
    inTrigger->setValue(77);
    D("trigger multi frame");
    inMono->click();
    D("mono multi frame");
    inMono->click();
    D("mono off multi frame");
    row(0);

    inDuration->setValue(250);
    D("duration 250");
    row(1);
    inDuration->setValue(10);
    D("duration on unsupported value");
    selectRows({2, 3, 4});
    inDuration->setValue(60);
    D("duration multi frame");
    row(0);

    inDmdShape->setCurrentIndex(1);
    D("dmd dots");
    inDmdSize->setValue(73);
    D("dmd size 73 rounds");
    inDmdSize->setValue(50);
    D("dmd size 50");
    inShowBg->click();
    D("show bg off");
    {
        const QString out = ioDir + QStringLiteral("/frame.png");
        answerFileDialog(out);
        w.exportCurrentFramePng();
        printImageFile("exported frame dots 50 without bg", out);
        D("frame exported");
        QFile::remove(out);
        inShowBg->click();
        inDmdSize->setValue(100);
        inDmdShape->setCurrentIndex(0);
        answerFileDialog(out);
        w.exportCurrentFramePng();
        printImageFile("exported frame squares 100 with bg", out);
        answerFileDialog(QString());
        w.exportCurrentFramePng();
    }
    D("show bg on, dmd squares 100");

    answerFileDialog(QString());
    inImportFrame->click();
    D("import frame image cancelled");
    answerFileDialog(notAnImage, QStringLiteral("OK"));
    inImportFrame->click();
    D("import frame image unreadable");
    answerFileDialog(importPng);
    inImportFrame->click();
    D("import frame image sd");
    answerFileDialog(importPng);
    inImportFrame->click();
    D("import frame image again");
    undo("import undo");
    redo("import redo");
    w.m_hdButton->click();
    w.m_frameList->selectAll();
    answerFileDialog(importPng);
    inImportFrame->click();
    D("import frame image hd multi with frames without hd");
    row(4);
    answerFileDialog(importPng);
    inImportFrame->click();
    D("import frame image hd on frame without hd");
    w.m_sdButton->click();

    row(2);
    answerCreateHdDialog(false, {});
    inCreateHd->click();
    D("create hd cancelled (unassigned bg data)");
    answerCreateHdDialog(true, {});
    inCreateHd->click();
    D("create hd frame 2");
    undo("undo after create hd");
    redo("redo after create hd");
    w.m_preview->m_pixelClickedHandler(col(9, 9, Resolution::Hd), false);
    D("hd pixel painted on new hd frame");
    inRemoveHd->click();
    D("remove hd frame 2");
    undo("undo after remove hd");
    row(3);
    answerCreateHdDialog(true, {0});
    inCreateHd->click();
    D("create hd frame 3 without dynamic (out of range bg)");
    inRemoveHd->click();
    D("remove hd frame 3");
    inBgId->setValue(1);
    answerCreateHdDialog(true, {});
    inCreateHd->click();
    D("create hd frame 3 with everything (bg 1 gets hd)");
    row(0);
    guarded([&]() { w.createCurrentFrameHd(); });
    D("create hd on hd frame");
    row(4);
    w.removeCurrentFrameHd();
    D("remove hd on sd frame");
    answerCreateHdDialog(true, {});
    inCreateHd->click();
    D("create hd on plain frame 4");
    inRemoveHd->click();
    w.m_sdButton->click();
    row(0);

    for (int filter = 0; filter < w.m_frameFilterComboBox->count(); ++filter) {
        w.m_frameFilterComboBox->setCurrentIndex(filter);
        std::cout << "filter " << filter << '\n';
        D("frame filter");
    }
    w.m_frameFilterComboBox->setCurrentIndex(2);
    row(0);
    inBgId->setValue(-1);
    D("frame leaves background filter");
    w.m_frameFilterComboBox->setCurrentIndex(1);
    row(1);
    inDuration->setValue(333);
    D("duration under hd filter");
    inRemoveHd->click();
    D("frame leaves hd filter");
    w.m_frameFilterComboBox->setCurrentIndex(0);
    row(0);
    inBgId->setValue(0);
    D("filter all again");

    // Playback: every duration is long enough that no timer fires during the dumps.
    w.m_frameList->selectAll();
    inDuration->setValue(4500);
    row(2);
    w.m_useProjectTimingCheckBox->click();
    w.m_playbackIntervalSpinBox->setValue(5000);
    D("playback prepared");
    w.m_playbackButton->click();
    w.m_colorRotationPreviewTimer->stop();
    D("playing");
    w.m_playbackIntervalSpinBox->setValue(4900);
    D("playing interval 4900");
    w.m_useProjectTimingCheckBox->click();
    D("playing with crp timing");
    inDuration->setValue(4800);
    D("playing duration edited");
    w.advancePlaybackFrame();
    D("playing advanced");
    w.m_loopPlaybackButton->click();
    row(4);
    w.advancePlaybackFrame();
    D("playback ends without loop");
    w.m_loopPlaybackButton->click();
    w.m_playbackButton->click();
    w.m_colorRotationPreviewTimer->stop();
    w.advancePlaybackFrame();
    D("playback wraps with loop");
    w.m_playbackButton->click();
    D("playback stopped by button");
    w.m_playbackIntervalSpinBox->setValue(100);
    row(0);

    // ===== Mask page
    showTab("Mask");
    showLeft("Masks");
    D("mask page with mask browser");
    w.m_maskList->setCurrentRow(2);
    D("mask list row 2");
    inMaskId->setValue(1);
    D("mask id 1");
    inMaskId->setValue(-1);
    D("mask id none");
    inMaskId->setValue(0);
    D("mask id 0");
    row(3);
    D("mask page frame 3 (out of range mask id)");
    inMaskId->setValue(2);
    D("mask id 2 on frame 3");
    row(0);
    inShape->setCurrentIndex(1);
    D("shape mode");
    inShape->setCurrentIndex(0);
    D("full comparison");
    row(2);
    D("mask page frame 2 (legacy shape mode)");
    inShape->setCurrentIndex(1);
    D("shape mode replaces legacy mode");
    w.setCurrentFrameShapeComparisonMode(3);
    D("invalid shape mode rejected");
    selectRows({0, 1});
    inShape->setCurrentIndex(1);
    D("shape mode multi frame");
    inShape->setCurrentIndex(0);
    row(0);

    inEditMask->click();
    D("edit mask");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_originalClickedHandler(orig(100, 25), false);
    D("mask pixel painted");
    w.m_preview->m_originalClickedHandler(orig(5, 5), true);
    D("mask pixel erased");
    w.m_preview->m_originalClickedHandler(orig(5, 5), true);
    D("mask pixel erased twice");
    w.setDrawingTool(Tool::FloodFill);
    w.m_preview->m_originalClickedHandler(orig(120, 30), false);
    D("mask flood filled");
    undo("mask undo");
    redo("mask redo");
    undo("mask undo again");
    w.setDrawingTool(Tool::FilledRectangle);
    w.m_preview->m_strokePreviewHandler(orig(60, 20), orig(70, 28), false);
    D("mask stroke preview");
    w.m_preview->m_strokeFinishedHandler(orig(60, 20), orig(70, 28), false);
    D("mask stroke painted");
    w.setDrawingTool(Tool::Point);
    w.m_frameList->selectAll();
    w.m_preview->m_originalClickedHandler(orig(110, 3), false);
    D("mask pixel painted with all frames selected");
    row(4);
    D("mask mode on frame without mask");
    w.m_preview->m_originalClickedHandler(orig(1, 1), false);
    w.editAssignedMask();
    D("edit mask without mask");
    w.setDrawingTool(Tool::FloodFill);
    w.m_preview->m_originalClickedHandler(orig(1, 1), false);
    D("mask flood without mask");
    w.setDrawingTool(Tool::Point);

    w.m_dynamicMaskOverlayButton->click();
    D("overlay: dynamic replaces mask");
    w.m_backgroundMaskOverlayButton->click();
    D("overlay: bg replaces dynamic");
    w.m_comparisonMaskOverlayButton->click();
    D("overlay: mask and bg");
    w.m_dynamicMaskOverlayButton->click();
    D("overlay: dynamic replaces both");
    w.m_dynamicMaskOverlayButton->click();
    D("overlay: all off");

    w.m_frameList->selectAll();
    w.m_maskList->setCurrentRow(1);
    D("mask list row 1 with all frames selected");
    inAssignMask->click();
    D("assign mask to all frames");
    w.m_maskList->setCurrentItem(nullptr);
    D("mask list without current item");
    w.assignSelectedMaskToFrames();
    D("assign without selected mask");
    row(0);
    inMaskId->setValue(0);

    // ===== Dyn page
    showTab("Dyn");
    showLeft("Frames");
    D("dyn page frame 0");
    inDynSet->setValue(2);
    D("dyn set 2");
    inDynSet->setValue(31);
    D("dyn set 31");
    inDynSet->setValue(0);
    D("dyn set 0");
    answerColorDialog(QColor(255, 0, 0));
    inDynColors[1]->click();
    D("dyn color 1 red");
    answerColorDialog(std::nullopt);
    inDynColors[2]->click();
    D("dyn color cancelled");
    answerColorDialog(QColor(255, 0, 0));
    inDynColors[1]->click();
    D("dyn color unchanged");
    undo("dyn color undo");
    redo("dyn color redo");
    inDynSet->setValue(2);
    answerColorDialog(QColor(0, 255, 0));
    inDynColors[15]->click();
    D("dyn set 2 color 15 green");
    w.m_hdButton->click();
    D("dyn page hd");
    answerColorDialog(QColor(0, 0, 255));
    inDynColors[3]->click();
    D("dyn hd color 3 blue");
    undo("dyn hd color undo");
    w.m_sdButton->click();
    inDynSet->setValue(0);
    for (std::size_t i = 0; i < inShadowDirs.size(); ++i) {
        inShadowDirs[i]->click();
        std::cout << "shadow direction " << i << '\n';
        D("shadow direction clicked");
    }
    undo("shadow direction undo");
    redo("shadow direction redo");
    answerColorDialog(QColor(255, 0, 255));
    inShadowColor->click();
    D("shadow color magenta");
    answerColorDialog(std::nullopt);
    inShadowColor->click();
    D("shadow color cancelled");
    undo("shadow color undo");
    redo("shadow color redo");
    w.m_frameList->selectAll();
    inShadowDirs[4]->click();
    D("shadow direction multi frame");
    answerColorDialog(QColor(255, 255, 0));
    inShadowColor->click();
    D("shadow color multi frame");
    row(1);
    D("dyn page frame 1 (no shadow data)");
    inShadowDirs[2]->click();
    D("shadow direction on frame without shadow data");
    answerColorDialog(QColor(0, 255, 255));
    inDynColors[0]->click();
    D("dyn color on frame 1");
    row(4);
    D("dyn page frame 4 (no dynamic data)");
    guarded([&]() { w.editDynamicColor(0); });
    w.setDynamicShadowDirectionBit(1, true);
    guarded([&]() { w.editDynamicShadowColor(); });
    D("dyn handlers without dynamic data");
    row(0);

    w.m_dynamicMaskOverlayButton->click();
    D("dynamic overlay on");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_pixelClickedHandler(col(10, 10), false);
    D("dynamic mask pixel painted set 0");
    w.m_preview->m_pixelClickedHandler(col(10, 10), true);
    D("dynamic mask pixel erased");
    inDynSet->setValue(5);
    w.m_preview->m_pixelClickedHandler(col(11, 10), false);
    D("dynamic mask pixel painted set 5");
    inDynSet->setValue(0);
    w.setDrawingTool(Tool::ColorPicker);
    w.m_preview->m_pixelClickedHandler(col(11, 10), false);
    D("dynamic set picked");
    w.m_preview->m_pixelClickedHandler(col(100, 30), false);
    D("dynamic set pick on empty pixel");
    w.setDrawingTool(Tool::FloodFill);
    w.m_preview->m_pixelClickedHandler(col(100, 30), false);
    D("dynamic mask flood filled");
    undo("dynamic mask undo");
    redo("dynamic mask redo");
    undo("dynamic mask undo again");
    w.setDrawingTool(Tool::Point);
    w.m_frameList->selectAll();
    w.m_preview->m_pixelClickedHandler(col(20, 2), false);
    D("dynamic mask painted with all frames selected");
    row(4);
    w.m_preview->m_pixelClickedHandler(col(20, 2), false);
    w.setDrawingTool(Tool::FloodFill);
    w.m_preview->m_pixelClickedHandler(col(20, 2), false);
    w.setDrawingTool(Tool::ColorPicker);
    w.m_preview->m_pixelClickedHandler(col(20, 2), false);
    w.setDrawingTool(Tool::Point);
    D("dynamic mask tools without dynamic data");
    row(0);
    w.m_hdButton->click();
    w.m_preview->m_pixelClickedHandler(col(20, 20, Resolution::Hd), false);
    D("dynamic mask hd pixel painted");
    undo("dynamic mask hd undo");
    w.m_sdButton->click();
    w.m_dynamicMaskOverlayButton->click();
    D("dynamic overlay off");

    // ===== BG page
    showTab("BG");
    showLeft("Backgrounds");
    D("bg page with background browser");
    w.m_backgroundList->setCurrentRow(1);
    D("background list row 1");
    w.m_backgroundList->setCurrentRow(2);
    D("background list row 2");
    inBgCombo->setCurrentIndex(0);
    D("background combo index 0");
    row(4);
    D("bg page frame 4 (no background data)");
    inAssignBg->click();
    D("assign bg without background data");
    row(2);
    w.m_backgroundList->setCurrentRow(2);
    inAssignBg->click();
    D("assign bg 2 to frame 2");
    w.m_frameList->selectAll();
    w.m_backgroundList->setCurrentRow(1);
    inAssignBg->click();
    D("assign bg 1 to all frames");
    selectRows({0, 2});
    w.m_backgroundList->setCurrentRow(0);
    inAssignBg->click();
    D("assign bg 0 to frames 0 and 2");
    row(0);

    inEditBg->click();
    D("edit bg 0");
    w.setDrawingTool(Tool::Point);
    w.setActiveDrawColor(0xffe0);
    w.m_preview->m_pixelClickedHandler(col(7, 7), false);
    D("bg pixel painted");
    w.setDrawingTool(Tool::ColorPicker);
    w.m_preview->m_pixelClickedHandler(col(60, 20), false);
    D("bg color picked");
    w.setDrawingTool(Tool::FloodFill);
    w.setActiveDrawColor(0xffe0);
    w.m_preview->m_pixelClickedHandler(col(60, 20), false);
    D("bg flood filled");
    w.setDrawingTool(Tool::FilledRectangle);
    w.m_preview->m_strokePreviewHandler(col(10, 4), col(20, 9), false);
    D("bg stroke preview");
    w.m_preview->m_strokeFinishedHandler(col(10, 4), col(20, 9), false);
    D("bg stroke painted");
    undo("bg undo");
    undo("bg undo twice");
    redo("bg redo");
    w.setDrawingTool(Tool::Point);
    w.m_hdButton->click();
    D("edit bg 0 in hd");
    w.m_preview->m_pixelClickedHandler(col(9, 9, Resolution::Hd), false);
    D("bg hd pixel painted");
    w.m_backgroundList->setCurrentRow(2);
    D("background without hd selected in hd bg target");
    inEditBg->click();
    D("edit bg without hd falls back to sd");
    inTarget->setCurrentIndex(0);
    D("bg page target frame again");

    w.m_backgroundList->setCurrentRow(2);
    inFrameToBg->click();
    D("frame to bg 2");
    inFrameToBg->click();
    D("frame to bg 2 again");
    w.m_backgroundList->setCurrentRow(1);
    inBgToFrame->click();
    D("bg 1 to frame 0");
    inBgToFrame->click();
    D("bg 1 to frame 0 again");
    undo("bg to frame undo");
    w.m_frameList->selectAll();
    w.m_backgroundList->setCurrentRow(0);
    inBgToFrame->click();
    D("bg 0 to all frames");
    w.m_hdButton->click();
    inBgToFrame->click();
    D("bg 0 to all frames in hd");
    row(0);
    inFrameToBg->click();
    D("frame 0 to bg 0 in hd");
    w.m_backgroundList->setCurrentRow(2);
    inFrameToBg->click();
    D("frame 0 to bg 2 in hd falls back to sd");
    inBgToFrame->click();
    D("bg 2 to frame 0 in hd falls back to sd");
    row(4);
    w.m_backgroundList->setCurrentRow(0);
    inFrameToBg->click();
    D("sd frame 4 to bg 0 in hd");
    w.m_sdButton->click();
    row(0);

    w.m_backgroundList->setCurrentRow(2);
    answerFileDialog(QString());
    inImportBg->click();
    D("import bg cancelled");
    answerFileDialog(notAnImage, QStringLiteral("OK"));
    inImportBg->click();
    D("import bg unreadable");
    answerFileDialog(importPng);
    inImportBg->click();
    D("import bg 2 sd");
    answerFileDialog(importPng);
    inImportBg->click();
    D("import bg 2 sd again");
    w.m_hdButton->click();
    answerFileDialog(importPng);
    inImportBg->click();
    D("import bg 2 hd creates hd");
    inTarget->setCurrentIndex(1);
    undo("import bg hd undo");
    redo("import bg hd redo");
    inTarget->setCurrentIndex(0);
    {
        const QString out = ioDir + QStringLiteral("/background.png");
        answerFileDialog(out);
        inExportBg->click();
        printImageFile("exported bg 2 hd", out);
        QFile::remove(out);
        w.m_sdButton->click();
        w.m_backgroundList->setCurrentRow(1);
        answerFileDialog(out);
        inExportBg->click();
        printImageFile("exported bg 1 sd", out);
        answerFileDialog(QString());
        inExportBg->click();
    }
    D("bg exported");

    w.m_backgroundMaskOverlayButton->click();
    D("bg mask overlay on");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_pixelClickedHandler(col(100, 25), false);
    D("bg mask pixel painted");
    w.m_preview->m_pixelClickedHandler(col(5, 5), true);
    D("bg mask pixel erased");
    w.setDrawingTool(Tool::FloodFill);
    w.m_preview->m_pixelClickedHandler(col(120, 30), false);
    D("bg mask flood filled");
    undo("bg mask undo");
    redo("bg mask redo");
    undo("bg mask undo again");
    w.setDrawingTool(Tool::Point);
    w.m_frameList->selectAll();
    w.m_preview->m_pixelClickedHandler(col(90, 28), false);
    D("bg mask painted with all frames selected");
    row(4);
    w.m_preview->m_pixelClickedHandler(col(90, 28), false);
    D("bg mask paint without background data");
    w.setDrawingTool(Tool::FloodFill);
    w.m_preview->m_pixelClickedHandler(col(90, 28), false);
    D("bg mask flood without background data");
    w.setDrawingTool(Tool::Point);
    row(3);
    inBgId->setValue(-1);
    w.m_preview->m_pixelClickedHandler(col(90, 28), false);
    D("bg mask paint with unassigned background");
    row(0);
    w.m_hdButton->click();
    w.m_preview->m_pixelClickedHandler(col(200, 60, Resolution::Hd), false);
    D("bg mask hd pixel painted");
    w.m_sdButton->click();
    w.m_backgroundMaskOverlayButton->click();
    D("bg mask overlay off");

    // ===== toggles kept across project changes, missing selections, missing data
    showTab("Frame");
    showLeft("Frames");
    inTarget->setCurrentIndex(1);
    inShowBg->click();
    inDynSet->setValue(7);
    inDmdShape->setCurrentIndex(1);
    w.m_comparisonMaskOverlayButton->click();
    w.m_backgroundMaskOverlayButton->click();
    w.m_frameFilterComboBox->setCurrentIndex(3);
    D("toggles set before reload");
    loadInspector();
    D("inspector reloaded with toggles set");
    w.m_frameList->clearSelection();
    w.m_frameList->setCurrentRow(-1);
    D("inspector no frame selected");
    const auto callHandlers = [&]() {
        w.assignSelectedMaskToFrames();
        w.editAssignedMask();
        w.setCurrentFrameComparisonMaskId(1);
        w.setCurrentFrameShapeComparisonMode(1);
        w.setCurrentFrameBackgroundId(0);
        w.setCurrentFrameTriggerId(3);
        w.setCurrentFrameDurationMs(30);
        w.setCurrentFrameSwitchToMonochrome(true);
        w.assignSelectedBackgroundToFrames();
        w.copyCurrentFrameToSelectedBackground();
        w.copySelectedBackgroundToFrames();
        guarded([&]() { w.createCurrentFrameHd(); });
        w.removeCurrentFrameHd();
        guarded([&]() { w.editDynamicColor(0); });
        w.setDynamicShadowDirectionBit(1, true);
        guarded([&]() { w.editDynamicShadowColor(); });
        guarded([&]() { w.importImageIntoSelectedFrames(); });
        guarded([&]() { w.exportCurrentFramePng(); });
        w.m_canvasEdit.pickDynamicSet(w.canvasEditContext(), CanvasHit{CanvasRegion::ColorizedFrame, Resolution::Sd, 1, 1});
        w.m_preview->m_pixelClickedHandler(CanvasHit{CanvasRegion::ColorizedFrame, Resolution::Sd, 1, 1}, false);
    };
    callHandlers();
    D("handlers without frame");
    w.editAssignedBackground();
    D("edit bg without frame");
    w.m_frameFilterComboBox->setCurrentIndex(0);
    D("filter all after no selection");
    loadPlain();
    D("plain project with toggles set");
    for (const char* name : {"Mask", "BG", "Dyn", "Frame"}) {
        showTab(name);
        std::cout << "plain project page " << name << '\n';
        D("plain project page");
    }
    callHandlers();
    D("handlers on plain project");
    guarded([&]() { w.importImageIntoSelectedBackground(); });
    guarded([&]() { w.exportSelectedBackgroundPng(); });
    w.editAssignedBackground();
    D("background handlers without backgrounds");
    w.m_frameList->setCurrentRow(2);
    inRemoveHd->click();
    D("plain project remove hd");
    answerCreateHdDialog(true, {});
    inCreateHd->click();
    D("plain project create hd");
    w.m_frameFilterComboBox->setCurrentIndex(3);
    D("plain project dynamic filter shows no frames");
    w.m_frameFilterComboBox->setCurrentIndex(1);
    D("plain project hd filter");
    w.m_frameFilterComboBox->setCurrentIndex(0);
    inTarget->setCurrentIndex(0);
    inShowBg->click();
    inDynSet->setValue(0);
    inDmdShape->setCurrentIndex(0);
    w.m_comparisonMaskOverlayButton->click();
    w.m_backgroundMaskOverlayButton->click();
    D("plain project toggles reset");
    w.m_document.reset();
    w.refreshProjectSummary();
    callHandlers();
    guarded([&]() { w.importImageIntoSelectedBackground(); });
    guarded([&]() { w.exportSelectedBackgroundPng(); });
    D("handlers without project");
    inTarget->setCurrentIndex(1);
    inDynSet->setValue(3);
    inMaskId->setValue(0);
    inBgId->setValue(0);
    inTrigger->setValue(4);
    inDuration->setValue(40);
    inMono->click();
    inShape->setCurrentIndex(1);
    D("controls changed without project");
    inMono->click();
    inTarget->setCurrentIndex(0);
    inDynSet->setValue(0);
    inTrigger->setValue(0);
    inDuration->setValue(0);
    inShape->setCurrentIndex(0);
    loadInspector();
    D("inspector loaded at the end");

    // ===== Undo across histories, frame attributes and mode switches
    showTab("Frame");
    showLeft("Frames");
    row(0);
    w.m_preview->m_pixelClickedHandler(col(3, 3), false);
    inDuration->setValue(500);
    w.m_preview->m_pixelClickedHandler(col(4, 3), false);
    inBgCombo->setCurrentIndex(0);
    inFrameToBg->click();
    D("pixel, duration, pixel, frame to bg");
    undo("undo 1 of 4 in the frame context");
    undo("undo 2 of 4 in the frame context");
    undo("undo 3 of 4 in the frame context");
    undo("undo 4 of 4 in the frame context");
    redo("redo 1 of 4 in the frame context");
    redo("redo 2 of 4 in the frame context");
    redo("redo 3 of 4 in the frame context");
    redo("redo 4 of 4 in the frame context");

    inMaskId->setValue(2);
    inShape->setCurrentIndex(1);
    inBgId->setValue(1);
    inTrigger->setValue(77);
    inMono->click();
    D("frame 0 attributes edited");
    undo("undo mono");
    undo("undo trigger");
    undo("undo bg id");
    undo("undo shape mode");
    undo("undo mask id");
    redo("redo mask id");
    inMono->click();
    inMono->click();
    D("mono on and off again");
    inTrigger->setValue(-1);
    D("trigger cleared");

    selectRows({0, 1, 2});
    inMaskId->setValue(1);
    inShape->setCurrentIndex(1);
    inDuration->setValue(70);
    inMono->click();
    D("attributes on three frames");
    inMono->click();
    D("mono off on three frames");
    row(1);
    undo("undo on frame 1 after the three frame edit");

    showTab("Mask");
    showLeft("Masks");
    row(0);
    w.m_maskList->setCurrentRow(2);
    D("mask 2 selected in the browser");
    row(3);
    D("mask selection after a frame change");
    selectRows({3, 4});
    inAssignMask->click();
    D("selected mask assigned to two frames");
    undo("undo mask assignment on the current frame");

    auto* inSpriteCanvas = byTip<QToolButton>(w, "Show selected sprite in the main canvas");
    row(0);
    inSpriteCanvas->click();
    D("sprite canvas before edit mask");
    inEditMask->click();
    D("edit mask from the sprite canvas");
    w.m_comparisonMaskOverlayButton->click();
    inSpriteCanvas->click();
    inEditBg->click();
    D("edit bg from the sprite canvas");
    inTarget->setCurrentIndex(0);

    showTab("Frame");
    showLeft("Frames");
    row(1);
    w.m_hdButton->click();
    w.m_preview->m_pixelClickedHandler(col(20, 20, Resolution::Hd), false);
    inRemoveHd->click();
    D("remove hd on frame 1");
    undo("undo remove hd on frame 1");
    redo("redo remove hd on frame 1");
    answerCreateHdDialog(true, {});
    inCreateHd->click();
    D("create hd with related data on frame 1");
    undo("undo create hd with related data");
    redo("redo create hd with related data");
    w.m_sdButton->click();

    // Relabelling the HD buttons changes the width of the inspector, and one round of event
    // processing does not always finish that layout change.
    const auto settle = []() {
        for (int round = 0; round < 5; ++round) QApplication::processEvents();
    };
    inSpriteCanvas->click();
    (inRemoveHd->isEnabled() ? inRemoveHd : inCreateHd)->click();
    settle();
    D("sprite hd toggled in the sprite canvas");
    w.m_undoAction->trigger();
    settle();
    D("undo sprite hd change");
    w.m_redoAction->trigger();
    settle();
    D("redo sprite hd change");
    inSpriteCanvas->click();
    w.m_sdButton->click();
    settle();
    D("undo states at the end");
}
