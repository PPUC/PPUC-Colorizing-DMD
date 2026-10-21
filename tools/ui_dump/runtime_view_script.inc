// The Runtime view: the canvas shows libserum's output for the current frame. Included at the
// very end of main() in dump.cpp, after native_original_script.inc closed its project, so that
// no earlier state depends on it. The view is switched on and off through its menu actions,
// which are not widgets and so do not show in the dumps of the earlier states. Everything
// printed here comes after a dump of its own: a line printed before the first one would land
// in the last section of the scripts before.
//
// Three projects, one per plane layout:
//   128x32:  frame 0 has a sprite whose search area fits it exactly and a dynamic zone with a
//            shadow; frame 1 has an HD frame; frame 2 is plain
//   256x64 original: frame 1 has no 256x64 data, so libserum shows nothing for it
//   192x64 original: two plain frames, one with a dynamic zone beyond column 127
{
    using render::PlaneLayout;
    using render::Resolution;

    const auto D = [&](const char* title) { dump(title, w); };
    const auto row = [&](int r) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(r);
    };
    const auto makeProject = [](PlaneLayout layout, int frameCount) {
        document::ProjectSnapshot snapshot;
        snapshot.colorCount = 4;
        snapshot.romName = "runtime_view";
        snapshot.geometry = render::ProjectGeometry(layout);
        const render::Size rom = snapshot.geometry.romFrameSize();
        const render::Size sd = snapshot.geometry.planeSize(Resolution::Sd);
        const render::Size hd = snapshot.geometry.planeSize(Resolution::Hd);
        for (int f = 0; f < frameCount; ++f) {
            document::ProjectFrameSnapshot frame;
            frame.originalSd = document::makeIndexedFrame(rom, 0);
            for (int y = 0; y < rom.height; ++y) {
                for (int x = 0; x < rom.width; ++x) {
                    frame.originalSd.pixels[static_cast<std::size_t>(y) * rom.width + x] =
                        static_cast<uint8_t>(((x / 5 + y / 3 + f) % 7 == 0) ? 1 + (x + f) % 3 : 0);
                }
            }
            frame.colorizedSd = document::makeRgb565Frame(sd, 0);
            for (std::size_t i = 0; i < frame.colorizedSd.pixels.size(); ++i) {
                frame.colorizedSd.pixels[i] =
                    static_cast<uint16_t>(((i % sd.width) / 8 * 1723 + (i / sd.width) / 4 * 911 +
                                           (f + 1) * 3301) & 0xffff);
            }
            if (layout == PlaneLayout::NativeExtra) {
                frame.colorizedHd = document::makeRgb565Frame(hd, 0);
                for (std::size_t i = 0; i < frame.colorizedHd->pixels.size(); ++i) {
                    frame.colorizedHd->pixels[i] =
                        static_cast<uint16_t>(((i % hd.width) / 8 * 977 + (i / hd.width) / 4 * 613 +
                                               (f + 1) * 2203) & 0xffff);
                }
            }
            frame.comparisonMaskId = 255;
            frame.triggerId = document::kNoTriggerId;
            frame.durationMs = 100;
            snapshot.frames.push_back(frame);
        }
        document::PaletteRecord palette;
        palette.colors.assign(document::kPaletteColorCount, 0x1357);
        snapshot.palettes = {palette};
        return snapshot;
    };
    const auto addDynamicZone = [](document::ProjectSnapshot& snapshot, std::size_t f, int x0,
                                   int y0, int x1, int y1) {
        const bool extra = snapshot.geometry.nativePlane() == Resolution::Hd;
        document::FrameDynamicData dyn{
            document::makeIndexedFrame(snapshot.geometry.planeSize(Resolution::Sd), 255),
            document::makeIndexedFrame(snapshot.geometry.planeSize(Resolution::Hd), 255),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0),
        };
        document::IndexedFrame& mask = extra ? dyn.maskHd : dyn.maskSd;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                mask.pixels[static_cast<std::size_t>(y) * mask.size.width + x] = 2;
            }
        }
        for (std::size_t c = 0; c < dyn.colorsSd.size(); ++c) {
            dyn.colorsSd[c] = static_cast<uint16_t>((c * 2731 + 97) & 0xffff);
            dyn.colorsHd[c] = static_cast<uint16_t>((c * 1321 + 4441) & 0xffff);
        }
        dyn.shadowDirectionsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
        dyn.shadowColorsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
        dyn.shadowDirectionsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
        dyn.shadowColorsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
        (extra ? dyn.shadowDirectionsHd : dyn.shadowDirectionsSd)[2] = 0x10;
        (extra ? dyn.shadowColorsHd : dyn.shadowColorsSd)[2] = 0xf81f;
        snapshot.frames[f].dynamicData = dyn;
    };
    const auto load = [&](const document::ProjectSnapshot& snapshot) {
        std::string error;
        if (!document::isValidProjectSnapshot(snapshot, &error)) {
            std::cerr << "invalid runtime view snapshot: " << error << '\n';
            std::exit(8);
        }
        w.m_undoHistories.clear();
        w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
        w.refreshProjectSummary();
    };
    // What the dump of the widgets does not show: the view's own state and the menu actions.
    const auto printView = [&]() {
        const app::RuntimeView& view = w.m_runtimeView;
        std::cout << "runtime view: on=" << view.isEnabled() << " playing=" << view.isPlaying()
                  << " rows=" << (view.rows() == runtime::PlaneRequest::Rows64 ? 64 : 32)
                  << " loaded=" << runtime::RuntimePreview::instance().isOpen()
                  << " action=" << w.m_runtimeViewAction->isChecked()
                  << " rows32=" << w.m_runtimeRows32Action->isVisible()
                  << w.m_runtimeRows32Action->isEnabled() << w.m_runtimeRows32Action->isChecked()
                  << " rows64=" << w.m_runtimeRows64Action->isVisible()
                  << w.m_runtimeRows64Action->isEnabled() << w.m_runtimeRows64Action->isChecked()
                  << " rotationTimer=" << w.m_runtimeRotationTimer->isActive()
                  << " status='" << w.m_frameStatusLabel->text().toStdString() << "'";
        if (w.m_document && w.currentFrameIndex() && view.isEnabled()) {
            std::cout << " message='"
                      << w.m_runtimeView.message(*w.m_document, *w.currentFrameIndex()) << "'";
        }
        std::cout << '\n';
    };
    // Without a picture there is no dialog to answer, only a status message.
    const auto exportRuntime = [&](const char* label, bool expectDialog = true) {
        const QString out = QDir::temp().filePath(QStringLiteral("colorizingdmd_ui_dump_runtime.png"));
        QFile::remove(out);
        if (expectDialog) QTimer::singleShot(0, [out]() {
            QFileDialog* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) {
                for (QWidget* top : QApplication::topLevelWidgets()) {
                    if (auto* candidate = qobject_cast<QFileDialog*>(top); candidate && candidate->isVisible()) dialog = candidate;
                }
            }
            if (!dialog) { std::cerr << "no file dialog\n"; std::exit(8); }
            std::cout << "file dialog: " << dialog->windowTitle().toStdString() << " | default name "
                      << QFileInfo(dialog->selectedFiles().value(0)).fileName().toStdString() << '\n';
            dialog->selectFile(out);
            static_cast<QDialog*>(dialog)->accept();
        });
        // File -> Export Current Frame PNG..., which writes libserum's output.
        w.exportCurrentFramePng();
        const QImage image = QImage(out).convertToFormat(QImage::Format_ARGB32);
        std::cout << label << ": " << image.width() << 'x' << image.height() << ':'
                  << (image.isNull() ? 0 : qHashBits(image.constBits(), static_cast<size_t>(image.sizeInBytes())))
                  << " | status: " << w.statusBar()->currentMessage().toStdString() << '\n';
        QFile::remove(out);
    };

    // --- 128x32 ---------------------------------------------------------------------------
    document::ProjectSnapshot rs = makeProject(PlaneLayout::Ordinary, 3);
    {
        // Sprite 0: 6x4, found by its second row; its search area in frame 0 fits it exactly.
        static const uint8_t pixels[4][6] = {{1, 1, 2, 3, 3, 1}, {2, 1, 2, 3, 1, 3},
                                             {3, 3, 255, 1, 2, 2}, {1, 2, 1, 1, 3, 255}};
        document::SpriteRecord sprite;
        sprite.name = "exact";
        sprite.originalSd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedSd = document::makeRgb565Frame({256, 64}, 0);
        sprite.maskHd = document::makeIndexedFrame({256, 64}, 255);
        sprite.colorizedHd = document::makeRgb565Frame({256, 64}, 0);
        sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
        sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
        sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, document::SpriteDetectionArea{});
        document::IndexedFrame& original = rs.frames[0].originalSd;
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 6; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * 256 + x;
                if (pixels[y][x] != 255) {
                    sprite.originalSd.pixels[index] = pixels[y][x];
                    sprite.colorizedSd.pixels[index] = static_cast<uint16_t>(0x07e0 + y * 64 + x);
                }
                original.pixels[static_cast<std::size_t>(20 + y) * 128 + 100 + x] =
                    pixels[y][x] == 255 ? uint8_t{3} : pixels[y][x];
            }
        }
        sprite.detectionAreas[0] = document::SpriteDetectionArea{0, 0, 6, 4};
        sprite.detectionDwords[0] = 1u | (2u << 8) | (3u << 16) | (1u << 24);
        sprite.detectionDwordPositions[0] = 1 * 256 + 1;
        rs.sprites.push_back(sprite);
        rs.frames[0].spriteReferences.assign(document::kLegacySpritesPerFrame,
                                             document::FrameSpriteReference{255, 0, 0, 127, 31});
        rs.frames[0].spriteReferences[0] = document::FrameSpriteReference{0, 100, 20, 105, 23};
        addDynamicZone(rs, 0, 8, 4, 60, 18);
        rs.frames[1].colorizedHd = document::makeRgb565Frame({256, 64}, 0);
        for (std::size_t i = 0; i < rs.frames[1].colorizedHd->pixels.size(); ++i) {
            rs.frames[1].colorizedHd->pixels[i] =
                static_cast<uint16_t>(((i % 256) / 4 * 419 + (i / 256) / 2 * 733 + 5501) & 0xffff);
        }
    }
    load(rs);
    // Whatever the scripts before left on: ordinary frame editing, so that undo is in scope.
    for (QToolButton* toggle : {w.m_comparisonMaskOverlayButton, w.m_dynamicMaskOverlayButton,
                                w.m_backgroundMaskOverlayButton}) {
        if (toggle->isChecked()) toggle->click();
    }
    row(0);
    D("runtime view: 128x32 project, view off");
    printView();

    w.m_runtimeViewAction->trigger();
    D("runtime view: on, 32 rows, frame with sprite and dynamic zone");
    printView();
    w.m_runtimeRows64Action->trigger();
    D("runtime view: 64 rows, frame without HD upscaled");
    printView();
    row(1);
    D("runtime view: 64 rows, frame with HD");
    printView();
    w.m_runtimeRows32Action->trigger();
    D("runtime view: 32 rows, frame with HD");
    printView();
    exportRuntime("exported runtime 32 rows");
    w.m_runtimeRows64Action->trigger();
    exportRuntime("exported runtime 64 rows");
    D("runtime view: after exports");

    // An edit shows at once, and so does its undo.
    row(0);
    w.m_runtimeRows32Action->trigger();
    D("runtime view: before an edit");
    w.commitPixelChanges(edit::EditLayer{edit::EditKind::FramePixels, 0, Resolution::Sd},
                         {edit::PixelChange{70, 25, w.m_document->colorizedPixel(0, Resolution::Sd, 70, 25), 0xf800},
                          edit::PixelChange{71, 25, w.m_document->colorizedPixel(0, Resolution::Sd, 71, 25), 0xf800}});
    w.updateUndoRedoActions();
    D("runtime view: after a pixel edit");
    printView();
    w.m_undoAction->trigger();
    D("runtime view: after undo");
    w.m_redoAction->trigger();
    D("runtime view: after redo");
    // The canvas takes no strokes while the view is on: a real press and release on a pixel
    // of the picture.
    {
        QPoint target(-1, -1);
        for (int y = 0; y < w.m_preview->height() && target.x() < 0; ++y) {
            for (int x = 0; x < w.m_preview->width(); ++x) {
                const auto hit = w.m_preview->hitTestAt(QPoint(x, y));
                if (hit && hit->region == render::CanvasRegion::ColorizedFrame && hit->x == 40 && hit->y == 12) {
                    target = QPoint(x, y);
                    break;
                }
            }
        }
        std::cout << "canvas pixel found: " << (target.x() >= 0) << '\n';
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(target), QPointF(target), QPointF(target),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.m_preview, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(target), QPointF(target), QPointF(target),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(w.m_preview, &release);
    }
    D("runtime view: a click on the canvas changes nothing");

    // Playback through libserum: one loaded sequence, fed frame by frame.
    w.m_playbackIntervalSpinBox->setValue(5000);
    w.m_playbackButton->click();
    D("runtime view: playing");
    printView();
    w.advancePlaybackFrame();
    D("runtime view: playback step");
    printView();
    w.m_playbackButton->click();
    D("runtime view: playback stopped");
    printView();
    w.m_playbackIntervalSpinBox->setValue(100);

    w.m_runtimeViewAction->trigger();
    D("runtime view: off again");
    printView();
    exportRuntime("exported runtime with the view off, SD mode");

    // Opening the sprite canvas ends the view.
    row(0);
    w.m_runtimeViewAction->trigger();
    QToolButton* runtimeSpriteCanvas = byTip<QToolButton>(w, "Show selected sprite in the main canvas");
    runtimeSpriteCanvas->click();
    for (int round = 0; round < 5; ++round) QApplication::processEvents();
    D("runtime view: sprite canvas opened, view off");
    printView();
    runtimeSpriteCanvas->click();
    for (int round = 0; round < 5; ++round) QApplication::processEvents();
    D("runtime view: sprite canvas closed");

    // --- 256x64 original ------------------------------------------------------------------
    w.m_runtimeViewAction->trigger();
    document::ProjectSnapshot rb = makeProject(PlaneLayout::NativeExtra, 3);
    rb.frames[1].colorizedHd.reset();
    addDynamicZone(rb, 0, 140, 36, 220, 60);
    load(rb);
    row(0);
    D("runtime view: 256x64 original, view stays on");
    printView();
    row(1);
    D("runtime view: 256x64 original, frame without 256x64 data");
    printView();
    exportRuntime("exported runtime without picture", false);
    row(2);
    w.m_playbackIntervalSpinBox->setValue(5000);
    w.m_playbackButton->click();
    w.advancePlaybackFrame();
    D("runtime view: 256x64 original, playback step from the last frame");
    printView();
    w.m_playbackIntervalSpinBox->setValue(100);

    // --- 192x64 original ------------------------------------------------------------------
    document::ProjectSnapshot rn = makeProject(PlaneLayout::NativeOriginal, 2);
    addDynamicZone(rn, 1, 130, 30, 190, 62);
    load(rn);
    row(1);
    D("runtime view: 192x64 original");
    printView();
    w.m_runtimeViewAction->trigger();
    D("runtime view: 192x64 original, view off");
    printView();

    w.m_document.reset();
    w.refreshProjectSummary();
    D("runtime view: closed");
    printView();

    // --- appended: inactive frames and frame identification by order ----------------------
    // Five 128x32 frames. Frame 1 is shorter than 15 ms and has the plain colors of its
    // original: it is saved as inactive and libserum skips it. Frames 2 and 3 have the
    // original of frame 0, so one ROM frame matches three frames, and which of them
    // libserum identifies depends on the frame it identified before.
    document::ProjectSnapshot ri = makeProject(PlaneLayout::Ordinary, 5);
    {
        const std::vector<uint16_t> plain = document::plainOriginalColors(ri.colorCount);
        for (std::size_t i = 0; i < ri.frames[1].colorizedSd.pixels.size(); ++i) {
            ri.frames[1].colorizedSd.pixels[i] = plain[ri.frames[1].originalSd.pixels[i]];
        }
        ri.frames[1].durationMs = 5;
        ri.frames[2].originalSd = ri.frames[0].originalSd;
        ri.frames[3].originalSd = ri.frames[0].originalSd;
    }
    load(ri);
    row(0);
    D("identification: project with an inactive frame and shared originals");
    printView();
    row(1);
    D("identification: inactive frame selected");
    std::cout << "frame list item: '" << w.m_frameList->item(1)->text().replace('\n', '|').toStdString()
              << "' tip '" << w.m_frameList->item(1)->toolTip().toStdString() << "' | status tip '"
              << w.m_frameStatusLabel->toolTip().toStdString() << "'\n";
    {
        const int inactiveFilter = w.m_frameFilterComboBox->findText(QStringLiteral("Inactive"));
        std::cout << "inactive filter entry: " << inactiveFilter << '\n';
        w.m_frameFilterComboBox->setCurrentIndex(inactiveFilter);
        D("identification: filter Inactive");
        // Colorized, the frame is active and leaves the filter.
        w.commitPixelChanges(edit::EditLayer{edit::EditKind::FramePixels, 1, Resolution::Sd},
                             {edit::PixelChange{3, 3, w.m_document->colorizedPixel(1, Resolution::Sd, 3, 3), 0xf800}});
        w.updateUndoRedoActions();
        D("identification: the inactive frame colorized leaves the filter");
        w.m_frameFilterComboBox->setCurrentIndex(0);
        row(1);
        w.m_undoAction->trigger();
        D("identification: inactive again after undo");
    }
    w.m_runtimeViewAction->trigger();
    D("identification: runtime view on the inactive frame");
    printView();
    exportRuntime("export of the inactive frame", false);
    // A still shows the frame rendered on its own. Frame 3's ROM frame is also frame 2's,
    // which a machine coming from frame 2 identifies it as.
    row(3);
    D("identification: still of a frame that shares its original with the frame before");
    printView();
    row(2);
    D("identification: still of the first of them");
    printView();
    // Playback from frame 2: libserum's search starts at frame 0, which has that original.
    w.m_playbackIntervalSpinBox->setValue(5000);
    w.m_playbackButton->click();
    D("identification: playing from frame 2, identified as frame 0");
    printView();
    w.advancePlaybackFrame();
    D("identification: playback step to frame 3");
    printView();
    w.advancePlaybackFrame();
    D("identification: playback step to frame 4");
    printView();
    w.advancePlaybackFrame();
    w.advancePlaybackFrame();
    D("identification: playback wrapped to the inactive frame");
    printView();
    w.m_playbackButton->click();
    w.m_playbackIntervalSpinBox->setValue(100);
    w.m_runtimeViewAction->trigger();
    D("identification: view off");
    printView();
    w.m_document.reset();
    w.refreshProjectSummary();
    D("identification: closed");
}
