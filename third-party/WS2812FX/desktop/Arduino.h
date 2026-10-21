// The part of the Arduino API WS2812FX uses, for building it on a desktop (the editor's
// speaker light preview). Time is a virtual clock the caller sets (ws2812fxSetMillis), so
// the effects can be stepped to any moment.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

typedef bool boolean;
typedef uint8_t byte;

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_ptr(addr) (*(void* const*)(addr))

class __FlashStringHelper;
#define F(text) (reinterpret_cast<const __FlashStringHelper*>(text))

void ws2812fxSetMillis(unsigned long ms);
unsigned long millis();
unsigned long micros();
inline void delay(unsigned long) {}
long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

template <typename T, typename L, typename H>
inline T constrain(T value, L low, H high)
{
    return value < static_cast<T>(low) ? static_cast<T>(low)
           : value > static_cast<T>(high) ? static_cast<T>(high)
                                          : value;
}
inline long map(long x, long inMin, long inMax, long outMin, long outMax)
{
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
using std::max;
using std::min;
