// States for the bug fixes of 2026-10-14: compact sprite slot lists, the question about unsaved
// changes, the mask browser icon after mask edits, the frame thumbnail and "Dyn" after dynamic
// mask edits, rejected IDs of records that do not exist, the single selection after a frame
// is selected programmatically, and the markers of references the runtime cannot follow.
// Included at the very end of main() in dump.cpp, for a tree that has these fixes; all titles
// carry the prefix "fixes: ".
{
    using render::Resolution;

    // Four 128x32 frames, 4 colors, two comparison masks, one sprite. Every frame carries
    // dynamic data, as every loaded frame does; only frame 2 has a dynamic pixel. Frame 3 has
    // a gap in its sprite slots, as a file may have.
    document::ProjectSnapshot fs;
    fs.colorCount = 4;
    fs.romName = "fixes";
    for (int i = 0; i < 4; ++i) {
        document::ProjectFrameSnapshot frame{
            document::makeIndexedFrame(render::kSdFrameSize, 0),
            document::makeRgb565Frame(render::kSdFrameSize, static_cast<uint16_t>(0x0841 * i)),
            std::nullopt,
            0,
            255,
        };
        frame.originalSd.pixels[static_cast<std::size_t>(i)] = 3;
        frame.dynamicData = document::FrameDynamicData{
            document::makeIndexedFrame(render::kSdFrameSize, 255),
            document::makeIndexedFrame(render::kHdFrameSize, 255),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0xf800),
            std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 4, 0),
        };
        frame.durationMs = 50;
        frame.spriteReferences.assign(document::kLegacySpritesPerFrame,
                                      document::FrameSpriteReference{255, 0, 0, 127, 31});
        fs.frames.push_back(std::move(frame));
    }
    fs.frames[2].dynamicData->maskSd.pixels[40] = 0;
    fs.frames[0].spriteReferences[0] = document::FrameSpriteReference{0, 1, 1, 20, 20};
    fs.frames[0].spriteReferences[1] = document::FrameSpriteReference{0, 30, 1, 50, 20};
    fs.frames[3].spriteReferences[2] = document::FrameSpriteReference{0, 2, 2, 9, 9};
    fs.comparisonMasks.assign(2, document::makeIndexedFrame(render::kSdFrameSize, 0));
    document::SpriteRecord sprite;
    sprite.originalSd = document::makeIndexedFrame({256, 64}, 255);
    sprite.colorizedSd = document::makeRgb565Frame({256, 64}, 0);
    sprite.maskHd = document::makeIndexedFrame({256, 64}, 255);
    sprite.colorizedHd = document::makeRgb565Frame({256, 64}, 0);
    sprite.detectionDwords.assign(4, 0xffffffff);
    sprite.detectionDwordPositions.assign(4, 0xffff);
    sprite.detectionAreas.assign(4, document::SpriteDetectionArea{});
    sprite.name = "fix";
    fs.sprites.push_back(std::move(sprite));
    document::PaletteRecord palette;
    palette.colors.assign(document::kPaletteColorCount, 0);
    fs.palettes.push_back(palette);

    std::vector<std::string> questions;
    app::UnsavedChangesAnswer answer = app::UnsavedChangesAnswer::Cancel;
    w.m_unsavedChangesAnswer = [&](const QString& action) {
        questions.push_back(action.toStdString());
        return answer;
    };
    const auto load = [&]() {
        w.m_document = document::ProjectDocument::fromSnapshot(fs);
        w.m_currentCromPath = QStringLiteral("/nonexistent/fixes.cROM");
        w.m_currentCrpPath = QStringLiteral("/nonexistent/fixes.cRP");
        w.m_undoHistories.clear();
        w.refreshProjectSummary();
    };
    const auto maskIconsHash = [&]() {
        size_t hash = 0;
        for (int row = 0; row < w.m_maskList->count(); ++row) {
            const QImage image = w.m_maskList->item(row)->icon().pixmap(QSize(160, 42)).toImage()
                                     .convertToFormat(QImage::Format_ARGB32);
            hash = hash * 31 + qHashBits(image.constBits(), static_cast<size_t>(image.sizeInBytes()));
        }
        return hash;
    };
    const auto printState = [&]() {
        std::cout << "fixes: mask icons " << maskIconsHash() << " selection:";
        for (std::size_t f : w.selectedFrameIndices()) std::cout << ' ' << f;
        std::cout << " current=" << (w.currentFrameIndex() ? int(*w.currentFrameIndex()) : -1)
                  << " questions:";
        for (const std::string& question : questions) std::cout << " [" << question << ']';
        std::cout << " visible=" << w.isVisible() << " dirty="
                  << (w.m_document ? int(w.m_document->isDirty()) : -1) << "\nfixes: slots";
        if (w.m_document) {
            for (std::size_t f = 0; f < w.m_document->frameCount(); ++f) {
                std::cout << " f" << f << '[';
                for (const document::FrameSpriteReference& r : w.m_document->frameSpriteReferences(f)) {
                    if (r.spriteId != 255) std::cout << int(r.spriteId) << '@' << r.minX << ',';
                    else std::cout << '_';
                }
                std::cout << "] mask=" << int(w.m_document->comparisonMaskId(f));
            }
        }
        std::cout << '\n';
        questions.clear();
    };
    const auto D = [&](const char* title) {
        dump(title, w);
        printState();
    };
    const auto noModal = [&](const std::function<void()>& call) {
        QTimer::singleShot(0, []() {
            if (QWidget* modal = QApplication::activeModalWidget()) {
                std::cout << "fixes: unexpected modal " << modal->windowTitle().toStdString() << '\n';
                if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
            }
        });
        call();
        QApplication::processEvents();
    };

    load();
    D("fixes: loaded");

    // Bug 5: "Dyn" only for frame 2, the one with a dynamic pixel; the filter agrees.
    const int dynamicFilter = w.m_frameFilterComboBox->findData(
        static_cast<int>(app::MainWindow::FrameFilter::Dynamic));
    w.m_frameFilterComboBox->setCurrentIndex(dynamicFilter);
    D("fixes: dynamic filter");
    w.m_frameFilterComboBox->setCurrentIndex(0);

    // Bugs 4 and 5: a dynamic mask edit redraws the frame's thumbnail and its "Dyn".
    // Undo and redo of mask edits belong to the mask modes.
    w.m_frameList->setCurrentRow(1);
    w.m_dynamicMaskOverlayButton->setChecked(true);
    w.commitEdit(std::make_unique<edit::MaskEditCommand>(
        edit::EditKind::DynamicMask, 1, Resolution::Sd,
        std::vector<edit::MaskPixelChange>{{5, 5, 255, 0}, {6, 5, 255, 0}}));
    w.updateUndoRedoActions();
    D("fixes: dynamic mask painted on frame 1");
    w.m_undoAction->trigger();
    D("fixes: dynamic mask undone");
    w.m_dynamicMaskOverlayButton->setChecked(false);

    // Bug 3: the mask browser icon follows a mask edit, its undo and its redo.
    w.setCurrentFrameComparisonMaskId(1);
    w.m_comparisonMaskOverlayButton->setChecked(true);
    w.commitEdit(std::make_unique<edit::MaskEditCommand>(
        edit::EditKind::ComparisonMask, 1, Resolution::Sd,
        std::vector<edit::MaskPixelChange>{{10, 10, 0, 1}, {11, 10, 0, 1}, {12, 10, 0, 1}}));
    w.updateUndoRedoActions();
    D("fixes: mask 1 painted");
    w.m_undoAction->trigger();
    D("fixes: mask 1 undone");
    w.m_redoAction->trigger();
    D("fixes: mask 1 redone");
    w.m_comparisonMaskOverlayButton->setChecked(false);

    // Bug 6: a mask ID without a mask is refused.
    w.setCurrentFrameComparisonMaskId(5);
    D("fixes: mask id 5 refused");

    // Bug 7: selecting a frame programmatically leaves it the only selected one.
    w.m_frameList->selectAll();
    w.selectFrameNear(2);
    D("fixes: frame near 2 after select all");
    w.m_frameList->selectAll();
    w.m_frameList->setCurrentRow(2);
    w.m_frameList->item(0)->setSelected(true);
    w.selectFrameNear(2);
    D("fixes: frame near 2 when it is current already");

    // Bug 1: Remove closes the slot list up; Assign into an empty slot takes the first one.
    auto* fixesSlotCombo = byTip<QComboBox>(w, "Current frame sprite slot");
    w.m_frameList->setCurrentRow(0);
    fixesSlotCombo->setCurrentIndex(0);
    w.removeCurrentSpriteSlot();
    D("fixes: sprite removed from slot 0");
    fixesSlotCombo->setCurrentIndex(fixesSlotCombo->findData(5));
    w.assignSelectedSpriteToCurrentSlot();
    D("fixes: sprite assigned to empty slot 5");
    w.m_frameList->setCurrentRow(3);
    D("fixes: frame 3 with a gap in its slots");

    // Bug 2: the question about unsaved changes.
    load();
    w.m_document->setFrameDurationMs(0, 60);
    w.refreshProjectSummary();
    answer = app::UnsavedChangesAnswer::Cancel;
    noModal([&]() { w.close(); });
    D("fixes: quit cancelled");
    noModal([&]() { w.openProject(); });
    D("fixes: open project cancelled");
    noModal([&]() { w.newProjectFromDump(); });
    D("fixes: new project from dump cancelled");
    noModal([&]() { w.openRecentProject(QStringLiteral("/nonexistent/fixes.cROM|/nonexistent/fixes.cRP")); });
    D("fixes: open recent cancelled");
    answer = app::UnsavedChangesAnswer::Save;
    noModal([&]() { w.close(); });
    D("fixes: quit, save fails");
    answer = app::UnsavedChangesAnswer::Discard;
    noModal([&]() { w.close(); });
    D("fixes: quit, changes discarded");
    w.show();
    w.m_unsavedChangesAnswer = nullptr;
    w.m_document.reset();
    w.m_currentCromPath.clear();
    w.m_currentCrpPath.clear();
    w.refreshProjectSummary();
    D("fixes: closed");
}
