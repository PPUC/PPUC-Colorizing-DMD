#pragma once

class QObject;

namespace colorizing::app {

// The mouse wheel over a value control (a combo box, spin box or slider) that does not have
// the keyboard focus scrolls the list or section around it instead of changing the value, so
// scrolling through a sidebar section does not change the settings it passes. A control
// clicked into first takes the wheel as before. Installed once, on the application.
void installWheelGuard(QObject* application);

}  // namespace colorizing::app
