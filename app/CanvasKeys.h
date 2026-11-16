#pragma once

class QWidget;

namespace colorizing::app {

// Whether a key held for the canvas (Space to move the shown part, W to select non-black pixels
// only) belongs to the canvas although another widget, such as the frame strip, has the focus:
// only with the mouse over the canvas (under: the widget under it), not while a text field or
// a spin box has the focus (focus), and not while a modal window or a popup is open.
[[nodiscard]] bool keyHeldForCanvas(const QWidget* canvas,
                                    const QWidget* focus,
                                    const QWidget* under,
                                    bool modalOrPopupOpen);

}  // namespace colorizing::app
