// Moving frames in the frame list, and Remove BG, title prefix "frame move: ". Included at the
// very end of main() in dump.cpp, for a tree that has core/edit/FrameMove.h. Frame i lasts
// 100 + i ms, so the order is printed as the original numbers.
{
    const auto D = [&](const char* title) {
        dump(title, w);
        std::cout << "frame move: status '" << w.statusBar()->currentMessage().toStdString()
                  << "' | order";
        for (std::size_t frame = 0; frame < w.m_document->frameCount(); ++frame) {
            std::cout << ' ' << (w.m_document->frameDurationMs(frame) - 100);
        }
        std::cout << " | selected";
        for (std::size_t frame : w.selectedFrameIndices()) {
            std::cout << ' ' << frame;
        }
        std::cout << " | sections";
        for (const edit::Section& section : edit::sections(*w.m_document)) {
            std::cout << ' ' << section.name << '@' << section.firstFrame;
        }
        std::cout << " | backgrounds";
        for (std::size_t frame = 0; frame < w.m_document->frameCount(); ++frame) {
            const auto& data = w.m_document->backgroundData(frame);
            std::cout << ' '
                      << (data && data->backgroundId != document::kNoBackgroundId
                              ? std::to_string(data->backgroundId)
                              : std::string("-"));
        }
        std::cout << '\n';
    };
    const auto selectRows = [&](std::initializer_list<int> rows) {
        w.m_frameList->clearSelection();
        bool first = true;
        for (int row : rows) {
            if (first) {
                w.m_frameList->setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
                first = false;
            } else {
                w.m_frameList->item(row)->setSelected(true);
            }
        }
    };

    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 4;
    snapshot.romName = "moves";
    for (int i = 0; i < 8; ++i) {
        document::IndexedFrame original = document::makeIndexedFrame(render::kSdFrameSize, 0);
        original.pixels[static_cast<std::size_t>(i)] = 3;
        snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
            snapshot.geometry, snapshot.colorCount, std::move(original),
            static_cast<uint32_t>(100 + i)));
    }
    snapshot.editorMetadata.sectionCount = 2;
    snapshot.editorMetadata.sectionFirstFrames[0] = 0;
    snapshot.editorMetadata.sectionFirstFrames[1] = 4;
    document::setFixedName(snapshot.editorMetadata.sectionNames, 0, document::kSectionNameBytes,
                           "Attract");
    document::setFixedName(snapshot.editorMetadata.sectionNames, 1, document::kSectionNameBytes,
                           "Game");
    w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
    w.m_currentCromPath.clear();
    w.m_currentCrpPath.clear();
    w.m_undoHistories.clear();
    w.refreshProjectSummary();
    w.goToFrame(0);
    D("frame move: loaded");

    selectRows({1, 2});
    w.moveSelectedFramesDown();
    D("frame move: two frames one down");
    w.moveSelectedFramesUp();
    D("frame move: and up again");
    selectRows({0});
    w.moveSelectedFramesUp();
    D("frame move: the first frame cannot go up");

    // A drop before row 6 (frame 6), as the list reports it.
    selectRows({1, 3});
    w.m_frameList->onMoveRequested(6);
    D("frame move: dropped before frame 6");
    w.m_undoAction->trigger();
    D("frame move: drop undone");
    w.m_redoAction->trigger();
    D("frame move: drop redone");
    w.m_undoAction->trigger();

    // Before the first frame of Game: they join Game.
    selectRows({1, 2});
    w.m_frameList->onMoveRequested(4);
    D("frame move: before a section's first frame");
    w.m_undoAction->trigger();

    // Move Frames To..., to the end.
    QTimer::singleShot(0, []() {
        auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (!input) {
            std::cerr << "no input dialog\n";
            std::exit(3);
        }
        std::cout << "modal: " << input->labelText().toStdString() << " | " << input->intValue()
                  << " of " << input->intMinimum() << " to " << input->intMaximum() << '\n';
        input->setIntValue(input->intMaximum());
        input->accept();
    });
    selectRows({0});
    w.moveSelectedFramesTo();
    D("frame move: to the end");
    w.m_undoAction->trigger();

    // Remove BG on the selected frames.
    w.recordStructureEdit(edit::addBackgroundFromFrame(*w.m_document, 0));
    selectRows({2, 3});
    w.m_backgroundComboBox->setCurrentIndex(w.m_backgroundComboBox->findData(0));
    w.assignSelectedBackgroundToFrames();
    D("frame move: background assigned to two frames");
    selectRows({3});
    w.m_backgroundPanel->onRemoveRequested();
    D("frame move: Remove BG on one");
    w.m_undoAction->trigger();
    D("frame move: Remove BG undone");

    // The frames that use the selected background (a tree with that filter): the asset of the
    // Backgrounds tab.
    const int usesBackground = w.m_frameFilterComboBox->findText(
        QStringLiteral("Using the selected asset"));
    if (usesBackground >= 0) {
        for (int i = 0; i < w.m_leftBrowserTabs->count(); ++i) {
            if (w.m_leftBrowserTabs->tabText(i) == QStringLiteral("Backgrounds")) {
                w.m_leftBrowserTabs->setCurrentIndex(i);
            }
        }
        w.m_frameFilterComboBox->setCurrentIndex(usesBackground);
        D("frame move: frames using the selected background");
        std::cout << "frame move: listed";
        for (int row = 0; row < w.m_frameList->count(); ++row) {
            std::cout << ' ' << w.m_frameList->item(row)->data(Qt::UserRole).toULongLong();
        }
        std::cout << '\n';
        w.m_frameFilterComboBox->setCurrentIndex(0);
    }

    // A mask and a background dragged onto the canvas (a tree with drops on the canvas).
    if (w.m_preview->m_dropHandler) {
        const auto rowOf = [](QListWidget* list, int index) {
            for (int row = 0; row < list->count(); ++row) {
                if (list->item(row)->data(Qt::UserRole).toInt() == index) {
                    return row;
                }
            }
            return -1;
        };
        const auto assignments = [&](const char* title) {
            D(title);
            std::cout << "frame move: masks";
            for (std::size_t frame = 0; frame < w.m_document->frameCount(); ++frame) {
                const int mask = w.m_document->comparisonMaskId(frame);
                std::cout << ' ' << (mask == 255 ? std::string("-") : std::to_string(mask));
            }
            std::cout << '\n';
        };
        w.recordStructureEdit(edit::addComparisonMask(*w.m_document));
        selectRows({4, 5});
        w.m_maskList->setCurrentRow(rowOf(w.m_maskList, 0));
        std::cout << "frame move: canvas takes the mask "
                  << w.m_preview->m_dropHandler(w.m_maskList, false) << '\n';
        w.m_preview->m_dropHandler(w.m_maskList, true);
        assignments("frame move: mask dropped onto the canvas");
        w.m_undoAction->trigger();
        assignments("frame move: mask drop undone");
        selectRows({6});
        w.m_backgroundList->setCurrentRow(rowOf(w.m_backgroundList, 0));
        w.m_preview->m_dropHandler(w.m_backgroundList, true);
        D("frame move: background dropped onto the canvas");
        w.m_undoAction->trigger();
        D("frame move: background drop undone");
        std::cout << "frame move: canvas takes a frame "
                  << w.m_preview->m_dropHandler(w.m_frameList, false) << '\n';
    }
}
