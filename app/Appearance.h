#pragma once

#include <QColor>

#include "FrameImageUtils.h"

class QStyle;

namespace colorizing::app {

// Light or dark, or as the operating system has it (the default).
enum class Theme {
    System,
    Light,
    Dark,
};

[[nodiscard]] Theme loadTheme();
void saveTheme(Theme theme);

// Applies the theme to the running application. Qt 6.8 and later ask the platform style for
// the color scheme. A style that cannot draw it (Windows 10's "windowsvista" draws light
// only), and Qt before 6.8, use the Fusion style with a light or dark palette instead; back to
// System, the application's own style and palette return.
void applyTheme(Theme theme);
// The editor's style over that of the platform or Fusion (checked buttons in the accent
// color, long combo box lists that scroll); takes base over.
[[nodiscard]] QStyle* createEditorStyle(QStyle* base);

// The style draws a checked button in the highlight color, so that it stands out from the
// others in every theme. A button with this property set to true is drawn as the style draws
// it, e.g. the header of an open section.
inline constexpr const char* kPlainCheckedProperty = "plainChecked";

// Comparison masks wherever the editor shows them (the mask list, the canvas, the preview of
// what a stroke paints into one): magenta, as CDMD shows them.
[[nodiscard]] inline QColor comparisonMaskColor(int alpha = 255)
{
    return QColor(255, 0, 255, alpha);
}

// Helper lines on the canvas: a light green no overlay uses.
[[nodiscard]] inline QColor helperLineColor()
{
    return QColor(140, 255, 90);
}

// Frames the frame list marks, the same frames of the current one: a green, as the Win32
// editor marks them.
[[nodiscard]] inline QColor markedFrameColor()
{
    return QColor(60, 200, 60);
}

// How the canvas, the sprite editor and PNG exports draw DMD pixels: squares or dots, at 50 to
// 100 % of their cell in steps of ten (squares at 80 % until set otherwise).
[[nodiscard]] DmdRenderOptions loadDmdRenderOptions();
void saveDmdRenderOptions(const DmdRenderOptions& options);

}  // namespace colorizing::app
