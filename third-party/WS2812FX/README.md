# WS2812FX (vendored)

The effect library of the PPUC DMD firmware's speaker lights, built into the editor for its
speaker light preview, so the preview runs the effect code the hardware runs.

- `src/`: `WS2812FX.h`, `WS2812FX.cpp`, `modes.cpp`, `modes_funcs.cpp`, `modes_esp.h` of
  https://github.com/PPUC/WS2812FX at commit 6a5cb5b (the commit PPUCDMD-firmwares pins),
  unchanged. MIT license, see `LICENSE`.
- `desktop/`: the editor's own stand-ins for the parts of Arduino and Adafruit_NeoPixel the
  library uses, with a virtual clock (`ws2812fxSetMillis()`).

Update `src/` together with the firmware's pin of the library.
