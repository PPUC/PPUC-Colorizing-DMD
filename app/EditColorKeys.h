#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <QString>

class QKeyEvent;

namespace colorizing::app {

// The keys that select an edit color slot are picked by their place on the keyboard, as the
// Win32 editor does with scan codes (SCKeyState): the digits 1 to 4 select slots 0 to 3, and
// in a 16-color project the three letter rows below them select 4 to 7, 8 to 11 and 12 to 15.
// Named after a QWERTY keyboard that is 1 2 3 4 / Q W E R / A S D F / Z X C V; on an AZERTY
// keyboard the same keys are 1 2 3 4 / A Z E R / Q S D F / W X C V, the Win32 editor's names.
//
// The place of a key is only known through the native key code, which Qt reports per
// platform: the macOS virtual key code (kVK_ANSI_*), the Windows scan code (set 1, as the
// Win32 editor uses), the XKB key code of X11 and Wayland (evdev code + 8). Where none of
// these is available (other platforms, the offscreen platform, synthetic events) the key is
// taken by its Qt key on the assumption of a QWERTY layout.
enum class NativeKeyCodes {
    None,
    MacVirtualKey,
    WindowsScanCode,
    XkbKeyCode,
};

[[nodiscard]] NativeKeyCodes currentNativeKeyCodes();

[[nodiscard]] std::optional<std::size_t> editColorSlotForNativeKey(NativeKeyCodes codes,
                                                                   uint32_t nativeScanCode,
                                                                   uint32_t nativeVirtualKey);
[[nodiscard]] std::optional<std::size_t> editColorSlotForQtKey(int key);

// The slot the key selects in a project of colorCount colors, or nothing. Control, Alt and
// Meta never select one; Shift only with a digit (as in the Win32 editor, where the letters
// need it released).
[[nodiscard]] std::optional<std::size_t> editColorSlotForKeyEvent(const QKeyEvent& event,
                                                                  uint32_t colorCount,
                                                                  NativeKeyCodes codes);

// "1" ... "4", "Q" ... "V": the QWERTY names of the 16 keys, for tool tips.
[[nodiscard]] std::vector<QString> editColorSlotKeyNames();

}  // namespace colorizing::app
