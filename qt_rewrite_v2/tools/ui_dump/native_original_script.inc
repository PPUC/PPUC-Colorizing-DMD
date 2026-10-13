// Project with 192x64 original frames: its 192x64 original plane is the only one shown and
// edited, the 96x32 extra plane of the snapshot is discarded. Included at the end of main() in
// dump.cpp, after big_original_script.inc closed its project, so that no earlier state depends
// on it.
//
// The snapshot has 96x32 data everywhere, as a file of the Win32 editor can have:
//   frame 0: dynamic data, background 0
//   frame 1: a flagged 96x32 frame
//   frame 2: 96x32 pixels behind a cleared flag
//   background 0 and sprite 1 have flagged 96x32 data; sprite 0 is empty
{
    using render::CanvasHit;
    using render::CanvasRegion;
    using render::Resolution;
    using Tool = app::MainWindow::DrawingTool;

    const render::ProjectGeometry nativeGeometry(render::PlaneLayout::NativeOriginal);
    const render::Size nativeSize = nativeGeometry.planeSize(Resolution::Sd);
    const render::Size extraSize = nativeGeometry.planeSize(Resolution::Hd);
    const auto pixel = [](render::Size size, int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) + static_cast<std::size_t>(x);
    };
    const auto pattern = [&](render::Size size, int seed) {
        document::Rgb565Frame frame = document::makeRgb565Frame(size, 0);
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                frame.pixels[pixel(size, x, y)] =
                    static_cast<uint16_t>(((x / 8) * 1723 + (y / 4) * 911 + seed * 3301) & 0xffff);
            }
        }
        return frame;
    };

    document::ProjectSnapshot ns;
    ns.colorCount = 4;
    ns.romName = "native_original";
    ns.geometry = nativeGeometry;
    for (int m = 0; m < 2; ++m) {
        document::IndexedFrame mask = document::makeIndexedFrame(nativeSize, 0);
        for (int y = 40 + m; y < 60; ++y) {
            for (int x = 130 + 20 * m; x < 190; ++x) mask.pixels[pixel(nativeSize, x, y)] = 1;
        }
        ns.comparisonMasks.push_back(mask);
    }
    ns.backgrounds.push_back(document::BackgroundRecord{pattern(nativeSize, 31), pattern(extraSize, 32)});
    for (int i = 0; i < 3; ++i) {
        document::ProjectFrameSnapshot frame{
            document::makeIndexedFrame(nativeSize, 0),
            pattern(nativeSize, i),
            std::nullopt,
            0,
            static_cast<uint8_t>(i < 2 ? i : 255),
        };
        for (int y = 0; y < nativeSize.height; ++y) {
            for (int x = 0; x < nativeSize.width; ++x) {
                frame.originalSd.pixels[pixel(nativeSize, x, y)] = static_cast<uint8_t>((x / 3 + 2 * y + i) % 4);
            }
        }
        frame.spriteReferences.assign(document::kMaxSpritesPerFrame,
                                      document::FrameSpriteReference{255, 0, 0, 191, 63});
        ns.frames.push_back(frame);
    }
    ns.frames[1].colorizedHd = pattern(extraSize, 41);
    ns.frames[2].inactiveHd = pattern(extraSize, 42);
    {
        document::FrameDynamicData dyn{
            document::makeIndexedFrame(nativeSize, 255),
            document::makeIndexedFrame(extraSize, 255),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0),
        };
        for (int y = 8; y < 28; ++y) {
            for (int x = 120; x < 180; ++x) dyn.maskSd.pixels[pixel(nativeSize, x, y)] = static_cast<uint8_t>(x < 150 ? 0 : 3);
        }
        for (int y = 4; y < 14; ++y) {
            for (int x = 60; x < 90; ++x) dyn.maskHd.pixels[pixel(extraSize, x, y)] = 1;
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
        dyn.shadowDirectionsHd[1] = 0x08;
        dyn.shadowColorsHd[1] = 0x07ff;
        ns.frames[0].dynamicData = dyn;
        document::FrameBackgroundData background{
            0,
            document::makeIndexedFrame(nativeSize, 0),
            document::makeIndexedFrame(extraSize, 0),
        };
        for (int y = 32; y < 64; ++y) {
            for (int x = 0; x < 96; ++x) background.maskSd.pixels[pixel(nativeSize, x, y)] = 1;
        }
        for (int y = 16; y < 32; ++y) {
            for (int x = 0; x < 48; ++x) background.maskHd.pixels[pixel(extraSize, x, y)] = 1;
        }
        ns.frames[0].backgroundData = background;
    }
    for (int s = 0; s < 2; ++s) {
        document::SpriteRecord sprite;
        sprite.name = s == 0 ? "native" : "";
        sprite.hasHd = s == 1;
        sprite.originalSd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedSd = document::makeRgb565Frame({256, 64}, 0);
        sprite.maskHd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedHd = document::makeRgb565Frame({256, 64}, 0);
        sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
        sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
        sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, document::SpriteDetectionArea{});
        if (s == 1) {
            for (int y = 0; y < 6; ++y) {
                for (int x = 0; x < 9; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x);
                    sprite.originalSd.pixels[index] = static_cast<uint8_t>((x + y) % 4);
                    sprite.colorizedSd.pixels[index] = 0x07e0;
                    sprite.maskHd.pixels[index / 2] = 0;
                    sprite.colorizedHd.pixels[index / 2] = 0xf81f;
                }
            }
        }
        ns.sprites.push_back(sprite);
    }
    document::PaletteRecord np0;
    np0.colors.assign(document::kPaletteColorCount, 0x1357);
    ns.palettes = {np0};
    std::string nativeError;
    if (!document::isValidProjectSnapshot(ns, &nativeError)) {
        std::cerr << "invalid 192x64 snapshot: " << nativeError << '\n';
        return 7;
    }

    QTabWidget* tabs = nullptr;
    for (QTabWidget* candidate : w.findChildren<QTabWidget*>()) {
        for (int i = 0; i < candidate->count(); ++i) {
            if (candidate->tabText(i) == QLatin1String("Dyn")) tabs = candidate;
        }
    }
    if (!tabs) { std::cerr << "no inspector tabs\n"; return 7; }
    const auto showTab = [&](const char* name) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == QLatin1String(name)) { tabs->setCurrentIndex(i); return; }
        }
        std::cerr << "no tab " << name << '\n';
        std::exit(7);
    };
    const auto showLeft = [&](const char* name) {
        for (int i = 0; i < w.m_leftBrowserTabs->count(); ++i) {
            if (w.m_leftBrowserTabs->tabText(i) == QLatin1String(name)) {
                w.m_leftBrowserTabs->setCurrentIndex(i);
                return;
            }
        }
        std::cerr << "no left tab " << name << '\n';
        std::exit(7);
    };
    const auto D = [&](const char* title) { dump(title, w); };
    const auto row = [&](int r) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(r);
    };
    // Both regions of a 192x64 project are on the original plane.
    const auto orig = [](int x, int y) {
        return CanvasHit{CanvasRegion::OriginalFrame, Resolution::Sd, x, y};
    };
    const auto col = [](int x, int y) {
        return CanvasHit{CanvasRegion::ColorizedFrame, Resolution::Sd, x, y};
    };
    const auto undo = [&](const char* title) { w.m_undoAction->trigger(); dump(title, w); };
    const auto answerNextMessageBox = [](const QString& buttonText) {
        QTimer::singleShot(0, [buttonText]() {
            QMessageBox* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) {
                for (QWidget* top : QApplication::topLevelWidgets()) {
                    if (auto* candidate = qobject_cast<QMessageBox*>(top); candidate && candidate->isVisible()) box = candidate;
                }
            }
            if (!box) { std::cerr << "no message box\n"; std::exit(7); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | " << box->text().toStdString() << '\n';
            for (QAbstractButton* button : box->buttons()) {
                if (button->text() == buttonText) { button->click(); return; }
            }
            std::cerr << "no button " << buttonText.toStdString() << '\n';
            std::exit(7);
        });
    };
    const auto settle = []() {
        for (int round = 0; round < 5; ++round) QApplication::processEvents();
    };
    // The document has no 96x32 plane, so the dump's hashes cannot show what a save writes
    // for it: print that (it has to be "no extra data", whatever was edited, and the same
    // every time), and the last pixels of the native plane.
    const auto printPlanes = [&]() {
        const document::ProjectSnapshot saved = w.m_document->toSnapshot();
        const auto write = [](const document::ProjectSnapshot& snapshot, legacy::LegacySectionOffsets* sections) {
            std::ostringstream stream;
            std::string saveError;
            if (!legacy::writeLegacyCrom(stream, snapshot, saveError, sections)) {
                std::cerr << "192 original: save failed: " << saveError << '\n';
                std::exit(7);
            }
            return stream.str();
        };
        legacy::LegacySectionOffsets sections;
        const std::string file = write(saved, &sections);
        // Flags, colors, dynamic colors, shadows and rotations 0, dynamic and sprite masks 255,
        // background masks 1.
        bool neutral = true;
        for (std::size_t i = 0; i + 1 < sections.size(); ++i) {
            const std::string& name = sections[i].first;
            const bool extraBlock = name.rfind("HD ", 0) == 0 || (name.rfind("extra ", 0) == 0 && name.find(" flags") != std::string::npos);
            if (!extraBlock) continue;
            const char expected = name == "HD background masks" ? char(1)
                                  : name.find(" masks") != std::string::npos ? char(255) : char(0);
            for (std::size_t offset = sections[i].second; offset < sections[i + 1].second; ++offset) {
                neutral = neutral && file[offset] == expected;
            }
        }
        std::cout << "save: second save identical=" << (file == write(w.m_document->toSnapshot(), nullptr))
                  << " extra plane written as no extra data=" << neutral << '\n';
        std::cout << "extra plane:";
        for (const document::ProjectFrameSnapshot& frame : saved.frames) {
            std::cout << " f" << (frame.colorizedHd ? "+" : "-")
                      << (frame.colorizedHd ? qHashBits(frame.colorizedHd->pixels.data(), frame.colorizedHd->pixels.size() * 2)
                                            : frame.inactiveHd ? qHashBits(frame.inactiveHd->pixels.data(), frame.inactiveHd->pixels.size() * 2)
                                                               : 0);
            if (frame.dynamicData) {
                std::cout << '/' << qHashBits(frame.dynamicData->maskHd.pixels.data(), frame.dynamicData->maskHd.pixels.size())
                          << '/' << qHashBits(frame.dynamicData->colorsHd.data(), frame.dynamicData->colorsHd.size() * 2);
            }
            if (frame.backgroundData) {
                std::cout << '/' << qHashBits(frame.backgroundData->maskHd.pixels.data(), frame.backgroundData->maskHd.pixels.size());
            }
        }
        for (const document::BackgroundRecord& background : saved.backgrounds) {
            std::cout << " b" << (background.colorizedHd ? "+" : "-")
                      << (background.colorizedHd ? qHashBits(background.colorizedHd->pixels.data(), background.colorizedHd->pixels.size() * 2) : 0);
        }
        for (const document::SpriteRecord& sprite : saved.sprites) {
            std::cout << " s" << (sprite.hasHd ? "+" : "-") << qHashBits(sprite.maskHd.pixels.data(), sprite.maskHd.pixels.size())
                      << '/' << qHashBits(sprite.colorizedHd.pixels.data(), sprite.colorizedHd.pixels.size() * 2);
        }
        std::cout << "\nnative plane:";
        for (std::size_t f = 0; f < w.m_document->frameCount(); ++f) {
            const document::Rgb565Frame& frame = w.m_document->colorizedFrame(f, Resolution::Sd);
            std::cout << " f" << f << ' ' << frame.size.width << 'x' << frame.size.height << ' ' << std::hex
                      << frame.pixels[pixel(frame.size, 191, 63)] << std::dec << '/'
                      << qHashBits(frame.pixels.data(), frame.pixels.size() * 2);
        }
        std::cout << '\n';
    };
    const auto printSprite = [&](std::size_t spriteIndex) {
        const document::SpriteRecord& sprite = w.m_document->sprite(spriteIndex);
        std::cout << "sprite " << spriteIndex << " rows:";
        for (int y = 0; y < 3; ++y) {
            std::cout << " |";
            for (int x = 0; x < 6; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x);
                std::cout << ' ' << int(sprite.originalSd.pixels[index]) << '/' << std::hex << sprite.colorizedSd.pixels[index] << std::dec;
            }
        }
        std::cout << " | hd " << sprite.hasHd << '\n';
    };

    // Looked up while no project is loaded: their texts change with the project.
    QToolButton* const nativeCreateHd = toolButtonByTextPrefix(w, "Create HD");
    QToolButton* const nativeRemoveHd = toolButtonByTextPrefix(w, "Remove HD");
    w.m_undoHistories = edit::UndoHistories();
    w.m_document = document::ProjectDocument::fromSnapshot(ns);
    w.m_document->markClean();
    w.refreshProjectSummary();
    showLeft("Frames");
    showTab("Frame");
    row(0);
    // Nothing is printed before the first section: it would land in the last section of
    // big_original_script.inc.
    D("192 original: loaded, frame page");
    printPlanes();
    for (const char* name : {"Mask", "BG", "Sprite", "Dyn", "Palette", "Rot"}) {
        showTab(name);
        std::string title = std::string("192 original: page ") + name;
        D(title.c_str());
    }
    showTab("Frame");
    D("192 original: no sd, hd, create hd and remove hd buttons");
    std::cout << "sd/hd/create/remove hidden: " << w.m_sdButton->isHidden() << '/' << w.m_hdButton->isHidden() << '/'
              << nativeCreateHd->isHidden() << '/' << nativeRemoveHd->isHidden() << '\n';
    row(1);
    D("192 original: frame whose file had a 96x32 frame");
    row(0);

    // ===== paint on the native plane
    w.setDrawingTool(Tool::Point);
    w.m_preview->m_pixelClickedHandler(col(191, 63), false);
    printPlanes();
    D("192 original: last pixel painted");
    undo("192 original: undo last pixel");
    w.setDrawingTool(Tool::FilledRectangle);
    w.m_preview->m_strokePreviewHandler(col(130, 34), col(191, 63), false);
    D("192 original: rectangle preview beyond 128x32");
    w.m_preview->m_strokeFinishedHandler(col(130, 34), col(191, 63), false);
    D("192 original: rectangle painted beyond 128x32");
    undo("192 original: undo rectangle");
    w.setDrawingTool(Tool::Point);

    // ===== comparison mask
    auto* nativeEditMask = byTip<QToolButton>(w, "Show and edit the assigned comparison mask");
    showTab("Mask");
    showLeft("Masks");
    D("192 original: mask page with mask list");
    nativeEditMask->click();
    D("192 original: edit mask");
    w.m_preview->m_originalClickedHandler(orig(191, 63), false);
    D("192 original: last mask pixel painted");
    w.m_preview->m_originalClickedHandler(orig(150, 45), true);
    D("192 original: mask pixel erased");
    undo("192 original: undo mask erase");
    undo("192 original: undo mask pixel");
    nativeEditMask->click();
    showLeft("Frames");

    // ===== dynamic mask
    showTab("Dyn");
    w.m_dynamicMaskOverlayButton->click();
    D("192 original: dynamic overlay");
    w.m_preview->m_pixelClickedHandler(col(190, 40), false);
    D("192 original: dynamic mask pixel painted");
    undo("192 original: undo dynamic mask pixel");
    w.m_dynamicMaskOverlayButton->click();

    // ===== sprite capture, in 192x64 coordinates
    auto* nativeSlot = byTip<QComboBox>(w, "Current frame sprite slot");
    auto* nativeAssign = byTip<QToolButton>(w, "Assign selected sprite to selected slot");
    auto* nativeCapture = byTip<QToolButton>(w, "Copy original/colorized pixels from current frame bounds into selected sprite");
    auto* nativeSelectCapture = byTip<QToolButton>(w, "Draw a shaped sprite capture selection on the original frame");
    auto* nativeCanvas = byTip<QToolButton>(w, "Show selected sprite in the main canvas");
    auto* nativeMinX = byTip<QSpinBox>(w, "Sprite slot bounding box minimum X");
    auto* nativeMaxX = byTip<QSpinBox>(w, "Sprite slot bounding box maximum X");
    auto* nativeMaxY = byTip<QSpinBox>(w, "Sprite slot bounding box maximum Y");
    showTab("Sprite");
    showLeft("Sprites");
    w.m_spriteList->setCurrentRow(0);
    nativeSlot->setCurrentIndex(0);
    D("192 original: sprite page, sprite 0, slot 0");
    nativeSelectCapture->click();
    w.setDrawingTool(Tool::FilledRectangle);
    w.m_preview->m_strokeFinishedHandler(orig(150, 40), orig(158, 45), false);
    D("192 original: capture rectangle painted beyond column 127");
    answerNextMessageBox(QStringLiteral("Replace Current"));
    nativeCapture->click();
    printSprite(0);
    D("192 original: captured selection into sprite 0");
    nativeSelectCapture->click();
    w.setDrawingTool(Tool::Point);
    nativeAssign->click();
    D("192 original: assigned sprite 0 to slot 0");
    nativeMaxX->setValue(191);
    nativeMaxY->setValue(63);
    nativeMinX->setValue(129);
    D("192 original: slot bounds edited");
    nativeCanvas->click();
    settle();
    D("192 original: sprite canvas");
    std::cout << "sprite create/remove hidden: " << nativeCreateHd->isHidden() << '/' << nativeRemoveHd->isHidden() << '\n';
    nativeCanvas->click();
    settle();
    showTab("Frame");
    showLeft("Frames");
    row(0);
    printPlanes();
    D("192 original: frame page at the end");

    w.m_document.reset();
    w.refreshProjectSummary();
    D("192 original: closed");
}
