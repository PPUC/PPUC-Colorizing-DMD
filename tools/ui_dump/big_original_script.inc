// Project with 256x64 original frames: only its HD plane is rendered and edited, one to one with
// the original. Included at the end of main() in dump.cpp, after the window was closed, so that
// no earlier state depends on it.
//
// "HD" in the comments and titles below is the stored extra plane. The window never calls it
// that for such a project: it has no SD and HD buttons, the plane is "256x64", missing data is
// "no picture" / "not rendered" and the repair is "Create 256x64 Frame" / "... Sprite".
//
//   frame 0: HD, dynamic data, background 0 (HD)
//   frame 1: HD
//   frame 2: no HD frame
//   frame 3: HD, background 1 (no HD), sprite 1 (no HD) in slot 0
//   frame 4: no HD frame
//   sprite 0: HD, empty; sprite 1 and 2: no HD, with an original and SD colours
{
    using render::CanvasHit;
    using render::CanvasRegion;
    using render::Resolution;
    using Tool = app::MainWindow::DrawingTool;

    const auto pixel = [](render::Size size, int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) + static_cast<std::size_t>(x);
    };

    document::ProjectSnapshot bs;
    bs.colorCount = 16;
    bs.romName = "big_original";
    bs.geometry = render::ProjectGeometry(render::PlaneLayout::NativeExtra);
    for (int m = 0; m < 2; ++m) {
        document::IndexedFrame mask = document::makeIndexedFrame(render::kHdFrameSize, 0);
        for (int y = 40 + m; y < 60; ++y) {
            for (int x = 150 + 20 * m; x < 250; ++x) mask.pixels[pixel(render::kHdFrameSize, x, y)] = 1;
        }
        bs.comparisonMasks.push_back(mask);
    }
    const auto bigPattern = [&](render::Size size, int seed) {
        document::Rgb565Frame frame = document::makeRgb565Frame(size, 0);
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                frame.pixels[pixel(size, x, y)] =
                    static_cast<uint16_t>(((x / 8) * 1723 + (y / 4) * 911 + seed * 3301) & 0xffff);
            }
        }
        return frame;
    };
    bs.backgrounds.push_back(document::BackgroundRecord{bigPattern(render::kSdFrameSize, 31),
                                                        bigPattern(render::kHdFrameSize, 32)});
    bs.backgrounds.push_back(document::BackgroundRecord{bigPattern(render::kSdFrameSize, 33)});
    for (int i = 0; i < 5; ++i) {
        document::ProjectFrameSnapshot frame{
            document::makeIndexedFrame(render::kHdFrameSize, 0),
            bigPattern(render::kSdFrameSize, i),
            std::nullopt,
            0,
            static_cast<uint8_t>(i < 2 ? i : 255),
        };
        // Differs between neighbouring pixels, so a (2x, 2y) subsample would show.
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 256; ++x) {
                frame.originalSd.pixels[pixel(render::kHdFrameSize, x, y)] =
                    static_cast<uint8_t>((x % 2 == 0 && y % 2 == 0) ? (x / 12 + y / 10 + i) % 16
                                                                    : (x + 2 * y + i) % 16);
            }
        }
        frame.spriteReferences.assign(document::kLegacySpritesPerFrame, document::FrameSpriteReference{});
        bs.frames.push_back(frame);
    }
    bs.frames[0].colorizedHd = bigPattern(render::kHdFrameSize, 40);
    bs.frames[1].colorizedHd = bigPattern(render::kHdFrameSize, 41);
    bs.frames[3].colorizedHd = bigPattern(render::kHdFrameSize, 43);
    {
        document::FrameDynamicData dyn{
            document::makeIndexedFrame(render::kSdFrameSize, 255),
            document::makeIndexedFrame(render::kHdFrameSize, 255),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
        };
        for (int y = 4; y < 14; ++y) {
            for (int x = 70; x < 100; ++x) dyn.maskSd.pixels[pixel(render::kSdFrameSize, x, y)] = static_cast<uint8_t>(x < 85 ? 0 : 3);
        }
        for (int y = 8; y < 28; ++y) {
            for (int x = 140; x < 200; ++x) dyn.maskHd.pixels[pixel(render::kHdFrameSize, x, y)] = static_cast<uint8_t>(x < 170 ? 0 : 3);
        }
        for (std::size_t c = 0; c < dyn.colorsSd.size(); ++c) {
            dyn.colorsSd[c] = static_cast<uint16_t>((c * 2731 + 97) & 0xffff);
            dyn.colorsHd[c] = static_cast<uint16_t>((c * 1321 + 4441) & 0xffff);
        }
        dyn.shadowDirectionsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
        dyn.shadowColorsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
        dyn.shadowDirectionsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
        dyn.shadowColorsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
        dyn.shadowDirectionsSd[3] = 0x10;
        dyn.shadowColorsSd[3] = 0xf81f;
        dyn.shadowDirectionsHd[0] = 0x08;
        dyn.shadowColorsHd[0] = 0x07ff;
        bs.frames[0].dynamicData = dyn;
        document::FrameBackgroundData background{
            0,
            document::makeIndexedFrame(render::kSdFrameSize, 0),
            document::makeIndexedFrame(render::kHdFrameSize, 0),
        };
        for (int y = 16; y < 32; ++y) {
            for (int x = 0; x < 64; ++x) background.maskSd.pixels[pixel(render::kSdFrameSize, x, y)] = 1;
        }
        for (int y = 32; y < 64; ++y) {
            for (int x = 0; x < 128; ++x) background.maskHd.pixels[pixel(render::kHdFrameSize, x, y)] = 1;
        }
        bs.frames[0].backgroundData = background;
        background.backgroundId = 1;
        bs.frames[3].backgroundData = background;
    }
    for (int s = 0; s < 3; ++s) {
        document::SpriteRecord sprite;
        sprite.name = s == 0 ? "big" : "";
        sprite.hasHd = s == 0;
        sprite.originalSd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedSd = document::makeRgb565Frame({256, 64}, 0);
        sprite.maskHd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedHd = document::makeRgb565Frame({256, 64}, 0);
        sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
        sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
        sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, document::SpriteDetectionArea{});
        if (s >= 1) {
            for (int y = 0; y < 6; ++y) {
                for (int x = 0; x < 9; ++x) {
                    sprite.originalSd.pixels[static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x)] = static_cast<uint8_t>((x + y) % 16);
                    sprite.colorizedSd.pixels[static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x)] = 0xf81f;
                }
            }
        }
        bs.sprites.push_back(sprite);
    }
    bs.frames[3].spriteReferences[0] = document::FrameSpriteReference{1, 0, 0, 255, 63};
    document::PaletteRecord bp0;
    bp0.colors.assign(document::kPaletteColorCount, 0x1357);
    bs.palettes = {bp0};
    std::string bigError;
    if (!document::isValidProjectSnapshot(bs, &bigError)) {
        std::cerr << "invalid 256x64 snapshot: " << bigError << '\n';
        return 6;
    }

    UiDumpTabs* tabs = nullptr;
    for (UiDumpTabs* candidate : uiDumpChildren<UiDumpTabs>(w)) {
        for (int i = 0; i < candidate->count(); ++i) {
            if (candidate->tabText(i) == QLatin1String("Dyn")) tabs = candidate;
        }
    }
    if (!tabs) { std::cerr << "no inspector tabs\n"; return 6; }
    const auto showTab = [&](const char* name) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == QLatin1String(name)) { tabs->setCurrentIndex(i); return; }
        }
        std::cerr << "no tab " << name << '\n';
        std::exit(6);
    };
    const auto showLeft = [&](const char* name) {
        // A tree with the frame strip shows the frames below, not in a tab.
        if (QLatin1String(name) == QLatin1String("Frames") &&
            w.m_frameList->parentWidget() != w.m_leftBrowserTabs->currentWidget()) {
            bool tabbed = false;
            for (int i = 0; i < w.m_leftBrowserTabs->count(); ++i) {
                tabbed = tabbed || w.m_leftBrowserTabs->tabText(i) == QLatin1String("Frames");
            }
            if (!tabbed) {
                return;
            }
        }
        for (int i = 0; i < w.m_leftBrowserTabs->count(); ++i) {
            if (w.m_leftBrowserTabs->tabText(i) == QLatin1String(name)) {
                w.m_leftBrowserTabs->setCurrentIndex(i);
                return;
            }
        }
        std::cerr << "no left tab " << name << '\n';
        std::exit(6);
    };
    const auto D = [&](const char* title) { dump(title, w); };
    const auto row = [&](int r) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(r);
    };
    // Both regions of a 256x64 project are on the 256x64 grid.
    const auto orig = [](int x, int y) {
        return CanvasHit{CanvasRegion::OriginalFrame, Resolution::Hd, x, y};
    };
    const auto col = [](int x, int y) {
        return CanvasHit{CanvasRegion::ColorizedFrame, Resolution::Hd, x, y};
    };
    const auto undo = [&](const char* title) { w.m_undoAction->trigger(); dump(title, w); };
    const auto redo = [&](const char* title) { w.m_redoAction->trigger(); dump(title, w); };
    const auto answerNextMessageBox = [](const QString& buttonText) {
        QTimer::singleShot(0, [buttonText]() {
            QMessageBox* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) {
                for (QWidget* top : QApplication::topLevelWidgets()) {
                    if (auto* candidate = qobject_cast<QMessageBox*>(top); candidate && candidate->isVisible()) box = candidate;
                }
            }
            if (!box) { std::cerr << "no message box\n"; std::exit(6); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | " << box->text().toStdString() << '\n';
            for (QAbstractButton* button : box->buttons()) {
                if (button->text() == buttonText) { button->click(); return; }
            }
            std::cerr << "no button " << buttonText.toStdString() << '\n';
            std::exit(6);
        });
    };
    // The capture reads the sprite back, so print what the dump's hashes cannot show:
    // original/HD mask/HD colour of the first pixels, then the HD flag and the SD colour sum.
    const auto printSprite = [&](std::size_t spriteIndex) {
        const document::SpriteRecord& sprite = w.m_document->sprite(spriteIndex);
        std::cout << "sprite " << spriteIndex << " rows:";
        for (int y = 0; y < 3; ++y) {
            std::cout << " |";
            for (int x = 0; x < 6; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x);
                std::cout << ' ' << int(sprite.originalSd.pixels[index]) << '/' << int(sprite.maskHd.pixels[index])
                          << '/' << std::hex << sprite.colorizedHd.pixels[index] << std::dec;
            }
        }
        unsigned long long sdSum = 0;
        for (uint16_t value : sprite.colorizedSd.pixels) sdSum += value;
        std::cout << " | hd " << sprite.hasHd << " sd sum " << sdSum << '\n';
    };
    const auto printFrameHd = [&](std::size_t frameIndex) {
        std::cout << "frame " << frameIndex << " hd:";
        if (!w.m_document->hasHdFrame(frameIndex)) {
            std::cout << " none\n";
            return;
        }
        const document::Rgb565Frame& hd = w.m_document->colorizedFrame(frameIndex, Resolution::Hd);
        for (int x = 0; x < 6; ++x) {
            std::cout << ' ' << int(w.m_document->originalFrame(frameIndex).pixels[static_cast<std::size_t>(64 + x)]) << '/'
                      << std::hex << hd.pixels[static_cast<std::size_t>(64 + x)] << std::dec;
        }
        std::cout << " | sd " << std::hex << w.m_document->colorizedFrame(frameIndex, Resolution::Sd).pixels[0] << std::dec << '\n';
    };
    const auto settle = []() {
        for (int round = 0; round < 5; ++round) QApplication::processEvents();
    };

    // "Create 256x64 Frame" / "Create 256x64 Sprite"; "Create HD ..." in trees that called it so.
    const auto repairButton = [&]() {
        QToolButton* found = nullptr;
        for (QToolButton* button : w.findChildren<QToolButton*>()) {
            if (button->text().startsWith(QLatin1String("Create 256x64")) ||
                button->text().startsWith(QLatin1String("Create HD"))) {
                found = button;
            }
        }
        if (!found) { std::cerr << "no repair button\n"; std::exit(7); }
        return found;
    };
    w.m_undoHistories = edit::UndoHistories();
    w.m_document = document::ProjectDocument::fromSnapshot(bs);
    w.m_document->markClean();
    w.refreshProjectSummary();
    showLeft("Frames");
    showTab("Frame");
    row(0);
    D("256 original: loaded, frame page");
    for (const char* name : {"Mask", "BG", "Sprite", "Dyn", "Palette", "Rot"}) {
        showTab(name);
        std::string title = std::string("256 original: page ") + name;
        D(title.c_str());
    }
    showTab("Frame");
    D("256 original: no sd and hd buttons");
    std::cout << "sd/hd buttons hidden: " << w.m_sdButton->isHidden() << '/' << w.m_hdButton->isHidden() << '\n';
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_pixelClickedHandler(col(201, 51), false);
    D("256 original: hd pixel painted");
    undo("256 original: undo hd pixel");

    // ===== missing HD data and its repair
    row(2);
    printFrameHd(2);
    D("256 original: frame without hd");
    repairButton()->click();
    settle();
    printFrameHd(2);
    D("256 original: hd frame created from the original");
    undo("256 original: undo create hd");
    redo("256 original: redo create hd");
    row(3);
    D("256 original: background and sprite without hd");
    w.m_frameFilterComboBox->setCurrentIndex(
        std::max(w.m_frameFilterComboBox->findText(QStringLiteral("Not rendered")),
                 w.m_frameFilterComboBox->findText(QStringLiteral("HD missing"))));
    D("256 original: filter not rendered");
    w.m_frameFilterComboBox->setCurrentIndex(0);
    row(3);
    repairButton()->click();
    settle();
    D("256 original: hd background created");
    undo("256 original: undo create hd background");
    redo("256 original: redo create hd background");
    showLeft("Backgrounds");
    D("256 original: background list");
    showLeft("Frames");
    row(0);

    // ===== comparison mask
    auto* bigEditMask = byTip<QToolButton>(w, "Show and edit the assigned comparison mask");
    showTab("Mask");
    showLeft("Masks");
    D("256 original: mask page with mask list");
    bigEditMask->click();
    D("256 original: edit mask");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_originalClickedHandler(orig(10, 5), false);
    D("256 original: mask pixel painted");
    w.m_preview->m_originalClickedHandler(orig(255, 63), false);
    D("256 original: last mask pixel painted");
    w.m_preview->m_originalClickedHandler(orig(200, 50), true);
    D("256 original: mask pixel erased");
    w.setDrawingTool(Tool::FilledRectangle);
    w.m_preview->m_strokePreviewHandler(orig(20, 2), orig(140, 30), false);
    D("256 original: mask rectangle preview");
    w.m_preview->m_strokeFinishedHandler(orig(20, 2), orig(140, 30), false);
    D("256 original: mask rectangle painted");
    w.setDrawingTool(Tool::Line);
    w.m_preview->m_strokeFinishedHandler(orig(20, 10), orig(140, 10), true);
    D("256 original: mask line erased");
    w.setDrawingTool(Tool::FloodFill);
    w.m_preview->m_originalClickedHandler(orig(250, 2), false);
    D("256 original: mask flood filled");
    undo("256 original: mask undo flood");
    undo("256 original: mask undo line");
    redo("256 original: mask redo line");
    undo("256 original: mask undo line again");
    undo("256 original: mask undo rectangle");
    w.setDrawingTool(Tool::Point);
    w.m_comparisonMaskOverlayButton->click();
    D("256 original: mask overlay toggled");
    showLeft("Frames");

    // ===== dynamic overlay and background layer
    showTab("Dyn");
    w.m_dynamicMaskOverlayButton->click();
    D("256 original: dynamic overlay");
    w.m_preview->m_pixelClickedHandler(col(211, 41), false);
    D("256 original: dynamic mask pixel painted");
    undo("256 original: undo dynamic mask pixel");
    w.m_dynamicMaskOverlayButton->click();
    showTab("BG");
    auto* bigShowBg = byTip<QToolButton>(w, "Show assigned background layer");
    if (!bigShowBg->isChecked()) bigShowBg->click();
    D("256 original: background layer");

    // ===== sprite capture, on the native grid
    auto* bigSlot = byTip<QComboBox>(w, "Current frame sprite slot");
    auto* bigAssign = byTip<QToolButton>(w, "Assign selected sprite to selected slot");
    auto* bigCapture = byTip<QToolButton>(w, "Copy original/colorized pixels from current frame bounds into selected sprite");
    auto* bigSelectCapture = byTip<QToolButton>(w, "Draw a shaped sprite capture selection on the original frame");
    auto* bigShowBounds = byTip<QToolButton>(w, "Show selected sprite slot bounds on original frame");
    auto* bigCanvas = byTip<QToolButton>(w, "Show selected sprite in the main canvas");
    auto* bigMinX = byTip<QSpinBox>(w, "Sprite slot bounding box minimum X");
    auto* bigMinY = byTip<QSpinBox>(w, "Sprite slot bounding box minimum Y");
    auto* bigMaxX = byTip<QSpinBox>(w, "Sprite slot bounding box maximum X");
    auto* bigMaxY = byTip<QSpinBox>(w, "Sprite slot bounding box maximum Y");
    showTab("Sprite");
    showLeft("Sprites");
    w.m_spriteList->setCurrentRow(0);
    bigSlot->setCurrentIndex(0);
    D("256 original: sprite page, sprite 0, slot 0");
    bigSelectCapture->click();
    D("256 original: capture mode on");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_originalClickedHandler(orig(101, 41), false);
    D("256 original: capture point painted");
    w.m_preview->m_originalClickedHandler(orig(100, 40), false);
    D("256 original: capture point on the neighbouring pixel");
    w.setDrawingTool(Tool::FilledRectangle);
    w.m_preview->m_strokePreviewHandler(orig(97, 37), orig(107, 45), false);
    D("256 original: capture rectangle preview");
    w.m_preview->m_strokeFinishedHandler(orig(97, 37), orig(107, 45), false);
    D("256 original: capture rectangle painted");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_originalClickedHandler(orig(97, 37), true);
    D("256 original: capture point erased");
    answerNextMessageBox(QStringLiteral("Replace Current"));
    bigCapture->click();
    printSprite(0);
    D("256 original: captured selection into sprite 0");
    bigSelectCapture->click();
    D("256 original: capture mode off");
    w.m_spriteList->setCurrentRow(1);
    bigSlot->setCurrentIndex(1);
    bigAssign->click();
    D("256 original: assigned sprite 1 without hd to slot 1");
    bigMaxX->setValue(255);
    bigMaxY->setValue(63);
    bigMinX->setValue(201);
    bigMinY->setValue(41);
    D("256 original: slot bounds edited");
    answerNextMessageBox(QStringLiteral("Replace Current"));
    bigCapture->click();
    printSprite(1);
    D("256 original: captured slot bounds into sprite 1");
    if (!bigShowBounds->isChecked()) bigShowBounds->click();
    D("256 original: slot bounds shown");

    // ===== sprite canvas and HD sprites
    bigCanvas->click();
    settle();
    D("256 original: sprite canvas, captured sprite has hd");
    w.m_spriteList->setCurrentRow(2);
    settle();
    D("256 original: sprite canvas, sprite without hd");
    repairButton()->click();
    settle();
    printSprite(2);
    D("256 original: hd sprite created from the sprite original");
    w.m_undoAction->trigger();
    settle();
    D("256 original: undo create hd sprite");
    bigCanvas->click();
    settle();
    row(4);
    bigCapture->click();
    D("256 original: no capture from a frame without hd");
    showTab("Frame");
    showLeft("Frames");
    row(0);
    D("256 original: frame page at the end");

    w.m_document.reset();
    w.refreshProjectSummary();
    D("256 original: closed");
}
