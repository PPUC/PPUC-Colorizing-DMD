// Sprite coverage. Included at the end of main() in dump.cpp.
{
    using render::CanvasHit;
    using render::CanvasRegion;
    using render::Resolution;

    document::ProjectSnapshot ss;
    ss.colorCount = 16;
    ss.romName = "sprites";
    for (int i = 0; i < 3; ++i) {
        document::ProjectFrameSnapshot frame{
            document::makeIndexedFrame(render::kSdFrameSize, 0),
            document::makeRgb565Frame(render::kSdFrameSize, 0),
            document::makeRgb565Frame(render::kHdFrameSize, 0),
            0,
            255,
        };
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 128; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x);
                frame.originalSd.pixels[index] = static_cast<uint8_t>((x / 4 + y / 3 + i) % 16);
                frame.colorizedSd.pixels[index] = static_cast<uint16_t>((x * 517 + y * 1031 + i * 77) & 0xffff);
            }
        }
        if (i < 2) {
            frame.spriteReferences.assign(document::kLegacySpritesPerFrame, document::FrameSpriteReference{});
        }
        ss.frames.push_back(frame);
    }
    ss.frames[2].colorizedHd.reset();
    ss.frames[0].spriteReferences[0] = document::FrameSpriteReference{0, 10, 5, 40, 20};
    ss.frames[0].spriteReferences[2] = document::FrameSpriteReference{1, 50, 2, 70, 12};
    ss.frames[0].spriteReferences[5] = document::FrameSpriteReference{7, 1, 1, 9, 9};
    auto makeSprite = [](const char* name, bool hd, int w0, int h0, uint16_t color) {
        document::SpriteRecord sprite;
        sprite.name = name;
        sprite.hasHd = hd;
        sprite.originalSd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedSd = document::makeRgb565Frame({256, 64}, 0);
        sprite.maskHd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedHd = document::makeRgb565Frame({256, 64}, 0);
        sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
        sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
        sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, document::SpriteDetectionArea{});
        for (int y = 0; y < h0; ++y) {
            for (int x = 0; x < w0; ++x) {
                const std::size_t index = static_cast<std::size_t>(y + 2) * 256 + static_cast<std::size_t>(x + 3);
                sprite.originalSd.pixels[index] = static_cast<uint8_t>((x + y) % 16);
                sprite.colorizedSd.pixels[index] = static_cast<uint16_t>(color + x * 3 + y);
            }
        }
        if (hd) {
            for (int y = 0; y < h0 * 2; ++y) {
                for (int x = 0; x < w0 * 2; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y + 4) * 256 + static_cast<std::size_t>(x + 6);
                    sprite.maskHd.pixels[index] = 1;
                    sprite.colorizedHd.pixels[index] = static_cast<uint16_t>(color ^ 0x0ff0);
                }
            }
        }
        return sprite;
    };
    ss.sprites.push_back(makeSprite("hero", true, 12, 8, 0xf800));
    ss.sprites.push_back(makeSprite("", false, 20, 10, 0x07e0));
    ss.sprites.push_back(makeSprite("tiny", false, 3, 3, 0x001f));
    ss.sprites[0].detectionAreas[0] = document::SpriteDetectionArea{4, 5, 20, 8};
    ss.sprites[0].detectionAreas[2] = document::SpriteDetectionArea{1, 1, 6, 6};
    ss.sprites[2].detectionAreas[3] = document::SpriteDetectionArea{0, 0, 0, 5};
    document::PaletteRecord sp0;
    sp0.colors.assign(document::kPaletteColorCount, 0x4321);
    ss.palettes = {sp0};
    std::string snapshotError;
    if (!document::isValidProjectSnapshot(ss, &snapshotError)) {
        std::cerr << "invalid sprite snapshot: " << snapshotError << '\n';
        return 4;
    }
    const auto loadSprites = [&]() {
        w.m_undoHistories = edit::UndoHistories();
        w.m_document = document::ProjectDocument::fromSnapshot(ss);
        w.m_document->markClean();
        w.refreshProjectSummary();
    };
    loadSprites();
    dump("sprites loaded", w);

    UiDumpTabs* inspectorTabs = nullptr;
    for (UiDumpTabs* tabs : uiDumpChildren<UiDumpTabs>(w)) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == QLatin1String("Sprite")) {
                inspectorTabs = tabs;
                tabs->setCurrentIndex(i);
            }
        }
    }
    if (!inspectorTabs) { std::cerr << "no inspector tabs\n"; return 4; }
    for (int i = 0; i < w.m_leftBrowserTabs->count(); ++i) {
        if (w.m_leftBrowserTabs->tabText(i) == QLatin1String("Sprites")) w.m_leftBrowserTabs->setCurrentIndex(i);
    }
    dump("sprite tabs shown", w);
    dumpFocusChain(w);

    auto* spName = byTip<QLineEdit>(w, "Selected sprite name");
    auto* spSlot = byTip<QComboBox>(w, "Current frame sprite slot");
    auto* spAssign = byTip<QToolButton>(w, "Assign selected sprite to selected slot");
    auto* spCapture = byTip<QToolButton>(w, "Copy original/colorized pixels from current frame bounds into selected sprite");
    auto* spSelectCapture = byTip<QToolButton>(w, "Draw a shaped sprite capture selection on the original frame");
    auto* spClearCapture = byTip<QToolButton>(w, "Clear the current sprite capture selection");
    auto* spRemove = byAnyTip<QToolButton>(
        w, {"Remove the selected slot's sprite from the selected frames; the slots behind it "
            "move up",
            "Clear selected sprite slot"});
    auto* spShowBounds = byTip<QToolButton>(w, "Show selected sprite slot bounds on original frame");
    auto* spShowAreas = byTip<QToolButton>(w, "Show sprite detection areas on original sprite preview");
    auto* spCanvas = byTip<QToolButton>(w, "Show selected sprite in the main canvas");
    auto* spApply = byTip<QToolButton>(w, "Refresh frame previews from edited sprite data");
    auto* spArea = byTip<QComboBox>(w, "Sprite detection area");
    auto* spClearArea = byTip<QToolButton>(w, "Disable selected sprite detection area");
    auto* spAreaX = byTip<QSpinBox>(w, "Sprite detection area X");
    auto* spAreaY = byTip<QSpinBox>(w, "Sprite detection area Y");
    auto* spAreaW = byTip<QSpinBox>(w, "Sprite detection area width");
    auto* spAreaH = byTip<QSpinBox>(w, "Sprite detection area height");
    auto* spMinX = byTip<QSpinBox>(w, "Sprite slot bounding box minimum X");
    auto* spMinY = byTip<QSpinBox>(w, "Sprite slot bounding box minimum Y");
    auto* spMaxX = byTip<QSpinBox>(w, "Sprite slot bounding box maximum X");
    auto* spMaxY = byTip<QSpinBox>(w, "Sprite slot bounding box maximum Y");

    const auto answerNextMessageBox = [](const QString& buttonText) {
        QTimer::singleShot(0, [buttonText]() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) {
                for (QWidget* top : QApplication::topLevelWidgets()) {
                    if (auto* candidate = qobject_cast<QMessageBox*>(top); candidate && candidate->isVisible()) box = candidate;
                }
            }
            if (!box) { std::cerr << "no message box\n"; std::exit(3); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | " << box->text().toStdString()
                      << " | " << box->informativeText().toStdString() << " | buttons:";
            QAbstractButton* chosen = nullptr;
            for (QAbstractButton* button : box->buttons()) {
                std::cout << ' ' << button->text().toStdString() << '(' << button->isEnabled() << ')';
                if (button->text() == buttonText) chosen = button;
            }
            std::cout << '\n';
            if (!chosen) { std::cerr << "no button " << buttonText.toStdString() << '\n'; std::exit(3); }
            chosen->click();
        });
    };
    const auto original = [](int x, int y) {
        return CanvasHit{CanvasRegion::OriginalFrame, Resolution::Sd, x, y};
    };

    // --- selecting sprites through the left Sprites list
    w.m_spriteList->setCurrentRow(1);
    dump("sprite list row 1 (no hd, unnamed)", w);
    w.m_spriteList->setCurrentRow(2);
    dump("sprite list row 2", w);
    w.m_spriteList->setCurrentRow(0);
    dump("sprite list row 0", w);
    w.m_spriteList->setCurrentItem(nullptr);
    dump("sprite list no current item", w);

    // --- slots
    for (int slot : {2, 5, 1, 31, 0}) {
        spSlot->setCurrentIndex(slot);
        std::cout << "slot " << slot << '\n';
        dump("slot changed", w);
    }

    // --- bounds overlay and bounds editing
    spShowBounds->click();
    dump("show bounds on", w);
    spMinX->setValue(12);
    dump("bounds minX 12", w);
    spMaxX->setValue(5);
    dump("bounds maxX 5 below minX", w);
    spMinY->setValue(30);
    dump("bounds minY 30", w);
    spMaxY->setValue(31);
    dump("bounds maxY 31", w);
    spMaxY->setValue(31);
    dump("bounds unchanged value", w);
    spSlot->setCurrentIndex(1);
    spMaxX->setValue(100);
    dump("bounds on empty slot", w);
    w.m_frameList->selectAll();
    dump("sprites all frames selected", w);
    spMinX->setValue(3);
    dump("bounds multi frame", w);
    w.m_frameList->setCurrentRow(1);
    dump("sprites frame 1 (empty slots)", w);
    w.m_frameList->setCurrentRow(2);
    dump("sprites frame 2 (no slot table)", w);
    spMinX->setValue(9);
    dump("bounds without slots", w);
    w.setCurrentSpriteSlotBounds();
    w.removeCurrentSpriteSlot();
    w.assignSelectedSpriteToCurrentSlot();
    dump("handlers without slots", w);
    w.m_frameList->setCurrentRow(0);
    spSlot->setCurrentIndex(0);
    dump("sprites frame 0 again", w);
    spShowBounds->click();
    dump("show bounds off", w);

    // --- detection areas
    spShowAreas->click();
    dump("show det areas on", w);
    spArea->setCurrentIndex(1);
    dump("area 1 (unset)", w);
    spAreaX->setValue(3);
    dump("area 1 x 3", w);
    spAreaW->setValue(30);
    dump("area 1 w 30", w);
    spAreaH->setValue(10);
    dump("area 1 h 10", w);
    spAreaY->setValue(60);
    dump("area 1 y 60 clamps height", w);
    spAreaX->setValue(255);
    dump("area 1 x 255 clamps width", w);
    spArea->setCurrentIndex(2);
    dump("area 2 (set)", w);
    spClearArea->click();
    dump("area 2 cleared", w);
    spClearArea->click();
    w.clearCurrentSpriteDetectionArea();
    dump("area 2 cleared again", w);
    spArea->setCurrentIndex(3);
    w.m_spriteList->setCurrentRow(2);
    dump("sprite 2 area 3 (zero width)", w);
    spAreaW->setValue(7);
    dump("sprite 2 area 3 w 7", w);
    w.m_spriteList->setCurrentRow(1);
    dump("sprite 1 with det areas shown but none", w);
    spArea->setCurrentIndex(0);
    w.m_spriteList->setCurrentRow(0);
    spArea->setCurrentIndex(1);
    spClearArea->click();
    spArea->setCurrentIndex(0);
    spClearArea->click();
    dump("sprite 0 all areas cleared", w);
    spAreaW->setValue(16);
    dump("sprite 0 area 0 set again", w);
    spShowAreas->click();
    dump("show det areas off", w);
    spApply->click();
    dump("apply after area edits", w);

    // --- rename
    spName->setText(QStringLiteral("renamed hero"));
    Q_EMIT spName->editingFinished();
    dump("sprite renamed", w);
    spName->setText(QString());
    Q_EMIT spName->editingFinished();
    dump("sprite name emptied", w);
    spName->setText(QStringLiteral("0123456789012345678901234567890123456789"));
    Q_EMIT spName->editingFinished();
    dump("sprite long name", w);
    spName->setText(QStringLiteral("hero"));
    Q_EMIT spName->editingFinished();
    w.m_spriteList->setCurrentRow(1);
    spName->setText(QStringLiteral("second"));
    Q_EMIT spName->editingFinished();
    dump("sprite 1 renamed", w);

    // --- assign / remove
    w.m_spriteList->setCurrentRow(2);
    spSlot->setCurrentIndex(1);
    spAssign->click();
    dump("assign sprite 2 to slot 1", w);
    spSlot->setCurrentIndex(5);
    spAssign->click();
    dump("assign over out-of-range reference", w);
    w.m_frameList->selectAll();
    spSlot->setCurrentIndex(3);
    w.m_spriteList->setCurrentRow(0);
    spAssign->click();
    dump("assign multi frame", w);
    spRemove->click();
    dump("remove multi frame", w);
    w.m_frameList->setCurrentRow(0);
    spSlot->setCurrentIndex(2);
    spRemove->click();
    dump("remove slot 2", w);
    spRemove->click();
    w.removeCurrentSpriteSlot();
    dump("remove empty slot 2", w);
    spSlot->setCurrentIndex(0);

    // --- capture selection mode
    spSelectCapture->click();
    dump("capture mode on", w);
    w.setDrawingTool(app::MainWindow::DrawingTool::Point);
    w.m_preview->m_originalClickedHandler(original(10, 5), false);
    w.m_preview->m_originalClickedHandler(original(11, 5), false);
    w.m_preview->m_originalClickedHandler(original(12, 6), false);
    dump("capture points painted", w);
    w.m_preview->m_originalClickedHandler(original(11, 5), true);
    dump("capture point erased", w);
    w.m_preview->m_originalClickedHandler(original(11, 5), true);
    dump("capture point erased twice", w);
    w.setDrawingTool(app::MainWindow::DrawingTool::FilledRectangle);
    w.m_preview->m_strokePreviewHandler(original(20, 8), original(30, 14), false);
    dump("capture rect preview", w);
    w.m_preview->m_strokeFinishedHandler(original(20, 8), original(30, 14), false);
    dump("capture rect painted", w);
    w.setDrawingTool(app::MainWindow::DrawingTool::Line);
    w.m_preview->m_strokeFinishedHandler(original(22, 9), original(28, 9), true);
    dump("capture line erased", w);
    w.setDrawingTool(app::MainWindow::DrawingTool::Point);
    w.m_frameList->setCurrentRow(1);
    dump("capture selection dropped on frame change", w);
    w.m_frameList->setCurrentRow(0);
    w.m_preview->m_originalClickedHandler(original(40, 10), false);
    w.m_preview->m_originalClickedHandler(original(44, 12), false);
    dump("capture repainted", w);
    spClearCapture->click();
    dump("capture selection cleared", w);
    w.m_preview->m_originalClickedHandler(original(60, 20), false);
    w.m_preview->m_originalClickedHandler(original(63, 22), false);
    w.m_preview->m_originalClickedHandler(original(61, 21), false);
    answerNextMessageBox(QStringLiteral("Cancel"));
    spCapture->click();
    dump("capture cancelled", w);
    answerNextMessageBox(QStringLiteral("Replace Current"));
    spCapture->click();
    dump("captured painted selection into sprite 0", w);
    spSelectCapture->click();
    dump("capture mode off", w);
    w.m_preview->m_originalClickedHandler(original(60, 20), false);
    dump("original click with capture mode off", w);
    w.m_spriteList->setCurrentRow(2);
    spSlot->setCurrentIndex(4);
    spMinX->setValue(100);
    spMinY->setValue(4);
    spMaxX->setValue(110);
    spMaxY->setValue(9);
    answerNextMessageBox(QStringLiteral("Replace Current"));
    spCapture->click();
    dump("captured bounds into sprite 2", w);
    w.m_undoAction->trigger();
    dump("capture into sprite 2 undone", w);
    w.m_redoAction->trigger();
    dump("capture into sprite 2 redone", w);
    // Create New: the box, then the name of the new sprite.
    QTimer::singleShot(0, [answerNextMessageBox]() {
        QTimer::singleShot(0, []() {
            auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!input) { std::cerr << "no input dialog\n"; std::exit(3); }
            std::cout << "modal: " << input->windowTitle().toStdString() << " | "
                      << input->labelText().toStdString() << '\n';
            input->setTextValue(QStringLiteral("captured new"));
            input->accept();
        });
    });
    answerNextMessageBox(QStringLiteral("Create New"));
    spCapture->click();
    dump("captured bounds into a new sprite", w);
    w.m_undoAction->trigger();
    dump("capture into the new sprite undone", w);
    w.m_undoAction->trigger();
    dump("new sprite undone", w);
    w.m_redoAction->trigger();
    w.m_redoAction->trigger();
    dump("new sprite and capture redone", w);
    spMinX->setValue(101);
    dump("slot bounds changed", w);
    w.m_undoAction->trigger();
    dump("slot bounds change undone", w);
    spName->setText(QStringLiteral("renamed sprite"));
    spName->editingFinished();
    dump("sprite renamed", w);
    w.m_undoAction->trigger();
    dump("sprite rename undone", w);
    w.m_spriteList->setCurrentRow(0);
    spSlot->setCurrentIndex(0);

    // --- sprite canvas
    const std::vector<render::Point> spritePixels{{3, 2}, {4, 2}, {5, 3}, {6, 4}};
    w.setDrawingTool(app::MainWindow::DrawingTool::Point);
    spCanvas->click();
    dump("sprite canvas on (sprite 0, hd)", w);
    dumpFocusChain(w);
    w.setActiveDrawColor(0x07ff);
    w.m_spriteEditor->m_colorizedPixelsEditedHandler(0, Resolution::Sd, spritePixels, false);
    dump("sprite pixels painted", w);
    w.m_spriteEditor->m_colorizedPixelEditedHandler(0, Resolution::Sd, 4, 2, true);
    dump("sprite pixel erased", w);
    w.m_spriteEditor->m_colorizedPixelEditedHandler(0, Resolution::Sd, 4, 2, true);
    dump("sprite pixel erased twice", w);
    w.m_undoAction->trigger();
    dump("sprite undo erase", w);
    w.m_undoAction->trigger();
    dump("sprite undo paint", w);
    w.m_redoAction->trigger();
    dump("sprite redo paint", w);
    w.m_spriteEditor->m_colorizedFloodFillHandler(0, Resolution::Sd, 8, 6);
    dump("sprite flood fill", w);
    w.m_spriteEditor->m_colorizedPixelPickedHandler(0, Resolution::Sd, 3, 2);
    dump("sprite pixel picked", w);
    spApply->click();
    dump("sprite edits applied", w);
    spApply->click();
    dump("sprite edits applied twice", w);
    w.m_hdButton->click();
    dump("sprite canvas hd", w);
    w.m_spriteEditor->m_colorizedPixelsEditedHandler(0, Resolution::Hd, {{6, 4}, {7, 5}}, false);
    dump("sprite hd pixels painted", w);
    w.m_undoAction->trigger();
    dump("sprite hd undo", w);
    w.m_redoAction->trigger();
    dump("sprite hd redo", w);
    w.m_spriteEditor->m_detectionAreaChangedHandler(2, document::SpriteDetectionArea{1, 2, 3, 4});
    dump("editor detection area 2 dragged", w);
    w.m_spriteEditor->m_detectionAreaChangedHandler(9, document::SpriteDetectionArea{1, 2, 3, 4});
    dump("editor detection area out of range", w);
    spShowAreas->click();
    dump("sprite canvas det areas on", w);
    toolButtonByTextPrefix(w, "Remove HD")->click();
    dump("sprite 0 hd removed", w);
    toolButtonByTextPrefix(w, "Remove HD")->click();
    w.removeCurrentSpriteHd();
    dump("sprite 0 hd removed twice", w);
    toolButtonByTextPrefix(w, "Create HD")->click();
    dump("sprite 0 hd created", w);
    toolButtonByTextPrefix(w, "Create HD")->click();
    w.createCurrentSpriteHd();
    dump("sprite 0 hd created twice", w);
    w.m_spriteList->setCurrentRow(1);
    dump("sprite canvas sprite 1 (no hd) in hd mode", w);
    w.m_spriteEditor->m_colorizedPixelsEditedHandler(1, Resolution::Sd, spritePixels, false);
    dump("sprite 1 painted", w);
    w.m_spriteList->setCurrentRow(0);
    dump("sprite canvas back to sprite 0", w);
    w.m_sdButton->click();
    dump("sprite canvas sd", w);
#ifdef UI_DUMP_HAS_SPRITE_DYNAMICS
    {
        // Sprite dynamic content: paint, undo, shape mode.
        const auto spriteDyn = [&](const char* title) {
            dump(title, w);
            const document::SpriteRecord& sprite = w.m_document->sprite(0);
            int dynamic = 0;
            for (uint8_t value : sprite.dynamicMaskSd.pixels) {
                dynamic += value != 255 ? 1 : 0;
            }
            std::cout << "sprite dynamics: dynamic pixels " << dynamic << " shape "
                      << int(sprite.shapeMode) << " paint "
                      << w.m_spriteEditor->isDynamicPaintMode()
                      << " status '" << w.statusBar()->currentMessage().toStdString() << "'\n";
        };
        w.m_spritePanel->setDynamicPaintChecked(true);
        w.m_spritePanel->onDynamicPaintToggled(true);
        spriteDyn("sprite dynamic paint on");
        w.m_spriteEditor->m_colorizedPixelsEditedHandler(0, Resolution::Sd, spritePixels, false);
        spriteDyn("sprite dynamic pixels painted");
        w.m_undoAction->trigger();
        spriteDyn("sprite dynamic paint undone");
        w.m_spritePanel->onShapeModeToggled(true);
        spriteDyn("sprite shape mode on");
        w.m_undoAction->trigger();
        spriteDyn("sprite shape mode undone");
        w.m_spritePanel->setDynamicPaintChecked(false);
        w.m_spritePanel->onDynamicPaintToggled(false);
    }
#endif
    w.m_frameList->setCurrentRow(1);
    dump("sprite canvas frame 1", w);
    w.m_frameList->setCurrentRow(0);
    w.m_playbackButton->click();
    w.m_playbackTimer->stop();
    w.m_colorRotationPreviewTimer->stop();
    dump("playback started from sprite canvas", w);
    w.m_playbackButton->click();
    dump("playback stopped", w);
    spCanvas->click();
    dump("sprite canvas on again", w);
    w.m_spriteEditor->m_colorizedPixelsEditedHandler(0, Resolution::Sd, {{9, 7}}, false);
    spSelectCapture->click();
    dump("capture mode on while sprite canvas", w);
    spCanvas->click();
    dump("sprite canvas off applies pending edits", w);
    w.m_undoAction->trigger();
    dump("undo outside sprite canvas", w);
    spCanvas->click();
    spShowBounds->click();
    dump("sprite canvas + bounds + capture mode + det areas", w);

    // --- project replaced / closed while the toggles are still on
    loadSprites();
    dump("sprites reloaded with toggles on", w);
    w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
    w.m_document->markClean();
    w.refreshProjectSummary();
    dump("project without sprites with toggles on", w);
    loadSprites();
    w.m_spriteList->setCurrentRow(2);
    dump("sprites loaded again, sprite 2", w);
    w.m_frameList->clearSelection();
    w.m_frameList->setCurrentRow(-1);
    dump("no frame selected", w);
    w.m_document.reset();
    w.refreshProjectSummary();
    dump("closed with toggles on", w);
    spCanvas->click();
    spSelectCapture->click();
    spShowBounds->click();
    spShowAreas->click();
    dump("toggles clicked without project", w);
    loadSprites();
    dump("sprites loaded at the end", w);
}
