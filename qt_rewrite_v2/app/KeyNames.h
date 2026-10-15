#pragma once

#include <QString>

namespace colorizing::app {

// Keys as the platform names them, for texts that mention them (tooltips, status messages);
// menus show their shortcuts natively themselves.

// A shortcut written portably ("Ctrl+0", "Alt+Left") as the platform shows it: unchanged on
// Windows and Linux, "⌘0", "⌥←" on macOS, where Qt's Ctrl is Cmd and Alt is Option.
[[nodiscard]] QString nativeShortcut(const QString& portable);
// "Ctrl" ("Strg" in German), "Cmd" on macOS.
[[nodiscard]] QString ctrlKeyName();
// "Alt", "Option" on macOS.
[[nodiscard]] QString altKeyName();

}  // namespace colorizing::app
