cmake_minimum_required(VERSION 3.20)

project(ColorizingDMDQtRewriteV2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COLORIZINGDMD_QT_V2_BUILD_TESTS "Build qt_rewrite_v2 tests" ON)
option(COLORIZINGDMD_QT_V2_BUILD_APP "Build qt_rewrite_v2 Qt app" ON)
option(COLORIZINGDMD_QT_V2_BUILD_UI_DUMP "Build the UI regression dump tool (tools/ui_dump)" OFF)

# Only for the project's own targets, so the flags do not reach third-party code.
function(colorizing_qt_v2_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

add_subdirectory(core)

if(COLORIZINGDMD_QT_V2_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    add_subdirectory(app)
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
