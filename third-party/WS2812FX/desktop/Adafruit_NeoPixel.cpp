#include "Adafruit_NeoPixel.h"

namespace {

unsigned long g_millis = 0;
// A fixed linear congruential generator: the same effect looks the same on every run.
uint32_t g_random = 1;

uint8_t scaled(uint8_t value, uint8_t brightness)
{
    return brightness == 0 ? value : static_cast<uint8_t>((value * brightness) >> 8);
}

}  // namespace

void ws2812fxSetMillis(unsigned long ms)
{
    g_millis = ms;
}

unsigned long millis()
{
    return g_millis;
}

unsigned long micros()
{
    return g_millis * 1000;
}

long random(long max)
{
    g_random = g_random * 1103515245u + 12345u;
    return max <= 0 ? 0 : static_cast<long>((g_random >> 8) % static_cast<uint32_t>(max));
}

long random(long min, long max)
{
    return max <= min ? min : min + random(max - min);
}

void randomSeed(unsigned long seed)
{
    g_random = static_cast<uint32_t>(seed);
}

Adafruit_NeoPixel::Adafruit_NeoPixel(uint16_t n, int16_t p, neoPixelType type)
    : pin(p)
{
    updateType(type);
    updateLength(n);
}

Adafruit_NeoPixel::~Adafruit_NeoPixel()
{
    std::free(pixels);
}

void Adafruit_NeoPixel::updateType(neoPixelType t)
{
    wOffset = (t >> 6) & 3;
    rOffset = (t >> 4) & 3;
    gOffset = (t >> 2) & 3;
    bOffset = t & 3;
    if (pixels) {
        updateLength(numLEDs);
    }
}

void Adafruit_NeoPixel::updateLength(uint16_t n)
{
    std::free(pixels);
    const uint16_t bytes = static_cast<uint16_t>(n * ((wOffset == rOffset) ? 3 : 4));
    pixels = static_cast<uint8_t*>(std::calloc(bytes == 0 ? 1 : bytes, 1));
    numLEDs = n;
    numBytes = bytes;
}

void Adafruit_NeoPixel::setPixelColor(uint16_t n, uint8_t r, uint8_t g, uint8_t b)
{
    setPixelColor(n, r, g, b, 0);
}

void Adafruit_NeoPixel::setPixelColor(uint16_t n, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    if (n >= numLEDs) {
        return;
    }
    const bool rgbw = wOffset != rOffset;
    uint8_t* p = &pixels[n * (rgbw ? 4 : 3)];
    p[rOffset] = scaled(r, brightness);
    p[gOffset] = scaled(g, brightness);
    p[bOffset] = scaled(b, brightness);
    if (rgbw) {
        p[wOffset] = scaled(w, brightness);
    }
}

void Adafruit_NeoPixel::setPixelColor(uint16_t n, uint32_t c)
{
    setPixelColor(n, static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8),
                  static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 24));
}

void Adafruit_NeoPixel::fill(uint32_t c, uint16_t first, uint16_t count)
{
    if (first >= numLEDs) {
        return;
    }
    const uint16_t end =
        count == 0 || first + count > numLEDs ? numLEDs : static_cast<uint16_t>(first + count);
    for (uint16_t i = first; i < end; ++i) {
        setPixelColor(i, c);
    }
}

uint32_t Adafruit_NeoPixel::getPixelColor(uint16_t n) const
{
    if (n >= numLEDs) {
        return 0;
    }
    const bool rgbw = wOffset != rOffset;
    const uint8_t* p = &pixels[n * (rgbw ? 4 : 3)];
    const auto unscaled = [this](uint8_t value) {
        return brightness == 0 ? value : static_cast<uint8_t>((value << 8) / brightness);
    };
    return Color(unscaled(p[rOffset]), unscaled(p[gOffset]), unscaled(p[bOffset]),
                 rgbw ? unscaled(p[wOffset]) : 0);
}

uint8_t Adafruit_NeoPixel::sine8(uint8_t x)
{
    const double value = 128.0 + 127.5 * std::sin(x * 2.0 * 3.14159265358979 / 256.0);
    return static_cast<uint8_t>(std::clamp(value, 0.0, 255.0));
}

uint8_t Adafruit_NeoPixel::gamma8(uint8_t x)
{
    return static_cast<uint8_t>(std::pow(x / 255.0, 2.6) * 255.0 + 0.5);
}

uint32_t Adafruit_NeoPixel::gamma32(uint32_t x)
{
    return (static_cast<uint32_t>(gamma8(static_cast<uint8_t>(x >> 24))) << 24) |
           (static_cast<uint32_t>(gamma8(static_cast<uint8_t>(x >> 16))) << 16) |
           (static_cast<uint32_t>(gamma8(static_cast<uint8_t>(x >> 8))) << 8) |
           gamma8(static_cast<uint8_t>(x));
}

uint32_t Adafruit_NeoPixel::ColorHSV(uint16_t hue, uint8_t sat, uint8_t val)
{
    const double h = hue / 65536.0 * 6.0;
    const double s = sat / 255.0;
    const double v = val / 255.0;
    const int sector = static_cast<int>(h) % 6;
    const double f = h - std::floor(h);
    const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double r = 0, g = 0, b = 0;
    switch (sector) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return Color(static_cast<uint8_t>(r * 255 + 0.5), static_cast<uint8_t>(g * 255 + 0.5),
                 static_cast<uint8_t>(b * 255 + 0.5));
}
