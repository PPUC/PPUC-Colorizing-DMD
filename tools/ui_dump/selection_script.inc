// States of the selection, copy and paste, auto copy, the scrolling fill and the recolor and
// dynamic helpers of the Edit menu. Included at the very end of main() in dump.cpp, for a tree
// that has them; all titles carry the prefix "selection: ".
{
    using render::CanvasHit;
    using render::CanvasRegion;
    using render::Resolution;

    // Six 128x32 frames, 4 colors. Frames 0 to 2 share a flat original (shade 1) except pixel
    // 3,3 of frame 2 (shade 2) and pixel 0,0 (shade 0); frame k is colored 0x0100 * (k + 1), frame
    // 0 has a red square at 10..14, 5..8, a dynamic zone of set 2 at 20..22, 4 and HD. Frame 1 has
    // background 0 with a mask over rows 0 to 3. Frames 3 to 5 are a vertical scroll of a
    // striped picture, moving up a row per frame; frames 3 and 5 are colored by picture row.
    const auto makeSelectionSnapshot = []() {
        document::ProjectSnapshot snapshot;
        snapshot.colorCount = 4;
        snapshot.romName = "selection";
        // Rows that do not repeat, so that only the true shift matches.
        const auto pictureShade = [](int x, int row) {
            uint32_t value = static_cast<uint32_t>(x / 4 + 1000) * 2654435761u ^
                             static_cast<uint32_t>(row + 1000) * 2246822519u;
            value ^= value >> 15;
            value *= 2654435761u;
            return static_cast<uint8_t>((value >> 13) % 4);
        };
        for (int i = 0; i < 6; ++i) {
            document::ProjectFrameSnapshot frame{
                document::makeIndexedFrame(render::kSdFrameSize, 1),
                document::makeRgb565Frame(render::kSdFrameSize,
                                          static_cast<uint16_t>(0x0100 * (i + 1))),
                std::nullopt,
                0,
                255,
            };
            frame.originalSd.pixels[0] = 0;
            if (i == 2) {
                frame.originalSd.pixels[3 * 128 + 3] = 2;
            }
            if (i >= 3) {
                for (int y = 0; y < 32; ++y) {
                    for (int x = 0; x < 128; ++x) {
                        frame.originalSd.pixels[static_cast<std::size_t>(y * 128 + x)] =
                            pictureShade(x, y + (i - 3));
                        if (i != 4) {
                            frame.colorizedSd.pixels[static_cast<std::size_t>(y * 128 + x)] =
                                static_cast<uint16_t>(0x2000 + y + (i - 3));
                        }
                    }
                }
            }
            frame.dynamicData = document::FrameDynamicData{
                document::makeIndexedFrame(render::kSdFrameSize, 255),
                document::makeIndexedFrame(render::kHdFrameSize, 255),
                std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0),
                std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0),
            };
            frame.backgroundData = document::FrameBackgroundData{
                0xffff,
                document::makeIndexedFrame(render::kSdFrameSize, 0),
                document::makeIndexedFrame(render::kHdFrameSize, 0),
            };
            frame.durationMs = 40;
            snapshot.frames.push_back(std::move(frame));
        }
        document::ProjectFrameSnapshot& first = snapshot.frames[0];
        first.colorizedHd = document::makeRgb565Frame(render::kHdFrameSize, 0x0841);
        for (int y = 5; y <= 8; ++y) {
            for (int x = 10; x <= 14; ++x) {
                first.colorizedSd.pixels[static_cast<std::size_t>(y * 128 + x)] = 0xf800;
            }
        }
        for (int x = 20; x <= 22; ++x) {
            first.dynamicData->maskSd.pixels[static_cast<std::size_t>(4 * 128 + x)] = 2;
        }
        for (std::size_t c = 0; c < 4; ++c) {
            first.dynamicData->colorsSd[2 * 4 + c] = static_cast<uint16_t>(0x0010 + c);
        }
        snapshot.backgrounds.push_back(document::BackgroundRecord{
            document::makeRgb565Frame(render::kSdFrameSize, 0x07e0),
        });
        snapshot.frames[1].backgroundData->backgroundId = 0;
        for (std::size_t i = 0; i < 4 * 128; ++i) {
            snapshot.frames[1].backgroundData->maskSd.pixels[i] = 1;
        }
        document::PaletteRecord palette;
        palette.colors.assign(document::kPaletteColorCount, 0);
        for (std::size_t c = 0; c < palette.colors.size(); ++c) {
            palette.colors[c] = static_cast<uint16_t>(c * 0x0421);
        }
        snapshot.palettes.push_back(palette);
        for (std::size_t slot = 0; slot < 4; ++slot) {
            snapshot.editorMetadata.editColors[slot] = static_cast<uint16_t>(0x0111 * (slot + 1));
        }
        return snapshot;
    };

    const auto loadSelection = [&]() {
        w.m_document = document::ProjectDocument::fromSnapshot(makeSelectionSnapshot());
        w.m_projectPath = QStringLiteral("/nonexistent/selection.cdmd");
        w.m_sourcePath.clear();
        w.m_undoHistories.clear();
        w.m_pasteMode.clearClipboard();
        w.refreshProjectSummary();
    };
    const auto printSelection = [&]() {
        const edit::PlaneSelection* selection = w.m_canvasEdit.selection();
        std::cout << "selection: sel=";
        if (selection) {
            const render::Rect box = *selection->bounds();
            std::cout << (selection->plane() == Resolution::Hd ? "hd " : "sd ")
                      << selection->count() << " [" << box.x << ',' << box.y << ' ' << box.width
                      << 'x' << box.height << ']';
        } else {
            std::cout << "none";
        }
        const edit::CopyBuffer* clip = w.m_pasteMode.clipboard();
        std::cout << " clip=";
        if (clip) {
            std::cout << clip->selectedCount() << " from f" << clip->sourceFrame << " ["
                      << clip->box.x << ',' << clip->box.y << ' ' << clip->box.width << 'x'
                      << clip->box.height << "] dyn=" << clip->hasDynamicData();
        } else {
            std::cout << "none";
        }
        std::cout << " paste=" << w.m_pasteMode.isActive();
        if (w.m_pasteMode.isActive()) {
            const edit::PastePlacement& p = w.m_pasteMode.placement();
            std::cout << " at " << p.x << ',' << p.y << " mirror " << p.mirrorHorizontally
                      << p.mirrorVertically << " mode " << int(w.m_pasteMode.mode());
        }
        std::cout << " overlay=" << (w.m_preview->m_pasteOverlay
                                         ? int(w.m_preview->m_pasteOverlay->pixels.size())
                                         : -1)
                  << " tracking=" << w.m_preview->hasMouseTracking()
                  << " pending=" << int(w.m_pendingCanvasClick) << '\n';
        if (!w.m_document) {
            return;
        }
        const document::ProjectDocument& d = *w.m_document;
        std::cout << "selection: pixels";
        for (std::size_t f = 0; f < d.frameCount(); ++f) {
            std::cout << " f" << f;
            for (const auto& [x, y] : std::vector<std::pair<int, int>>{
                     {0, 0}, {3, 3}, {4, 3}, {11, 6}, {21, 4}, {41, 21}, {44, 18}, {60, 15},
                     {60, 31}}) {
                std::cout << ' ' << d.colorizedPixel(f, Resolution::Sd, x, y);
            }
            std::cout << " dyn";
            for (const auto& [x, y] : std::vector<std::pair<int, int>>{
                     {21, 4}, {41, 21}, {44, 18}, {65, 13}, {105, 23}}) {
                std::cout << ' ' << int(d.dynamicMaskPixel(f, Resolution::Sd, x, y));
            }
            std::cout << " set2 " << d.dynamicColor(f, Resolution::Sd, 2, 1);
            if (d.hasHdFrame(f)) {
                std::cout << " hd " << d.colorizedPixel(f, Resolution::Hd, 0, 0) << ' '
                          << d.colorizedPixel(f, Resolution::Hd, 22, 12);
            }
        }
        std::cout << '\n';
    };
    const auto D = [&](const char* title) {
        dump(title, w);
        printSelection();
    };
    const auto modalNow = []() -> QDialog* {
        return qobject_cast<QDialog*>(QApplication::activeModalWidget());
    };
    const auto editAction = [&](const char* text) {
        for (QAction* action : w.m_editMenuActions->actions()) {
            if (action->text().remove(QLatin1Char('&')) == QLatin1String(text)) return action;
        }
        std::cerr << "no edit action " << text << '\n';
        std::exit(2);
    };
    const auto selectFrames = [&](std::initializer_list<int> rows) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(*rows.begin());
        for (int r : rows) w.m_frameList->item(r)->setSelected(true);
    };
    const auto hit = [](int x, int y, Resolution plane = Resolution::Sd) {
        return CanvasHit{CanvasRegion::ColorizedFrame, plane, x, y};
    };
    // A click as the canvas reports it, with the modifiers of its press.
    const auto click = [&](CanvasHit at, Qt::KeyboardModifiers modifiers) {
        w.m_preview->m_pressModifiers = modifiers;
        if (w.m_preview->m_pressInterceptor && w.m_preview->m_pressInterceptor(at, modifiers)) {
            w.m_preview->m_pressModifiers = Qt::NoModifier;
            return;
        }
        w.m_preview->m_pixelClickedHandler(at, modifiers.testFlag(Qt::ShiftModifier));
        w.m_preview->m_strokeFinishedHandler(at, at, modifiers.testFlag(Qt::ShiftModifier));
        w.m_preview->m_pressModifiers = Qt::NoModifier;
    };
    const auto drag = [&](CanvasHit from, CanvasHit to, Qt::KeyboardModifiers modifiers) {
        w.m_preview->m_pressModifiers = modifiers;
        w.m_preview->m_pixelClickedHandler(from, modifiers.testFlag(Qt::ShiftModifier));
        w.m_preview->m_strokePreviewHandler(from, to, modifiers.testFlag(Qt::ShiftModifier));
        w.m_preview->m_strokeFinishedHandler(from, to, modifiers.testFlag(Qt::ShiftModifier));
        w.m_preview->m_pressModifiers = Qt::NoModifier;
    };
    const auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers);
        QApplication::sendEvent(w.m_preview, &press);
        QKeyEvent release(QEvent::KeyRelease, code, modifiers);
        QApplication::sendEvent(w.m_preview, &release);
    };
    const auto answerDialog = [&](bool accept, std::function<void(QDialog*)> fill = {}) {
        QTimer::singleShot(0, [accept, fill, modalNow]() {
            QDialog* dialog = modalNow();
            if (!dialog) { std::cerr << "no dialog\n"; std::exit(3); }
            std::cout << "selection: dialog '" << dialog->windowTitle().toStdString() << "'";
            for (QLabel* label : dialog->findChildren<QLabel*>()) {
                std::cout << " | '" << label->text().toStdString() << "'";
            }
            for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>()) {
                std::cout << " | " << button->text().toStdString() << " en=" << button->isEnabled();
            }
            std::cout << '\n';
            if (fill) fill(dialog);
            if (auto* box = qobject_cast<QMessageBox*>(dialog)) {
                std::cout << "selection: question '" << box->informativeText().toStdString()
                          << "'\n";
                box->button(accept ? QMessageBox::Ok : QMessageBox::Cancel)->click();
                return;
            }
            if (accept) dialog->accept(); else dialog->reject();
        });
    };

    loadSelection();
    selectFrames({0});
    D("selection: loaded");
    {
        // Every shortcut of the window, and those two actions share.
        QStringList shortcuts;
        QStringList duplicates;
        for (QAction* action : w.findChildren<QAction*>()) {
            for (const QKeySequence& sequence : action->shortcuts()) {
                const QString text = sequence.toString(QKeySequence::PortableText);
                if (shortcuts.contains(text)) duplicates << text;
                shortcuts << text;
            }
        }
        shortcuts.sort();
        std::cout << "selection: shortcuts " << shortcuts.join(QLatin1Char(' ')).toStdString()
                  << " | shared: " << duplicates.join(QLatin1Char(' ')).toStdString() << '\n';
    }

    // Selecting with Ctrl: a filled rectangle, part of it removed with Shift, a point.
    w.setDrawingTool(edit::DrawingTool::FilledRectangle);
    drag(hit(8, 3), hit(15, 9), Qt::ControlModifier);
    D("selection: ctrl rectangle");
    drag(hit(8, 3), hit(9, 9), Qt::ControlModifier | Qt::ShiftModifier);
    D("selection: ctrl shift rectangle removes");
    w.setDrawingTool(edit::DrawingTool::Point);
    click(hit(21, 4), Qt::ControlModifier);
    D("selection: ctrl point");
    // macOS: the Control key is Meta to Qt; it selects as Cmd does.
    click(hit(22, 4), Qt::MetaModifier);
    D("selection: meta point");
    // The fill tool with Ctrl takes the connected pixels of the color; with W held only
    // non-black ones would join (the square has no black pixel).
    w.setDrawingTool(edit::DrawingTool::FloodFill);
    w.m_canvasEdit.clearSelection(w.canvasEditContext());
    click(hit(11, 6), Qt::ControlModifier);
    D("selection: ctrl fill selects the square");

    // Menu entries.
    editAction("Select All")->trigger();
    D("selection: select all");
    editAction("Invert Selection")->trigger();
    D("selection: invert gives none");
    editAction("Select None")->trigger();
    D("selection: select none");
    click(hit(11, 6), Qt::ControlModifier);
    editAction("Mirror Selection Horizontally")->trigger();
    D("selection: mirrored horizontally");
    editAction("Mirror Selection Vertically")->trigger();
    D("selection: mirrored vertically");
    editAction("Selection From Background Mask")->trigger();
    D("selection: no background mask on frame 0");
    selectFrames({1});
    editAction("Selection From Background Mask")->trigger();
    D("selection: background mask of frame 1");
    answerDialog(true, [](QDialog* dialog) {
        const QList<QSpinBox*> boxes = dialog->findChildren<QSpinBox*>();
        boxes.at(0)->setValue(2);
        boxes.at(1)->setValue(3);
        boxes.at(2)->setValue(5);
        dialog->findChild<QCheckBox*>()->setChecked(true);
    });
    editAction("Auto Select...")->trigger();
    D("selection: auto select columns");

    // Select by color: armed from the menu, then a click; Alt+Ctrl+click with the fill tool.
    selectFrames({0});
    editAction("Select By Color")->trigger();
    D("selection: select by color armed");
    click(hit(11, 6), Qt::NoModifier);
    D("selection: select by color picked the square");
    click(hit(60, 15), Qt::ControlModifier | Qt::AltModifier);
    D("selection: alt ctrl click adds the frame color");
    click(hit(60, 15), Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
    D("selection: alt ctrl shift click removes it");

    // Copy and paste: the square and the dynamic zone.
    w.m_canvasEdit.clearSelection(w.canvasEditContext());
    w.setDrawingTool(edit::DrawingTool::FilledRectangle);
    drag(hit(10, 4), hit(22, 8), Qt::ControlModifier);
    editAction("Copy")->trigger();
    D("selection: copied");
    selectFrames({1, 2});
    editAction("Paste")->trigger();
    D("selection: paste mode");
    w.m_preview->m_hoverHandler(hit(40, 20));
    D("selection: paste follows the mouse");
    key(Qt::Key_Right);
    key(Qt::Key_Down, Qt::ShiftModifier);
    key(Qt::Key_8, Qt::KeypadModifier);
    D("selection: paste nudged");
    key(Qt::Key_K);
    key(Qt::Key_L);
    D("selection: paste mirrored");
    key(Qt::Key_K);
    key(Qt::Key_L);
    key(Qt::Key_Escape);
    D("selection: paste cancelled");
    editAction("Paste")->trigger();
    w.m_preview->m_hoverHandler(hit(40, 20));
    click(hit(40, 20), Qt::NoModifier);
    D("selection: pasted on frames 1 and 2");
    w.m_undoAction->trigger();
    D("selection: undo on frame 1");
    w.m_redoAction->trigger();
    D("selection: redo on frame 1");
    editAction("Paste Where Originals Match")->trigger();
    w.m_preview->m_hoverHandler(hit(9, 5));
    key(Qt::Key_Return);
    D("selection: pasted where the originals match");
    editAction("Paste Dynamic Mask Only")->trigger();
    w.m_preview->m_hoverHandler(hit(60, 15));
    click(hit(60, 15), Qt::NoModifier);
    D("selection: pasted the dynamic mask only");
    editAction("Paste")->trigger();
    click(hit(100, 25), Qt::AltModifier);
    D("selection: alt click pastes the dynamic mask");
    w.m_hdButton->click();
    editAction("Paste")->trigger();
    D("selection: paste refused on HD");
    w.m_sdButton->click();

    // Auto copy: frames 1 and 2 show the same as frame 0 inside the square except pixel 3,3.
    selectFrames({0});
    w.m_canvasEdit.clearSelection(w.canvasEditContext());
    drag(hit(10, 5), hit(14, 8), Qt::ControlModifier);
    answerDialog(true);
    editAction("Auto Copy...")->trigger();
    D("selection: auto copy");
    w.m_canvasEdit.clearSelection(w.canvasEditContext());
    drag(hit(2, 2), hit(4, 4), Qt::ControlModifier);
    answerDialog(false);
    editAction("Auto Copy...")->trigger();
    D("selection: auto copy cancelled, frame 2 differs there");

    // Scrolling fill over frames 3 to 5, the current frame among them.
    selectFrames({4, 3, 5});
    answerDialog(true);
    editAction("Scrolling Auto Fill...")->trigger();
    D("selection: scrolling fill");
    selectFrames({4});
    editAction("Scrolling Auto Fill...")->trigger();
    D("selection: scrolling fill needs a run");

    // Recolor: armed from the menu, and Alt+click with the fill tool.
    selectFrames({1, 2});
    w.setActiveDrawColor(0x001f);
    editAction("Recolor...")->trigger();
    click(hit(100, 30), Qt::NoModifier);
    D("selection: recolor from the menu");
    w.setDrawingTool(edit::DrawingTool::FloodFill);
    selectFrames({0});
    click(hit(21, 4), Qt::AltModifier);
    D("selection: recolor refused on a dynamic pixel");
    click(hit(60, 31), Qt::AltModifier);
    D("selection: alt click recolors");
    w.m_undoAction->trigger();
    D("selection: recolor undone");

    // Black original to black and the dynamic helpers.
    w.m_canvasEdit.clearSelection(w.canvasEditContext());
    editAction("Black Original To Black")->trigger();
    D("selection: black original to black");
    w.m_canvasEdit.clearSelection(w.canvasEditContext());
    w.setDrawingTool(edit::DrawingTool::FilledRectangle);
    drag(hit(40, 20), hit(45, 22), Qt::ControlModifier);
    editAction("Assign Dynamic Set")->trigger();
    D("selection: assign dynamic set to the selection");
    w.m_canvasEdit.clearSelection(w.canvasEditContext());
    editAction("Remove Dynamic Zones")->trigger();
    D("selection: remove the frame's dynamic zones");
    w.m_undoAction->trigger();
    D("selection: removal undone");
    w.m_hdButton->click();
    editAction("Assign Dynamic Set")->trigger();
    D("selection: dynamic helpers refused on HD");
    w.m_sdButton->click();

    editAction("Copy")->trigger();
    D("selection: copy needs a selection");
}
