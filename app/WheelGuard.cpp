#include "WheelGuard.h"

#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QScrollBar>
#include <QWidget>

namespace colorizing::app {

namespace {

bool isValueControl(const QWidget* widget)
{
    return qobject_cast<const QComboBox*>(widget) ||
           qobject_cast<const QAbstractSpinBox*>(widget) ||
           (qobject_cast<const QAbstractSlider*>(widget) &&
            !qobject_cast<const QScrollBar*>(widget));
}

// The nearest scroll area around the widget that has something to scroll.
QAbstractScrollArea* scrollingArea(QWidget* widget)
{
    for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
        auto* area = qobject_cast<QAbstractScrollArea*>(parent);
        if (area && area->verticalScrollBar()->maximum() > area->verticalScrollBar()->minimum()) {
            return area;
        }
    }
    return nullptr;
}

class WheelGuard : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!watched->isWidgetType()) {
            return false;
        }
        auto* widget = static_cast<QWidget*>(watched);
        if (!isValueControl(widget)) {
            return false;
        }
        // Qt gives a control with the wheel focus policy the focus before any filter sees the
        // wheel event, so the focus check below would always pass. Such a control takes the
        // focus from a click or the tab key only.
        if (event->type() == QEvent::Polish) {
            if ((widget->focusPolicy() & Qt::WheelFocus) == Qt::WheelFocus) {
                widget->setFocusPolicy(Qt::StrongFocus);
            }
            return false;
        }
        if (event->type() != QEvent::Wheel || widget->hasFocus()) {
            return false;
        }
        // The value stays; the area around it scrolls, if it can.
        if (QAbstractScrollArea* area = scrollingArea(widget)) {
            QCoreApplication::sendEvent(area->verticalScrollBar(), event);
        }
        return true;
    }
};

}  // namespace

void installWheelGuard(QObject* application)
{
    application->installEventFilter(new WheelGuard(application));
}

}  // namespace colorizing::app
