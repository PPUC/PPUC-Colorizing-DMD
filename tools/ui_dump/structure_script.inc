// Adding and deleting records: frames (Edit menu and frame list menu), comparison masks,
// backgrounds and sprites (New and Delete above the lists of the left browser), each with
// its question, undo and redo. Included at the very end of main() in dump.cpp, after
// runtime_view_script.inc closed its project, so that no earlier state depends on it; all
// titles carry the prefix "structure: ". Everything printed here comes after a dump of its
// own. After each dump the script prints what the dump does not hold: the record counts,
// each frame's references, the sections, the sprite tables of the cRP and the selections.
//
// Three projects, one per plane layout:
//   128x32:  six frames, two of them shorter than 15 ms, three masks, backgrounds and sprites
//            in use, two sections
//   256x64 original: three frames, frame 1 without its 256x64 data
//   192x64 original: three frames
{
    using render::PlaneLayout;
    using render::Resolution;

    const auto printStructure = [&]() {
        if (!w.m_document) {
            std::cout << "structure: no document\n";
            return;
        }
        const document::ProjectDocument& d = *w.m_document;
        const document::EditorMetadata& metadata = d.editorMetadata();
        std::cout << "structure: " << d.frameCount() << " frames " << d.comparisonMaskCount()
                  << " masks " << d.backgroundCount() << " backgrounds " << d.spriteCount()
                  << " sprites dirty=" << d.isDirty() << '\n' << "structure frames:";
        for (std::size_t f = 0; f < d.frameCount(); ++f) {
            std::cout << " f" << f << "[trig=" << d.pupTriggerId(f) << " sprites=";
            for (const document::FrameSpriteReference& reference : d.frameSpriteReferences(f)) {
                std::cout << int(reference.spriteId) << ':' << reference.minX << ',';
            }
            std::cout << ']';
        }
        std::cout << '\n' << "structure sections:";
        for (std::size_t s = 0; s < metadata.sectionCount; ++s) {
            std::cout << ' ' << metadata.sectionFirstFrames[s] << '='
                      << document::fixedName(metadata.sectionNames, s, document::kSectionNameBytes);
        }
        std::cout << '\n' << "structure sprites:";
        for (std::size_t s = 0; s < d.spriteCount(); ++s) {
            const document::SpriteRecord& sprite = d.sprite(s);
            std::cout << " s" << s << "[" << sprite.name << " hd=" << sprite.hasHd << " from="
                      << metadata.spriteSourceFrames[s] << " rect="
                      << metadata.spriteSourceRects[s * 4] << " table="
                      << document::fixedName(metadata.spriteNames, s, document::kSpriteNameBytes)
                      << ']';
        }
        std::cout << '\n' << "structure backgrounds:";
        for (std::size_t b = 0; b < d.backgroundCount(); ++b) {
            std::cout << " b" << b << "[extra=" << d.background(b).colorizedHd.has_value()
                      << " used=" << d.backgroundUseCount(b) << ']';
        }
        std::cout << '\n' << "structure selection: mask="
                  << (w.m_selectedMaskIndex ? int(*w.m_selectedMaskIndex) : -1)
                  << " maskRow=" << w.m_maskList->currentRow()
                  << " backgroundRow=" << w.m_backgroundList->currentRow()
                  << " spriteRow=" << w.m_spriteList->currentRow()
                  << " filter=" << w.m_frameFilterComboBox->currentText().toStdString()
                  << " imported=" << metadata.importedPalettes.size() << '\n'
                  << "structure frame list menu:";
        for (const QAction* action : w.m_frameList->actions()) {
            std::cout << " '" << action->text().toStdString() << "' en=" << action->isEnabled()
                      << " keys=" << QKeySequence::listToString(action->shortcuts())
                                         .toStdString();
        }
        std::cout << '\n';
    };
    const auto D = [&](const char* title) {
        dump(title, w);
        printStructure();
    };
    const auto row = [&](int r) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(r);
    };
    const auto selectRows = [&](std::initializer_list<int> rows) {
        w.m_frameList->clearSelection();
        w.m_frameList->setCurrentRow(*rows.begin());
        for (int r : rows) w.m_frameList->item(r)->setSelected(true);
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
        std::exit(5);
    };
    const auto undo = [&](const char* title) { w.m_undoAction->trigger(); D(title); };
    const auto redo = [&](const char* title) { w.m_redoAction->trigger(); D(title); };
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
    const auto answerMessageBox = [modalDialog](const QString& buttonText) {
        QTimer::singleShot(0, [buttonText, modalDialog]() {
            auto* box = qobject_cast<QMessageBox*>(modalDialog());
            if (!box) { std::cerr << "no message box\n"; std::exit(3); }
            std::cout << "modal: " << box->windowTitle().toStdString() << " | "
                      << box->text().toStdString() << " | "
                      << box->informativeText().toStdString() << " | default "
                      << (box->defaultButton() ? box->defaultButton()->text().toStdString()
                                               : std::string("none"))
                      << '\n';
            for (QAbstractButton* button : box->buttons()) {
                if (button->text() == buttonText) { button->click(); return; }
            }
            std::cerr << "no button " << buttonText.toStdString() << '\n';
            std::exit(3);
        });
    };
    // A text or a number accepts the dialog with that value, neither cancels it.
    // messageBoxButton answers the question that follows.
    const auto answerInputDialog = [&](std::optional<QString> text, std::optional<int> number,
                                       const QString& messageBoxButton = QString()) {
        QTimer::singleShot(0, [text, number, messageBoxButton, modalDialog, answerMessageBox]() {
            auto* dialog = qobject_cast<QInputDialog*>(modalDialog());
            if (!dialog) { std::cerr << "no input dialog\n"; std::exit(3); }
            std::cout << "input dialog: " << dialog->windowTitle().toStdString() << " | "
                      << dialog->labelText().toStdString() << " | text '"
                      << dialog->textValue().toStdString() << "' number " << dialog->intValue()
                      << " [" << dialog->intMinimum() << ".." << dialog->intMaximum() << "]\n";
            if (!text && !number) {
                dialog->reject();
                return;
            }
            if (text) dialog->setTextValue(*text);
            if (number) dialog->setIntValue(*number);
            if (!messageBoxButton.isEmpty()) answerMessageBox(messageBoxButton);
            dialog->accept();
        });
    };
    // For calls that are expected to return without a dialog.
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
    const auto deleteFrames = [&](const QString& button) {
        answerMessageBox(button);
        w.m_deleteFramesAction->trigger();
    };
    const auto clickAnswered = [&](const char* tip, const QString& button) {
        answerMessageBox(button);
        byTip<QToolButton>(w, tip)->click();
    };

    const auto makeProject = [](PlaneLayout layout, int frameCount) {
        document::ProjectSnapshot snapshot;
        snapshot.colorCount = 4;
        snapshot.romName = "structure";
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
            frame.backgroundData = document::FrameBackgroundData{
                0xffff, document::makeIndexedFrame(sd, 1), document::makeIndexedFrame(hd, 1)};
            frame.pupTriggerId = static_cast<uint32_t>(500 + f);
            frame.durationMs = 100;
            snapshot.frames.push_back(frame);
        }
        document::PaletteRecord palette;
        palette.colors.assign(document::kPaletteColorCount, 0x1357);
        snapshot.palettes = {palette};
        return snapshot;
    };
    const auto addSprite = [](document::ProjectSnapshot& snapshot, const char* name) {
        const render::Size size{256, 64};
        document::SpriteRecord sprite;
        sprite.hasHd = snapshot.geometry.layout() == PlaneLayout::NativeExtra;
        sprite.originalSd = document::makeIndexedFrame(size, 255);
        sprite.colorizedSd = document::makeRgb565Frame(size, 0);
        sprite.maskHd = document::makeIndexedFrame(size, 255);
        sprite.colorizedHd = document::makeRgb565Frame(size, 0);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 6; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * 256 + x;
                sprite.originalSd.pixels[index] = static_cast<uint8_t>(1 + (x + y) % 3);
                const auto color =
                    static_cast<uint16_t>(0x4000 + snapshot.sprites.size() * 0x800 + y * 16 + x);
                sprite.colorizedSd.pixels[index] = color;
                if (sprite.hasHd) {
                    sprite.maskHd.pixels[index] = sprite.originalSd.pixels[index];
                    sprite.colorizedHd.pixels[index] = color;
                }
            }
        }
        sprite.detectionDwords.assign(4, 0xffffffff);
        sprite.detectionDwordPositions.assign(4, 0xffff);
        sprite.detectionAreas.assign(4, document::SpriteDetectionArea{});
        sprite.name = name;
        snapshot.sprites.push_back(sprite);
    };
    const auto addBackground = [](document::ProjectSnapshot& snapshot, uint16_t color) {
        document::BackgroundRecord background;
        background.colorizedSd =
            document::makeRgb565Frame(snapshot.geometry.planeSize(Resolution::Sd), color);
        if (snapshot.geometry.layout() == PlaneLayout::NativeExtra) {
            background.colorizedHd =
                document::makeRgb565Frame(snapshot.geometry.planeSize(Resolution::Hd), color);
        }
        snapshot.backgrounds.push_back(background);
    };
    const auto slot = [](document::ProjectSnapshot& snapshot, std::size_t f, std::size_t s,
                         uint8_t sprite, uint16_t minX) {
        snapshot.frames[f].spriteReferences.resize(32);
        snapshot.frames[f].spriteReferences[s] =
            document::FrameSpriteReference{sprite, minX, 0, static_cast<uint16_t>(minX + 20), 20};
    };
    const auto load = [&](const document::ProjectSnapshot& snapshot) {
        w.m_undoHistories.clear();
        w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
        w.refreshProjectSummary();
        w.updateUndoRedoActions();
    };

    document::ProjectSnapshot ordinary = makeProject(PlaneLayout::Ordinary, 6);
    ordinary.frames[1].durationMs = 10;
    ordinary.frames[3].durationMs = 12;
    for (int m = 0; m < 3; ++m) {
        ordinary.comparisonMasks.push_back(
            document::makeIndexedFrame(render::kSdFrameSize, 0));
        ordinary.comparisonMasks.back().pixels[static_cast<std::size_t>(m)] = 1;
        addBackground(ordinary, static_cast<uint16_t>(0x0841 * (m + 3)));
    }
    ordinary.frames[1].comparisonMaskId = 1;
    ordinary.frames[4].comparisonMaskId = 2;
    ordinary.frames[5].comparisonMaskId = 2;
    ordinary.frames[2].backgroundData->backgroundId = 1;
    ordinary.frames[5].backgroundData->backgroundId = 2;
    addSprite(ordinary, "ball");
    addSprite(ordinary, "flipper");
    addSprite(ordinary, "ramp");
    slot(ordinary, 3, 0, 1, 10);
    slot(ordinary, 3, 1, 0, 40);
    slot(ordinary, 4, 0, 2, 70);
    ordinary.editorMetadata.sectionCount = 2;
    ordinary.editorMetadata.sectionFirstFrames[0] = 0;
    ordinary.editorMetadata.sectionFirstFrames[1] = 3;
    document::setFixedName(ordinary.editorMetadata.sectionNames, 0, 32, "Attract");
    document::setFixedName(ordinary.editorMetadata.sectionNames, 1, 32, "Game");
    ordinary.editorMetadata.spriteSourceFrames[0] = 1;
    ordinary.editorMetadata.spriteSourceFrames[1] = 3;
    ordinary.editorMetadata.spriteSourceFrames[2] = 4;
    ordinary.editorMetadata.importedPalettes.assign(4 * document::kImportedPaletteBytes, 7);

    load(ordinary);
    showLeft("Frames");
    D("structure: loaded");
    dumpFocusChain(w);

    // Frames.
    selectRows({1, 2});
    D("structure: two frames selected");
    deleteFrames(QStringLiteral("Cancel"));
    D("structure: delete frames cancelled");
    deleteFrames(QStringLiteral("Delete"));
    D("structure: two frames deleted");
    undo("structure: delete frames undone");
    redo("structure: delete frames redone");
    undo("structure: delete frames undone again");
    row(3);
    deleteFrames(QStringLiteral("Delete"));
    D("structure: first frame of a section deleted");
    undo("structure: section start restored");
    row(5);
    deleteFrames(QStringLiteral("Delete"));
    D("structure: last frame deleted");
    undo("structure: last frame restored");
    w.m_frameList->selectAll();
    guarded([&]() { w.m_deleteFramesAction->trigger(); });
    D("structure: deleting every frame is refused");

    // Delete frames shorter than.
    answerInputDialog(std::nullopt, std::nullopt);
    w.m_deleteShortFramesAction->trigger();
    D("structure: delete short frames cancelled");
    answerInputDialog(std::nullopt, 5);
    w.m_deleteShortFramesAction->trigger();
    D("structure: no selected frame is shorter than 5 ms");
    answerInputDialog(std::nullopt, 15, QStringLiteral("Delete"));
    w.m_deleteShortFramesAction->trigger();
    D("structure: frames shorter than 15 ms deleted");
    undo("structure: short frames restored");
    selectRows({0, 1, 2});
    answerInputDialog(std::nullopt, 15, QStringLiteral("Delete"));
    w.m_deleteShortFramesAction->trigger();
    D("structure: only the short frame among the selected ones deleted");
    undo("structure: short frame restored");

    // The filter stays applied.
    w.m_frameFilterComboBox->setCurrentIndex(
        w.m_frameFilterComboBox->findText(QStringLiteral("With background")));
    D("structure: filter with background");
    row(0);
    deleteFrames(QStringLiteral("Delete"));
    D("structure: frame deleted under the filter");
    undo("structure: frame restored under the filter");
    w.m_frameFilterComboBox->setCurrentIndex(0);
    row(0);
    D("structure: filter off");

    // A history of a frame behind a deleted one is dropped, one before it stays.
    w.setDrawingTool(edit::DrawingTool::Line);
    row(0);
    w.m_preview->m_strokeFinishedHandler(
        render::CanvasHit{render::CanvasRegion::ColorizedFrame, Resolution::Sd, 2, 2},
        render::CanvasHit{render::CanvasRegion::ColorizedFrame, Resolution::Sd, 9, 2}, false);
    row(4);
    w.m_preview->m_strokeFinishedHandler(
        render::CanvasHit{render::CanvasRegion::ColorizedFrame, Resolution::Sd, 2, 5},
        render::CanvasHit{render::CanvasRegion::ColorizedFrame, Resolution::Sd, 9, 5}, false);
    D("structure: frames 0 and 4 painted");
    row(2);
    deleteFrames(QStringLiteral("Delete"));
    D("structure: frame 2 deleted after painting");
    row(3);
    undo("structure: on the old frame 4, undo restores frame 2, not the stroke");
    row(4);
    D("structure: frame 4 keeps its stroke and has nothing to undo but the structure");
    row(0);
    undo("structure: frame 0 undoes its stroke");

    // Comparison masks.
    showLeft("Masks");
    D("structure: masks page");
    byTip<QToolButton>(w, "Add an empty comparison mask")->click();
    D("structure: mask added and selected");
    undo("structure: mask addition undone");
    redo("structure: mask addition redone");
    w.m_maskList->setCurrentRow(0);
    D("structure: unused mask selected");
    clickAnswered("Delete the selected comparison mask", QStringLiteral("Cancel"));
    D("structure: mask deletion cancelled");
    clickAnswered("Delete the selected comparison mask", QStringLiteral("Delete"));
    D("structure: unused mask deleted");
    undo("structure: unused mask restored");
    w.m_maskList->setCurrentRow(2);
    clickAnswered("Delete the selected comparison mask", QStringLiteral("Delete"));
    D("structure: mask in use deleted");
    undo("structure: mask in use restored");
    redo("structure: mask in use deleted again");
    undo("structure: mask in use restored again");

    // Backgrounds.
    showLeft("Backgrounds");
    row(4);
    D("structure: backgrounds page");
    byTip<QToolButton>(w, "Add a background with the picture of the current frame")->click();
    D("structure: background added from frame 4");
    undo("structure: background addition undone");
    redo("structure: background addition redone");
    w.m_backgroundList->setCurrentRow(1);
    D("structure: background in use selected");
    clickAnswered("Delete the selected background", QStringLiteral("Delete"));
    D("structure: background in use deleted");
    undo("structure: background in use restored");
    redo("structure: background in use deleted again");
    undo("structure: background in use restored again");

    // Sprites.
    showLeft("Sprites");
    D("structure: sprites page");
    answerInputDialog(std::nullopt, std::nullopt);
    byTip<QToolButton>(w, "Add an empty sprite")->click();
    D("structure: sprite addition cancelled");
    answerInputDialog(QStringLiteral("flipper"), std::nullopt);
    byTip<QToolButton>(w, "Add an empty sprite")->click();
    D("structure: sprite name in use refused");
    answerInputDialog(QStringLiteral("  "), std::nullopt);
    byTip<QToolButton>(w, "Add an empty sprite")->click();
    D("structure: empty sprite name refused");
    answerInputDialog(QStringLiteral(" kicker "), std::nullopt);
    byTip<QToolButton>(w, "Add an empty sprite")->click();
    D("structure: sprite added and selected");
    undo("structure: sprite addition undone");
    redo("structure: sprite addition redone");
    w.m_spriteList->setCurrentRow(0);
    row(3);
    D("structure: sprite in use selected");
    clickAnswered("Delete the selected sprite", QStringLiteral("Delete"));
    D("structure: sprite in use deleted");
    undo("structure: sprite in use restored");
    redo("structure: sprite in use deleted again");
    undo("structure: sprite in use restored again");
    // With the sprite canvas on, the sprite shown is deleted.
    w.m_spriteList->setCurrentRow(3);
    byTip<QToolButton>(w, "Show selected sprite in the main canvas")->click();
    D("structure: sprite canvas on the added sprite");
    clickAnswered("Delete the selected sprite", QStringLiteral("Delete"));
    D("structure: sprite deleted on the sprite canvas");
    undo("structure: sprite restored on the sprite canvas");
    byTip<QToolButton>(w, "Show selected sprite in the main canvas")->click();
    D("structure: sprite canvas off");
    showLeft("Frames");
    D("structure: back on the frames page");
    dumpFocusChain(w);
    w.m_document.reset();
    w.refreshProjectSummary();
    D("structure: closed");

    // 256x64 original: a new sprite and a new background get their 256x64 data.
    document::ProjectSnapshot big = makeProject(PlaneLayout::NativeExtra, 3);
    big.frames[1].colorizedHd.reset();
    addBackground(big, 0x2222);
    addSprite(big, "ball");
    load(big);
    showLeft("Sprites");
    D("structure: 256 original loaded");
    answerInputDialog(QStringLiteral("target"), std::nullopt);
    byTip<QToolButton>(w, "Add an empty sprite")->click();
    D("structure: 256 original sprite added");
    showLeft("Backgrounds");
    row(1);
    guarded([&]() {
        byTip<QToolButton>(w, "Add a background with the picture of the current frame")->click();
    });
    D("structure: 256 original no background from a frame without its data");
    row(2);
    byTip<QToolButton>(w, "Add a background with the picture of the current frame")->click();
    D("structure: 256 original background added");
    showLeft("Frames");
    row(0);
    deleteFrames(QStringLiteral("Delete"));
    D("structure: 256 original frame deleted");
    undo("structure: 256 original frame restored");
    undo("structure: 256 original background addition undone");
    undo("structure: 256 original sprite addition undone");
    w.m_document.reset();
    w.refreshProjectSummary();
    D("structure: 256 original closed");

    // 192x64 original: nothing gets extra-plane data.
    document::ProjectSnapshot native = makeProject(PlaneLayout::NativeOriginal, 3);
    addBackground(native, 0x3333);
    addSprite(native, "ball");
    load(native);
    showLeft("Sprites");
    D("structure: 192 original loaded");
    answerInputDialog(QStringLiteral("target"), std::nullopt);
    byTip<QToolButton>(w, "Add an empty sprite")->click();
    D("structure: 192 original sprite added");
    showLeft("Backgrounds");
    row(2);
    byTip<QToolButton>(w, "Add a background with the picture of the current frame")->click();
    D("structure: 192 original background added");
    clickAnswered("Delete the selected background", QStringLiteral("Delete"));
    D("structure: 192 original background deleted");
    showLeft("Masks");
    byTip<QToolButton>(w, "Add an empty comparison mask")->click();
    D("structure: 192 original mask added");
    showLeft("Frames");
    selectRows({0, 1});
    deleteFrames(QStringLiteral("Delete"));
    D("structure: 192 original two frames deleted");
    undo("structure: 192 original frames restored");
    {
        // What a save writes for the new records.
        std::ostringstream crom;
        std::string error;
        const bool written =
            legacy::writeLegacyCrom(crom, w.m_document->toSnapshot(), error);
        std::cout << "structure: 192 original saves=" << written << " bytes=" << crom.str().size()
                  << '\n';
    }
    w.m_document.reset();
    w.refreshProjectSummary();
    D("structure: 192 original closed");
}
