#include "edit/ColorModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace colorizing::edit {

namespace {

uint8_t toByte(double unit)
{
    return static_cast<uint8_t>(std::lround(std::clamp(unit, 0.0, 1.0) * 255.0));
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

bool Rgb888::operator==(const Rgb888& other) const
{
    return red == other.red && green == other.green && blue == other.blue;
}

bool Rgb888::operator!=(const Rgb888& other) const
{
    return !(*this == other);
}

uint8_t expand5To8(uint8_t value5)
{
    return static_cast<uint8_t>((std::min<int>(value5, 31) * 255) / 31);
}

uint8_t expand6To8(uint8_t value6)
{
    return static_cast<uint8_t>((std::min<int>(value6, 63) * 255) / 63);
}

Rgb888 rgb565ToRgb888(uint16_t rgb565)
{
    return Rgb888{expand5To8(static_cast<uint8_t>((rgb565 >> 11) & 0x1f)),
                  expand6To8(static_cast<uint8_t>((rgb565 >> 5) & 0x3f)),
                  expand5To8(static_cast<uint8_t>(rgb565 & 0x1f))};
}

uint16_t rgb888ToRgb565(Rgb888 color)
{
    return static_cast<uint16_t>(((color.red >> 3) << 11) | ((color.green >> 2) << 5) |
                                 (color.blue >> 3));
}

Hsv rgb888ToHsv(Rgb888 color)
{
    const double red = color.red / 255.0;
    const double green = color.green / 255.0;
    const double blue = color.blue / 255.0;
    const double maximum = std::max({red, green, blue});
    const double minimum = std::min({red, green, blue});
    const double delta = maximum - minimum;
    double hue = 0.0;
    if (delta > 0.0) {
        if (maximum == red) {
            hue = 60.0 * std::fmod((green - blue) / delta, 6.0);
        } else if (maximum == green) {
            hue = 60.0 * ((blue - red) / delta + 2.0);
        } else {
            hue = 60.0 * ((red - green) / delta + 4.0);
        }
        if (hue < 0.0) {
            hue += 360.0;
        }
    }
    return Hsv{hue, maximum > 0.0 ? delta / maximum * 100.0 : 0.0, maximum * 100.0};
}

Rgb888 hsvToRgb888(Hsv hsv)
{
    double hue = std::fmod(hsv.hue, 360.0);
    if (hue < 0.0) {
        hue += 360.0;
    }
    const double saturation = std::clamp(hsv.saturation, 0.0, 100.0) / 100.0;
    const double value = std::clamp(hsv.value, 0.0, 100.0) / 100.0;
    const double sector = hue / 60.0;
    const int index = std::min(static_cast<int>(sector), 5);
    const double fraction = sector - index;
    const double p = value * (1.0 - saturation);
    const double q = value * (1.0 - saturation * fraction);
    const double t = value * (1.0 - saturation * (1.0 - fraction));
    double red = value;
    double green = t;
    double blue = p;
    switch (index) {
        case 0:
            break;
        case 1:
            red = q;
            green = value;
            break;
        case 2:
            red = p;
            green = value;
            blue = t;
            break;
        case 3:
            red = p;
            green = q;
            blue = value;
            break;
        case 4:
            red = t;
            green = p;
            blue = value;
            break;
        default:
            green = p;
            blue = q;
            break;
    }
    return Rgb888{toByte(red), toByte(green), toByte(blue)};
}

Hsv roundedHsv(Hsv hsv)
{
    const auto round1 = [](double value) { return std::round(value * 10.0) / 10.0; };
    Hsv rounded{round1(hsv.hue), round1(hsv.saturation), round1(hsv.value)};
    if (rounded.hue >= 360.0) {
        rounded.hue -= 360.0;
    }
    return rounded;
}

std::string rgb888ToHex(Rgb888 color)
{
    char text[7];
    std::snprintf(text, sizeof(text), "%02X%02X%02X", color.red, color.green, color.blue);
    return std::string(text);
}

std::optional<Rgb888> rgb888FromHex(const std::string& text)
{
    std::string digits = text;
    if (!digits.empty() && digits.front() == '#') {
        digits.erase(digits.begin());
    }
    if (digits.size() != 6) {
        return std::nullopt;
    }
    uint8_t bytes[3] = {};
    for (std::size_t index = 0; index < 3; ++index) {
        const int high = hexDigit(digits[index * 2]);
        const int low = hexDigit(digits[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes[index] = static_cast<uint8_t>(high * 16 + low);
    }
    return Rgb888{bytes[0], bytes[1], bytes[2]};
}

}  // namespace colorizing::edit
