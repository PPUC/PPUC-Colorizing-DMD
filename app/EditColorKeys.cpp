#include "EditColorKeys.h"

#include <array>

#include <QGuiApplication>
#include <QKeyEvent>

namespace colorizing::app {

namespace {

constexpr std::size_t kSlotCount = 16;

// In slot order: 1 2 3 4, Q W E R, A S D F, Z X C V (QWERTY names).
constexpr std::array<uint32_t, kSlotCount> kMacVirtualKeys{
    0x12, 0x13, 0x14, 0x15, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x01, 0x02, 0x03, 0x06, 0x07, 0x08, 0x09};
constexpr std::array<uint32_t, kSlotCount> kWindowsScanCodes{
    2, 3, 4, 5, 16, 17, 18, 19, 30, 31, 32, 33, 44, 45, 46, 47};
constexpr std::array<uint32_t, kSlotCount> kXkbKeyCodes{
    10, 11, 12, 13, 24, 25, 26, 27, 38, 39, 40, 41, 52, 53, 54, 55};
constexpr std::array<int, kSlotCount> kQwertyKeys{
    Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R,
    Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V};

template <typename Value>
std::optional<std::size_t> slotOf(const std::array<Value, kSlotCount>& codes, Value code)
{
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (codes[slot] == code) {
            return slot;
        }
    }
    return std::nullopt;
}

}  // namespace

NativeKeyCodes currentNativeKeyCodes()
{
#if defined(Q_OS_MACOS)
    return NativeKeyCodes::MacVirtualKey;
#elif defined(Q_OS_WIN)
    return NativeKeyCodes::WindowsScanCode;
#else
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("xcb") || platform.startsWith(QLatin1String("wayland"))) {
        return NativeKeyCodes::XkbKeyCode;
    }
    return NativeKeyCodes::None;
#endif
}

std::optional<std::size_t> editColorSlotForNativeKey(NativeKeyCodes codes,
                                                     uint32_t nativeScanCode,
                                                     uint32_t nativeVirtualKey)
{
    switch (codes) {
        case NativeKeyCodes::MacVirtualKey:
            return slotOf(kMacVirtualKeys, nativeVirtualKey);
        case NativeKeyCodes::WindowsScanCode:
            // Extended keys carry bit 8 (0xE0 prefix); none of the 16 keys is one.
            return slotOf(kWindowsScanCodes, nativeScanCode);
        case NativeKeyCodes::XkbKeyCode:
            return slotOf(kXkbKeyCodes, nativeScanCode);
        case NativeKeyCodes::None:
            break;
    }
    return std::nullopt;
}

std::optional<std::size_t> editColorSlotForQtKey(int key)
{
    return slotOf(kQwertyKeys, key);
}

std::optional<std::size_t> editColorSlotForKeyEvent(const QKeyEvent& event,
                                                    uint32_t colorCount,
                                                    NativeKeyCodes codes)
{
    const Qt::KeyboardModifiers modifiers = event.modifiers();
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier |
                     Qt::KeypadModifier)) {
        return std::nullopt;
    }
    std::optional<std::size_t> slot;
    // A synthetic event has no native data. The macOS virtual key of "A" is 0, but a real
    // macOS key event always carries native modifier flags.
    const bool hasNativeCodes = event.nativeScanCode() != 0 || event.nativeVirtualKey() != 0 ||
                                event.nativeModifiers() != 0;
    if (codes != NativeKeyCodes::None && hasNativeCodes) {
        slot = editColorSlotForNativeKey(codes, event.nativeScanCode(), event.nativeVirtualKey());
    } else {
        slot = editColorSlotForQtKey(event.key());
    }
    if (!slot || *slot >= colorCount) {
        return std::nullopt;
    }
    if (*slot >= 4 && (modifiers & Qt::ShiftModifier)) {
        return std::nullopt;
    }
    return slot;
}

std::vector<QString> editColorSlotKeyNames()
{
    return {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
            QStringLiteral("Q"), QStringLiteral("W"), QStringLiteral("E"), QStringLiteral("R"),
            QStringLiteral("A"), QStringLiteral("S"), QStringLiteral("D"), QStringLiteral("F"),
            QStringLiteral("Z"), QStringLiteral("X"), QStringLiteral("C"), QStringLiteral("V")};
}

}  // namespace colorizing::app
