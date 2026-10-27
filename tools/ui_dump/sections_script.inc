// Sections and the trigger list above the frame list, title prefix "sections: ".
// Included at the very end of main() in dump.cpp, for a tree that has app/panels/SectionBar.h.
{
    const auto D = [&](const char* title) {
        dump(title, w);
        std::cout << "sections: status '" << w.statusBar()->currentMessage().toStdString()
                  << "' | frame "
                  << (w.currentFrameIndex() ? std::to_string(*w.currentFrameIndex()) : "-")
                  << " | list";
        for (const edit::Section& section : edit::sections(*w.m_document)) {
            std::cout << ' ' << section.name << '@' << section.firstFrame;
        }
        std::cout << " | row 3 '" << w.m_frameList->item(3)->text().toStdString().substr(0, 20)
                  << "'\n";
    };
    // Answers the next modal dialog: a message box by button text, an input dialog with text.
    const auto answer = [](const QString& reply) {
        QTimer::singleShot(0, [reply]() {
            QWidget* modal = QApplication::activeModalWidget();
            if (auto* input = qobject_cast<QInputDialog*>(modal)) {
                std::cout << "modal: " << input->labelText().toStdString() << " | '"
                          << input->textValue().toStdString() << "'\n";
                input->setTextValue(reply);
                input->accept();
                return;
            }
            if (auto* box = qobject_cast<QMessageBox*>(modal)) {
                std::cout << "modal: " << box->text().toStdString() << '\n';
                for (QAbstractButton* button : box->buttons()) {
                    if (button->text() == reply) {
                        button->click();
                        return;
                    }
                }
            }
            std::cerr << "no dialog to answer " << reply.toStdString() << '\n';
            std::exit(3);
        });
    };

    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 4;
    snapshot.romName = "sections";
    for (int i = 0; i < 6; ++i) {
        document::IndexedFrame original = document::makeIndexedFrame(render::kSdFrameSize, 0);
        original.pixels[static_cast<std::size_t>(i)] = 3;
        snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
            snapshot.geometry, snapshot.colorCount, std::move(original), 100));
    }
    snapshot.frames[4].pupTriggerId = 2;
    snapshot.frames[1].pupTriggerId = 9;
    w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
    w.m_projectPath.clear();
    w.m_sourcePath.clear();
    w.m_undoHistories.clear();
    w.refreshProjectSummary();
    w.goToFrame(0);
    D("sections: none yet");

    answer(QStringLiteral("Attract"));
    w.nameSectionAtCurrentFrame();
    w.goToFrame(3);
    answer(QStringLiteral("Game"));
    w.nameSectionAtCurrentFrame();
    D("sections: two named");
    answer(QStringLiteral("Main game"));
    w.nameSectionAtCurrentFrame();
    D("sections: renamed where it starts");
    w.goToFrame(5);
    answer(QStringLiteral("Attract"));
    w.nameSectionAtCurrentFrame();
    D("sections: a taken name is refused");

    w.m_sectionBar->onSectionChosen(0);
    D("sections: jumped to the first one");

    QTimer::singleShot(0, []() {
        auto* dialog = dynamic_cast<app::SectionOrderDialog*>(QApplication::activeModalWidget());
        if (!dialog) { std::cerr << "no order dialog\n"; std::exit(3); }
        dialog->onMoveRequested(1, -1);
        std::cout << "sections: order dialog shows";
        for (int i = 0; i < dialog->m_list->count(); ++i) {
            std::cout << " '" << dialog->m_list->item(i)->text().toStdString() << "'";
        }
        std::cout << " selected " << dialog->m_list->currentRow() << '\n';
        dialog->reject();
    });
    w.orderSections();
    D("sections: reordered");

    answer(QStringLiteral("Delete"));
    w.deleteCurrentSection();
    D("sections: deleted the current frame's section");
    w.m_undoAction->trigger();
    D("sections: delete undone");
    w.m_undoAction->trigger();
    w.m_undoAction->trigger();
    D("sections: order and rename undone");
}
