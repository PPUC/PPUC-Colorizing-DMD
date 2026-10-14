#pragma once

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

}  // namespace colorizing::app
