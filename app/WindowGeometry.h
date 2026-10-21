#pragma once

#include <QString>

class QWidget;

namespace colorizing::app {

// Keeps a window's size and place on the screen, maximized or not, for the next time it opens
// (issue #2): restored from QSettings ("windows/<key>") now, before it is shown, and stored
// whenever it is hidden or closed. A place on a screen that is gone is moved onto one that is
// there (QWidget::restoreGeometry()). Without anything stored the window keeps the size it has.
void keepWindowGeometry(QWidget* window, const QString& key);

}  // namespace colorizing::app
