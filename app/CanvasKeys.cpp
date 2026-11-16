#include "CanvasKeys.h"

#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

namespace colorizing::app {

bool keyHeldForCanvas(const QWidget* canvas,
                      const QWidget* focus,
                      const QWidget* under,
                      bool modalOrPopupOpen)
{
    if (modalOrPopupOpen || qobject_cast<const QLineEdit*>(focus) ||
        qobject_cast<const QAbstractSpinBox*>(focus) || qobject_cast<const QTextEdit*>(focus) ||
        qobject_cast<const QPlainTextEdit*>(focus)) {
        return false;
    }
    return canvas && under && (under == canvas || canvas->isAncestorOf(under));
}

}  // namespace colorizing::app
