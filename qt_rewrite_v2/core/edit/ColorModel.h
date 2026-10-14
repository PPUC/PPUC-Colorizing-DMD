#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace colorizing::edit {

struct Rgb888 {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    [[nodiscard]] bool operator==(const Rgb888& other) const;
    [[nodiscard]] bool operator!=(const Rgb888& other) const;
};

// Hue in degrees (0 up to 360), saturation and value in percent (0..100), as the Win32
// editor's color picker shows them.
struct Hsv {
    double hue = 0.0;
    double saturation = 0.0;
    double value = 0.0;
};

// Each component scaled to 0..255 and truncated, the expansion the editor uses everywhere it
// shows an RGB565 color. rgb888ToRgb565() of the result gives the RGB565 value back.
[[nodiscard]] Rgb888 rgb565ToRgb888(uint16_t rgb565);
// Keeps the top 5/6/5 bits of each component.
[[nodiscard]] uint16_t rgb888ToRgb565(Rgb888 color);

[[nodiscard]] Hsv rgb888ToHsv(Rgb888 color);
// Rounds to the nearest 8-bit value. Hue is taken modulo 360, saturation and value are
// clamped to 0..100.
[[nodiscard]] Rgb888 hsvToRgb888(Hsv hsv);
// The precision the picker shows H, S and V with: one decimal. With it every RGB565 color
// survives RGB565 -> RGB888 -> HSV (rounded) -> RGB888 -> RGB565; with whole numbers, as the
// Win32 editor shows them, about 10,000 of the 65536 colors do not.
[[nodiscard]] Hsv roundedHsv(Hsv hsv);

// "RRGGBB", upper case.
[[nodiscard]] std::string rgb888ToHex(Rgb888 color);
// Six hex digits, any case, with or without a leading '#'.
[[nodiscard]] std::optional<Rgb888> rgb888FromHex(const std::string& text);

// One component of rgb565ToRgb888(); values above 31 or 63 are clamped.
[[nodiscard]] uint8_t expand5To8(uint8_t value5);
[[nodiscard]] uint8_t expand6To8(uint8_t value6);

}  // namespace colorizing::edit
