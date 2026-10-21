# WS2812FX on a desktop: the effect code of the speaker lights, for the editor's preview.
add_library(ws2812fx_desktop STATIC
    src/WS2812FX.cpp
    src/modes.cpp
    src/modes_funcs.cpp
    desktop/Adafruit_NeoPixel.cpp
)
# SYSTEM: the library's headers are not held to this project's warnings either.
target_include_directories(ws2812fx_desktop SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/desktop
)
# modes_esp.h, the mode table the RP2040/RP2350 firmware builds with.
target_compile_definitions(ws2812fx_desktop PUBLIC ARDUINO_ARCH_RP2040=1)
set_target_properties(ws2812fx_desktop PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Third-party code: its warnings are not this project's to fix.
if(MSVC)
    target_compile_options(ws2812fx_desktop PRIVATE /w)
else()
    target_compile_options(ws2812fx_desktop PRIVATE -w)
endif()
