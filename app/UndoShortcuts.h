#pragma once

#include <QKeySequence>
#include <QList>

namespace colorizing::app {

// The default keys of the Edit menu's four undo entries (issue #72). Undo and Redo go through
// the whole project, most recent edit first, as in the Win32 editor and other programs, and take
// the platform's standard keys (Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, Ctrl+Y on Windows). Undo Here and
// Redo Here keep to the frame, plane and mode shown (MainWindow::currentHistoryKeys()). Ctrl is
// Cmd on macOS and Alt is Option.
struct UndoShortcuts {
    QList<QKeySequence> undo;
    QList<QKeySequence> redo;
    QList<QKeySequence> undoHere;
    QList<QKeySequence> redoHere;
};

[[nodiscard]] inline UndoShortcuts defaultUndoShortcuts()
{
    return {QKeySequence::keyBindings(QKeySequence::Undo),
            QKeySequence::keyBindings(QKeySequence::Redo),
            {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z)},
            {QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Z)}};
}

}  // namespace colorizing::app
