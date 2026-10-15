// States of the edit colors, the color sets, color set replace drawing, the color picker and
// Edit <-> Dyn. Included at the very end of main() in dump.cpp, for a tree that has them; all
// titles carry the prefix "colors: ".
{
    using render::Resolution;

    // An ordinary project of three 128x32 frames, frame 0 with HD. Shade (x + 2y + frame)
    // mod colors, so every frame has other shades at the same place. Every frame carries
    // dynamic data. Color sets as hook_501 has them: named, active, the rest zero.
    const auto makeColorSnapshot = [](uint32_t colorCount, uint32_t drawColorMode) {
        document::ProjectSnapshot snapshot;
        snapshot.colorCount = colorCount;
        snapshot.romName = colorCount == 4 ? "colors4" : "colors16";
        for (int i = 0; i < 3; ++i) {
            document::ProjectFrameSnapshot frame{
                document::makeIndexedFrame(render::kSdFrameSize, 0),
                document::makeRgb565Frame(render::kSdFrameSize, 0),
                std::nullopt,
                0,
                255,
            };
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 128; ++x) {
                    frame.originalSd.pixels[static_cast<std::size_t>(y * 128 + x)] =
                        static_cast<uint8_t>((x + 2 * y + i) % static_cast<int>(colorCount));
                }
            }
            frame.dynamicData = document::FrameDynamicData{
                document::makeIndexedFrame(render::kSdFrameSize, 255),
                document::makeIndexedFrame(render::kHdFrameSize, 255),
                std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * colorCount, 0),
                std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * colorCount, 0),
            };
            frame.durationMs = 40;
            snapshot.frames.push_back(std::move(frame));
        }
        snapshot.frames[0].colorizedHd = document::makeRgb565Frame(render::kHdFrameSize, 0);
        for (int y = 2; y <= 6; ++y) {
            for (int x = 40; x <= 50; ++x) {
                snapshot.frames[2].colorizedSd.pixels[static_cast<std::size_t>(y * 128 + x)] =
                    0x0f0f;
            }
        }
        snapshot.frames[2].colorizedSd.pixels[20 * 128 + 60] = 0x0abc;
        document::PaletteRecord palette;
        palette.colors.assign(document::kPaletteColorCount, 0);
        for (std::size_t c = 0; c < palette.colors.size(); ++c) {
            palette.colors[c] = static_cast<uint16_t>(c * 0x0421);
        }
        snapshot.palettes.push_back(palette);
        document::EditorMetadata& metadata = snapshot.editorMetadata;
        for (std::size_t slot = 0; slot < colorCount; ++slot) {
            metadata.editColors[slot] =
                static_cast<uint16_t>(slot * (colorCount == 4 ? 0x5140 : 0x1040));
        }
        const uint16_t white[4] = {0x0000, 0x6b4d, 0xb5b6, 0xffff};
        const uint16_t red[4] = {0x0000, 0x8860, 0xc8a0, 0xe8a0};
        for (std::size_t c = 0; c < 4; ++c) {
            metadata.colorSets[c] = white[c];
            metadata.colorSets[16 + c] = red[c];
            metadata.colorSets[32 + c] = 0x001f;
        }
        metadata.activeColorSets[0] = 1;
        metadata.activeColorSets[1] = 1;
        const std::string whiteName = "White";
        std::copy(whiteName.begin(), whiteName.end(), metadata.colorSetNames.begin());
        metadata.drawColorMode = drawColorMode;
        return snapshot;
    };

    const auto loadColors = [&](uint32_t colorCount, uint32_t drawColorMode) {
        w.m_document = document::ProjectDocument::fromSnapshot(
            makeColorSnapshot(colorCount, drawColorMode));
        w.m_currentCromPath = QStringLiteral("/nonexistent/colors.cROM");
        w.m_currentCrpPath = QStringLiteral("/nonexistent/colors.cRP");
        w.m_undoHistories.clear();
        w.refreshProjectSummary();
    };
    const auto printColors = [&]() {
        std::cout << "colors: mode=" << int(w.m_drawColorMode) << " slot="
                  << (w.m_selectedEditColor ? int(*w.m_selectedEditColor) : -1)
                  << " draw=" << w.m_activeDrawColor;
        if (!w.m_document) {
            std::cout << " no project\n";
            return;
        }
        const document::ProjectDocument& d = *w.m_document;
        const document::EditorMetadata& m = d.editorMetadata();
        std::cout << " stored mode=" << m.drawColorMode
                  << " first set=" << int(m.firstDisplayedColorSet)
                  << " current set=" << int(m.currentColorSet) << " dirty=" << d.isDirty()
                  << "\ncolors: edit";
        for (uint16_t color : m.editColors) std::cout << ' ' << color;
        std::cout << "\ncolors: sets";
        const document::ColorSetTables tables = d.colorSetTables();
        for (std::size_t set = 0; set < 4; ++set) {
            std::cout << " [" << set << " active=" << tables.activeColorSets[set] << " '"
                      << edit::colorSetName(tables, set) << "'";
            for (std::size_t c = 0; c < d.colorCount(); ++c) {
                std::cout << ' ' << tables.colorSets[set * 16 + c];
            }
            std::cout << ']';
        }
        std::cout << "\ncolors: pixels";
        for (std::size_t f = 0; f < d.frameCount(); ++f) {
            std::cout << " f" << f << " sd";
            for (int x = 20; x < 24; ++x) {
                std::cout << ' ' << d.colorizedPixel(f, Resolution::Sd, x, 10);
            }
            std::cout << " fill " << d.colorizedPixel(f, Resolution::Sd, 45, 4);
            if (d.hasHdFrame(f)) {
                std::cout << " hd";
                for (int x = 40; x < 44; ++x) {
                    std::cout << ' ' << d.colorizedPixel(f, Resolution::Hd, x, 20);
                }
            }
            std::cout << " dyn0";
            for (std::size_t c = 0; c < d.colorCount(); ++c) {
                std::cout << ' ' << d.dynamicColor(f, Resolution::Sd, 0, c);
            }
        }
        std::cout << '\n';
    };
    const auto D = [&](const char* title) {
        dump(title, w);
        printColors();
    };
    const auto swatch = [&](int slot) {
        auto* button = w.m_editColorBar->findChild<QToolButton*>(
            QStringLiteral("editColorSwatch%1").arg(slot));
        if (!button) { std::cerr << "no swatch " << slot << '\n'; std::exit(2); }
        return button;
    };
    const auto barButton = [&](const char* text) {
        for (QToolButton* button : w.m_editColorBar->findChildren<QToolButton*>()) {
            if (button->text() == QLatin1String(text)) return button;
        }
        std::cerr << "no bar button " << text << '\n';
        std::exit(2);
    };
    const auto setButton = [&](const char* text) {
        for (QToolButton* button : w.m_colorSetPanel->findChildren<QToolButton*>()) {
            if (button->text() == QLatin1String(text)) return button;
        }
        std::cerr << "no set button " << text << '\n';
        std::exit(2);
    };
    auto* setList = w.m_colorSetPanel->findChild<QListWidget*>();
    auto* setName = w.m_colorSetPanel->findChild<QLineEdit*>();
    const auto modal = []() -> QDialog* {
        return qobject_cast<QDialog*>(QApplication::activeModalWidget());
    };
    // Prints the picker's fields, enters hex (if any) and then r5 (if >= 0), prints again, and
    // accepts, or rejects without hex.
    const auto answerPicker = [&](const char* hex, int r5) {
        QTimer::singleShot(0, [hex, r5, modal]() {
            QDialog* dialog = modal();
            if (!dialog || dialog->objectName() != QStringLiteral("colorPickerDialog")) {
                std::cerr << "no color picker\n";
                std::exit(3);
            }
            const auto fields = [dialog]() {
                std::cout << "colors: picker '" << dialog->windowTitle().toStdString() << "' hex="
                          << dialog->findChild<QLineEdit*>(QStringLiteral("colorPickerHex"))
                                 ->text().toStdString();
                for (const char* name :
                     {"colorPickerRed8", "colorPickerGreen8", "colorPickerBlue8",
                      "colorPickerRgb565", "colorPickerRed5", "colorPickerGreen6",
                      "colorPickerBlue5"}) {
                    std::cout << ' ' << dialog->findChild<QSpinBox*>(QLatin1String(name))->value();
                }
                for (const char* name :
                     {"colorPickerHue", "colorPickerSaturation", "colorPickerValue"}) {
                    std::cout << ' ' << dialog->findChild<QDoubleSpinBox*>(QLatin1String(name))
                                            ->text().toStdString();
                }
                std::cout << '\n';
            };
            fields();
            if (!hex) {
                dialog->reject();
                return;
            }
            auto* hexEdit = dialog->findChild<QLineEdit*>(QStringLiteral("colorPickerHex"));
            hexEdit->setText(QLatin1String(hex));
            Q_EMIT hexEdit->textEdited(hexEdit->text());
            if (r5 >= 0) {
                dialog->findChild<QSpinBox*>(QStringLiteral("colorPickerRed5"))->setValue(r5);
            }
            fields();
            dialog->accept();
        });
    };
    UiDumpTabs* inspector = nullptr;
    for (UiDumpTabs* candidate : uiDumpChildren<UiDumpTabs>(w)) {
        for (int i = 0; i < candidate->count(); ++i) {
            if (candidate->tabText(i) == QLatin1String("Sets")) inspector = candidate;
        }
    }
    if (!inspector) { std::cerr << "no Sets tab\n"; return 5; }
    const auto selectFrames = [&](std::initializer_list<int> rows) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(*rows.begin());
        for (int r : rows) w.m_frameList->item(r)->setSelected(true);
    };
    const auto colorized = [](int x, int y, Resolution plane = Resolution::Sd) {
        return render::CanvasHit{render::CanvasRegion::ColorizedFrame, plane, x, y};
    };
    const auto key = [&](int qtKey, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent event(QEvent::KeyPress, qtKey, modifiers);
        QApplication::sendEvent(w.m_preview, &event);
    };

    loadColors(4, 0);
    D("colors: loaded 4 colors");

    swatch(2)->click();
    D("colors: edit color 2 selected");
    byTip<QToolButton>(
        w, "Draw replacing each original shade by its edit color (color set replace)")->click();
    D("colors: set replace mode");

    // Shapes on two frames: each frame's own shades.
    selectFrames({0, 1});
    w.setDrawingTool(edit::DrawingTool::FilledRectangle);
    w.m_preview->m_strokeFinishedHandler(colorized(18, 8), colorized(25, 12), false);
    D("colors: set replace rectangle on frames 0 and 1");
    w.m_undoAction->trigger();
    D("colors: rectangle undone on frame 0");
    w.m_redoAction->trigger();
    D("colors: rectangle redone");

    // The HD plane takes the shade of the ROM pixel under each HD pixel.
    selectFrames({0});
    w.m_hdButton->click();
    w.setDrawingTool(edit::DrawingTool::Line);
    w.m_preview->m_strokeFinishedHandler(colorized(38, 20, Resolution::Hd),
                                         colorized(45, 20, Resolution::Hd), false);
    D("colors: set replace line on HD");
    w.m_undoAction->trigger();
    D("colors: HD line undone");
    w.m_sdButton->click();

    // Flood fill: the region of frame 2's clicked color, frame 1 colored by its own shades.
    selectFrames({2, 1});
    w.setDrawingTool(edit::DrawingTool::FloodFill);
    w.m_preview->m_pixelClickedHandler(colorized(45, 4), false);
    D("colors: set replace flood fill on frames 2 and 1");

    // Pipette: without Shift the draw color, with Shift the edit color slot of the color.
    selectFrames({0});
    w.setDrawingTool(edit::DrawingTool::ColorPicker);
    w.m_preview->m_pixelClickedHandler(colorized(21, 10), true);
    D("colors: pipette with Shift selects the slot");
    selectFrames({2});
    w.m_preview->m_pixelClickedHandler(colorized(60, 20), true);
    D("colors: pipette with Shift on a color no slot has");
    selectFrames({0});
    w.m_preview->m_pixelClickedHandler(colorized(21, 10), false);
    D("colors: pipette without Shift");

    byTip<QToolButton>(w, "Draw with a single color")->click();
    D("colors: single color mode");

    // Edit colors: a dropped color, the picker, undo.
    swatch(1)->click();
    w.setEditColor(1, 0x07e0);
    D("colors: edit color 1 dropped, draw color follows");
    w.m_undoAction->trigger();
    D("colors: edit color undone");
    answerPicker("0000FF", 20);
    w.editEditColor(3);
    D("colors: edit color 3 from the picker");
    answerPicker(nullptr, -1);
    w.editEditColor(3);
    D("colors: picker cancelled");

    // The keys: 1-4 select slots; letters only in a 16-color project.
    w.m_preview->setFocus();
    key(Qt::Key_4);
    D("colors: key 4");
    key(Qt::Key_Q);
    D("colors: key Q ignored with 4 colors");

    // Color sets.
    for (int i = 0; i < inspector->count(); ++i) {
        if (inspector->tabText(i) == QLatin1String("Sets")) inspector->setCurrentIndex(i);
    }
    D("colors: sets page");
    setList->setCurrentRow(2);
    D("colors: inactive set 2 selected");
    setButton("Store")->click();
    D("colors: edit colors stored in set 2");
    setName->setText(QStringLiteral("Probe"));
    Q_EMIT setName->editingFinished();
    D("colors: set 2 renamed");
    setList->setCurrentRow(1);
    setButton("Load")->click();
    D("colors: set 1 loaded");
    setButton("Deactivate")->click();
    D("colors: set 1 deactivated");
    w.m_undoAction->trigger();
    w.m_undoAction->trigger();
    D("colors: deactivation and load undone");
    setList->scrollToItem(setList->item(40), QAbstractItemView::PositionAtTop);
    D("colors: set list scrolled by a program, not stored");
    setList->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub);
    D("colors: set list scrolled by the user");

    // Edit -> Dyn into dynamic set 0 of the selected frames, Dyn -> Edit from the current one.
    selectFrames({0, 1, 2});
    barButton("Edit > Dyn")->click();
    D("colors: Edit to Dyn on 3 frames");
    w.m_undoAction->trigger();
    D("colors: Edit to Dyn undone on the current frame");
    w.setEditColor(0, 0x1234);
    selectFrames({1});
    barButton("Dyn > Edit")->click();
    D("colors: Dyn to Edit from frame 1");
    w.m_undoAction->trigger();
    D("colors: Dyn to Edit undone");

    // A 16-color project saved in color set replace mode opens in that mode.
    loadColors(16, 1);
    D("colors: loaded 16 colors in set replace mode");
    key(Qt::Key_V);
    D("colors: key V selects slot 15");
    key(Qt::Key_A, Qt::ShiftModifier);
    D("colors: Shift A selects nothing");
    selectFrames({1, 2});
    w.setDrawingTool(edit::DrawingTool::FilledRectangle);
    w.m_preview->m_strokeFinishedHandler(colorized(18, 8), colorized(25, 12), false);
    D("colors: 16 color set replace rectangle");
    w.m_undoAction->trigger();
    D("colors: 16 color rectangle undone on frame 1");
    for (int i = 0; i < inspector->count(); ++i) {
        if (inspector->tabText(i) == QLatin1String("Sets")) inspector->setCurrentIndex(i);
    }
    setList->setCurrentRow(3);
    setButton("Store")->click();
    D("colors: 16 edit colors stored in set 3");
    setList->setCurrentRow(0);
    setButton("Load")->click();
    D("colors: 4 color set 0 loaded into 16 edit colors");
    byTip<QToolButton>(w, "Draw with a single color")->click();
    D("colors: 16 colors single color mode");

    // The palette page's colors open the picker now.
    answerPicker("00FF00", -1);
    w.editCurrentPaletteColor(5);
    D("colors: palette color from the picker");

    w.m_document.reset();
    w.refreshProjectSummary();
    D("colors: closed");
}
