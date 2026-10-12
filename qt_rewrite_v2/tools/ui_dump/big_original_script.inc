// Project with 256x64 original frames. Included at the end of main() in dump.cpp, after the
// window was closed, so that no earlier state depends on it.
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
    bs.originalIs256x64 = true;
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
    for (int i = 0; i < 3; ++i) {
        document::ProjectFrameSnapshot frame{
            document::makeIndexedFrame(render::kHdFrameSize, 0),
            bigPattern(render::kSdFrameSize, i),
            std::nullopt,
            0,
            static_cast<uint8_t>(i < 2 ? i : 255),
        };
        // Differs between neighbouring pixels, so sampling (2x, 2y) is not the same as (x, y).
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 256; ++x) {
                frame.originalSd.pixels[pixel(render::kHdFrameSize, x, y)] =
                    static_cast<uint8_t>((x % 2 == 0 && y % 2 == 0) ? (x / 12 + y / 10 + i) % 16
                                                                    : (x + 2 * y + i) % 16);
            }
        }
        frame.spriteReferences.assign(document::kMaxSpritesPerFrame, document::FrameSpriteReference{});
        bs.frames.push_back(frame);
    }
    bs.frames[0].colorizedHd = bigPattern(render::kHdFrameSize, 40);
    bs.frames[1].colorizedHd = bigPattern(render::kHdFrameSize, 41);
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
    }
    for (int s = 0; s < 2; ++s) {
        document::SpriteRecord sprite;
        sprite.name = s == 0 ? "big" : "";
        sprite.originalSd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedSd = document::makeRgb565Frame({256, 64}, 0);
        sprite.maskHd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedHd = document::makeRgb565Frame({256, 64}, 0);
        sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
        sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
        sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, document::SpriteDetectionArea{});
        bs.sprites.push_back(sprite);
    }
    document::PaletteRecord bp0;
    bp0.colors.assign(document::kPaletteColorCount, 0x1357);
    bs.palettes = {bp0};
    std::string bigError;
    if (!document::isValidProjectSnapshot(bs, &bigError)) {
        std::cerr << "invalid 256x64 snapshot: " << bigError << '\n';
        return 6;
    }

    QTabWidget* tabs = nullptr;
    for (QTabWidget* candidate : w.findChildren<QTabWidget*>()) {
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
    // Hits on the original region of a 256x64 original are on its own 256x64 grid.
    const auto orig = [](int x, int y) {
        return CanvasHit{CanvasRegion::OriginalFrame, Resolution::Hd, x, y};
    };
    const auto col = [](int x, int y, Resolution resolution = Resolution::Sd) {
        return CanvasHit{CanvasRegion::ColorizedFrame, resolution, x, y};
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
    // The capture reads the sprite back, so print what the dump's hashes cannot show.
    const auto printSprite = [&](std::size_t spriteIndex) {
        const document::SpriteRecord& sprite = w.m_document->sprite(spriteIndex);
        std::cout << "sprite " << spriteIndex << " rows:";
        for (int y = 0; y < 3; ++y) {
            std::cout << " |";
            for (int x = 0; x < 6; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x);
                std::cout << ' ' << int(sprite.originalSd.pixels[index]) << '/' << std::hex
                          << sprite.colorizedSd.pixels[index] << std::dec;
            }
        }
        std::cout << '\n';
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
    w.m_hdButton->click();
    D("256 original: hd");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_pixelClickedHandler(col(200, 50, Resolution::Hd), false);
    D("256 original: hd pixel painted");
    undo("256 original: undo hd pixel");
    w.m_sdButton->click();
    D("256 original: sd");
    row(2);
    D("256 original: sd-only frame without mask");
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
    w.m_hdButton->click();
    D("256 original: edit mask in hd");
    w.m_sdButton->click();
    w.setDrawingTool(Tool::Point);
    w.m_comparisonMaskOverlayButton->click();
    D("256 original: mask overlay toggled");
    showLeft("Frames");

    // ===== dynamic overlay and background layer
    showTab("Dyn");
    w.m_dynamicMaskOverlayButton->click();
    D("256 original: dynamic overlay sd");
    w.m_preview->m_pixelClickedHandler(col(10, 20), false);
    D("256 original: dynamic mask pixel painted sd");
    undo("256 original: undo dynamic mask pixel");
    w.m_hdButton->click();
    D("256 original: dynamic overlay hd");
    w.m_sdButton->click();
    w.m_dynamicMaskOverlayButton->click();
    showTab("BG");
    auto* bigShowBg = byTip<QToolButton>(w, "Show assigned background layer");
    if (!bigShowBg->isChecked()) bigShowBg->click();
    D("256 original: background layer sd");
    w.m_hdButton->click();
    D("256 original: background layer hd");
    w.m_sdButton->click();

    // ===== sprite capture
    auto* bigSlot = byTip<QComboBox>(w, "Current frame sprite slot");
    auto* bigCapture = byTip<QToolButton>(w, "Copy original/colorized pixels from current frame bounds into selected sprite");
    auto* bigSelectCapture = byTip<QToolButton>(w, "Draw a shaped sprite capture selection on the original frame");
    auto* bigShowBounds = byTip<QToolButton>(w, "Show selected sprite slot bounds on original frame");
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
    D("256 original: capture point in the same sd pixel");
    w.setDrawingTool(Tool::FilledRectangle);
    w.m_preview->m_strokePreviewHandler(orig(96, 36), orig(107, 45), false);
    D("256 original: capture rectangle preview");
    w.m_preview->m_strokeFinishedHandler(orig(96, 36), orig(107, 45), false);
    D("256 original: capture rectangle painted");
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_originalClickedHandler(orig(96, 36), true);
    D("256 original: capture point erased");
    answerNextMessageBox(QStringLiteral("Replace Current"));
    bigCapture->click();
    printSprite(0);
    D("256 original: captured selection into sprite 0");
    bigSelectCapture->click();
    D("256 original: capture mode off");
    w.m_spriteList->setCurrentRow(1);
    bigSlot->setCurrentIndex(0);
    D("256 original: sprite 1, slot 0");
    bigMinX->setValue(100);
    bigMinY->setValue(20);
    bigMaxX->setValue(127);
    bigMaxY->setValue(31);
    D("256 original: slot bounds edited");
    answerNextMessageBox(QStringLiteral("Replace Current"));
    bigCapture->click();
    printSprite(1);
    D("256 original: captured slot bounds into sprite 1");
    if (!bigShowBounds->isChecked()) bigShowBounds->click();
    D("256 original: slot bounds shown");
    w.m_hdButton->click();
    D("256 original: sprite page hd");
    w.m_sdButton->click();
    showTab("Frame");
    showLeft("Frames");
    D("256 original: frame page at the end");

    w.m_document.reset();
    w.refreshProjectSummary();
    D("256 original: closed");
}
