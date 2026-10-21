// A desktop stand-in for the Adafruit_NeoPixel members WS2812FX uses: the pixel buffer in
// the strip's byte order, the brightness scaling of setPixelColor() / getPixelColor(),
// colour helpers. show() sends nothing; the caller reads the pixels.
#pragma once

#include "Arduino.h"

typedef uint16_t neoPixelType;

// Byte offsets of a pixel: white, red, green, blue (two bits each, high to low).
#define NEO_RGB ((0 << 6) | (0 << 4) | (1 << 2) | (2))
#define NEO_GRB ((1 << 6) | (1 << 4) | (0 << 2) | (2))
#define NEO_GBR ((2 << 6) | (2 << 4) | (0 << 2) | (1))
#define NEO_RGBW ((3 << 6) | (0 << 4) | (1 << 2) | (2))
#define NEO_GRBW ((3 << 6) | (1 << 4) | (0 << 2) | (2))
#define NEO_GBRW ((3 << 6) | (2 << 4) | (0 << 2) | (1))
#define NEO_KHZ800 0x0000
#define NEO_KHZ400 0x0100

class Adafruit_NeoPixel {
public:
    Adafruit_NeoPixel(uint16_t n, int16_t pin = 6, neoPixelType type = NEO_GRB + NEO_KHZ800);
    ~Adafruit_NeoPixel();

    void begin() { begun = true; }
    void show() {}
    bool canShow() { return true; }
    void setPin(int16_t p) { pin = p; }
    void setPixelColor(uint16_t n, uint8_t r, uint8_t g, uint8_t b);
    void setPixelColor(uint16_t n, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    void setPixelColor(uint16_t n, uint32_t c);
    void fill(uint32_t c = 0, uint16_t first = 0, uint16_t count = 0);
    void setBrightness(uint8_t b) { brightness = static_cast<uint8_t>(b + 1); }
    void clear() { std::memset(pixels, 0, numBytes); }
    void updateLength(uint16_t n);
    void updateType(neoPixelType t);
    uint8_t* getPixels() const { return pixels; }
    uint8_t getBrightness() const { return static_cast<uint8_t>(brightness - 1); }
    int16_t getPin() const { return pin; }
    uint16_t numPixels() const { return numLEDs; }
    uint32_t getPixelColor(uint16_t n) const;

    static uint8_t sine8(uint8_t x);
    static uint8_t gamma8(uint8_t x);
    static uint32_t gamma32(uint32_t x);
    static uint32_t Color(uint8_t r, uint8_t g, uint8_t b)
    {
        return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
    }
    static uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
    {
        return (static_cast<uint32_t>(w) << 24) | Color(r, g, b);
    }
    static uint32_t ColorHSV(uint16_t hue, uint8_t sat = 255, uint8_t val = 255);

protected:
    bool begun = false;
    uint16_t numLEDs = 0;
    uint16_t numBytes = 0;
    int16_t pin = -1;
    // 0 is full brightness, otherwise the set brightness + 1.
    uint8_t brightness = 0;
    uint8_t* pixels = nullptr;
    uint8_t rOffset = 1;
    uint8_t gOffset = 0;
    uint8_t bOffset = 2;
    uint8_t wOffset = 1;
};
