cmake_minimum_required(VERSION 3.20)

# The editor's version: raised before every push (3.99.x until the release 4.0.0).
project(PPUCColorizingDMD VERSION 3.99.19 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COLORIZINGDMD_QT_V2_BUILD_TESTS "Build the editor's tests" ON)
option(COLORIZINGDMD_QT_V2_BUILD_APP "Build the editor" ON)
option(COLORIZINGDMD_QT_V2_BUILD_UI_DUMP "Build the UI regression dump tool (tools/ui_dump)" OFF)
# For the release packages (packaging/, .github/workflows/qt-editor.yml): the app is called
# has no console window on Windows. On macOS every build is the bundle "PPUC Colorizing DMD.app";
# elsewhere a development build is app/PPUC-Colorizing-DMD.
option(COLORIZINGDMD_QT_V2_PACKAGE "Build the app the way the release packages ship it" OFF)

# Only for the project's own targets, so the flags do not reach third-party code.
function(colorizing_qt_v2_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# libserum provides the .cRZ archive reading and writing (core/legacy/LegacyCrzArchive.cpp).
get_filename_component(COLORIZINGDMD_LIBSERUM_DEFAULT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../libserum" ABSOLUTE)
set(COLORIZINGDMD_LIBSERUM_DIR "${COLORIZINGDMD_LIBSERUM_DEFAULT_DIR}" CACHE PATH
    "libserum source checkout, linked statically into the project core")

# Adds libserum's own CMake project as the static target serum_static. libserum configures
# itself through the plain variables PLATFORM, ARCH, BUILD_SHARED and BUILD_STATIC; they are set
# here as normal variables, which take precedence over its cache defaults and end with this
# function. Its compiler settings (C++20, hidden visibility, rpath) stay in its own directory
# scope, and EXCLUDE_FROM_ALL keeps its test tool and install rules out of this build.
function(colorizing_qt_v2_add_libserum)
    get_filename_component(libserum_dir "${COLORIZINGDMD_LIBSERUM_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${libserum_dir}/CMakeLists.txt" OR
       NOT EXISTS "${libserum_dir}/src/serum-decode.h")
        message(FATAL_ERROR
            "libserum not found in '${libserum_dir}'.\n"
            "The editor needs a libserum source checkout that has the Serum_CRZ_* API for "
            ".cRZ support. Put one next to the ColorizingDMD checkout as 'libserum', or "
            "configure with -DCOLORIZINGDMD_LIBSERUM_DIR=<path>.")
    endif()
    if(TARGET serum_static)
        return()
    endif()

    set(BUILD_SHARED OFF)
    set(BUILD_STATIC ON)
    set(ENABLE_SANITIZERS OFF)
    if(WIN32)
        if(MINGW)
            set(PLATFORM "win-mingw")
        else()
            set(PLATFORM "win")
        endif()
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(ARCH "x86")
        else()
            set(ARCH "x64")
        endif()
    else()
        if(APPLE)
            set(PLATFORM "macos")
        else()
            set(PLATFORM "linux")
        endif()
        # Any value libserum does not know: it then leaves the target architecture to this
        # project instead of forcing CMAKE_OSX_ARCHITECTURES.
        set(ARCH "host")
    endif()

    add_subdirectory("${libserum_dir}" "${CMAKE_CURRENT_BINARY_DIR}/libserum" EXCLUDE_FROM_ALL)

    # Third-party code: its warnings are not this project's to fix.
    if(MSVC)
        target_compile_options(serum_static PRIVATE /w)
    else()
        target_compile_options(serum_static PRIVATE -w)
    endif()
endfunction()
colorizing_qt_v2_add_libserum()

# VPX plugins: the editor hosts them on libvpxhost's plugin bus (github.com/PPUC/libvpxhost,
# shared with ppuc), built against the plugin SDK of a vpinball checkout (PPUC's fork). Both
# are optional together; without them the editor hosts no plugins and has no DMD output.
set(COLORIZINGDMD_LIBVPXHOST_DIR "" CACHE PATH "libvpxhost source checkout")
set(COLORIZINGDMD_VPINBALL_DIR "" CACHE PATH
    "vpinball checkout providing the plugin SDK and the plugins built here")
# DMD output goes through VPX's DMDUtil plugin, which is built here against libdmdutil:
# packaging/build-libdmdutil.sh builds libdmdutil and stages its headers and shared libraries
# into a prefix. Needs the two folders above.
set(COLORIZINGDMD_DMDUTIL_PREFIX "" CACHE PATH
    "libdmdutil staged by packaging/build-libdmdutil.sh; empty builds without DMD output")

# WS2812FX, the speaker light effects, for the preview (third-party/WS2812FX/README.md).
add_subdirectory(third-party/WS2812FX)

add_subdirectory(core)

if(COLORIZINGDMD_QT_V2_BUILD_APP)
    # The GitHub OAuth app (with the device flow on) the team features sign in through: PPUC's
    # "PPUC Colorizing DMD". A client ID is no secret; the environment's
    # COLORIZINGDMD_GITHUB_CLIENT_ID overrides it at run time.
    set(COLORIZINGDMD_GITHUB_CLIENT_ID "Ov23liIm2RiLWDVQMIZe" CACHE STRING
        "Client ID of the GitHub OAuth app for team projects")
    # Network: the team features talk to GitHub over HTTPS.
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
    add_subdirectory(app)
endif()

# DEPRECATED (tests/legacy-writers/README.md): the legacy writers some tests and the UI dump
# still make legacy projects with.
if(COLORIZINGDMD_QT_V2_BUILD_TESTS OR COLORIZINGDMD_QT_V2_BUILD_UI_DUMP)
    add_subdirectory(tests/legacy-writers)
endif()

if(COLORIZINGDMD_QT_V2_BUILD_UI_DUMP)
    if(NOT COLORIZINGDMD_QT_V2_BUILD_APP)
        message(FATAL_ERROR "COLORIZINGDMD_QT_V2_BUILD_UI_DUMP needs COLORIZINGDMD_QT_V2_BUILD_APP")
    endif()
    add_subdirectory(tools/ui_dump)
endif()

if(COLORIZINGDMD_QT_V2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
