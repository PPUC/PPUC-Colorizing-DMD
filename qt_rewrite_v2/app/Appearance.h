#pragma once

#include "FrameImageUtils.h"

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

// How the canvas, the sprite editor and PNG exports draw DMD pixels: squares or dots, at 50 to
// 100 % of their cell in steps of ten (squares at 80 % until set otherwise).
[[nodiscard]] DmdRenderOptions loadDmdRenderOptions();
void saveDmdRenderOptions(const DmdRenderOptions& options);

}  // namespace colorizing::app
