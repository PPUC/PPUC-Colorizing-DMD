// The row above the canvas wraps when the window is narrow, title prefix "toolbar: ".
// Included at the very end of main() in dump.cpp, for a tree that has app/FlowLayout.h. With
// UI_DUMP_GRAB=<folder> the window is also saved there as toolbar-<width>.png.
{
    for (const int width : {700, 1000, 1600}) {
        w.resize(width, 700);
        QCoreApplication::processEvents();
        const std::string title = "toolbar: window " + std::to_string(width) + " wide";
        dump(title.c_str(), w);
        std::cout << "toolbar: rows at";
        int lastY = -1;
        for (QToolButton* button : {w.m_sdButton, w.m_toolButtons.front(), w.m_activeColorButton,
                                    w.m_playbackButton}) {
            const int y = button->mapTo(&w, QPoint(0, 0)).y();
            std::cout << ' ' << (y == lastY ? "same" : std::to_string(y));
            lastY = y;
        }
        std::cout << " | status " << w.m_frameStatusLabel->width() << " wide\n";
        const QByteArray grab = qgetenv("UI_DUMP_GRAB");
        if (!grab.isEmpty()) {
            w.grab().save(QString::fromLocal8Bit(grab) +
                          QStringLiteral("/toolbar-%1.png").arg(width));
        }
    }
    w.resize(1000, 700);
    QCoreApplication::processEvents();
}
