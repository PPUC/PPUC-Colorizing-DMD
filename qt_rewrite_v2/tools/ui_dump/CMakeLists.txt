# UI regression dump tool, see README.md.
#
# Two ways to build it:
#   - as part of the project: -DCOLORIZINGDMD_QT_V2_BUILD_UI_DUMP=ON dumps the current tree
#   - standalone (what run.sh does): cmake -S tools/ui_dump -DUI_DUMP_TREE=<qt_rewrite_v2 tree>
#     dumps any tree, e.g. a "git archive" export, with the script sources next to this file
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ColorizingDMDQtRewriteV2UiDump LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    get_filename_component(UI_DUMP_DEFAULT_TREE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(UI_DUMP_TREE "${UI_DUMP_DEFAULT_TREE}" CACHE PATH "qt_rewrite_v2 tree to dump")
    if(NOT EXISTS "${UI_DUMP_TREE}/app/MainWindow.cpp")
        message(FATAL_ERROR "UI_DUMP_TREE is not a qt_rewrite_v2 tree: ${UI_DUMP_TREE}")
    endif()

    set(COLORIZINGDMD_QT_V2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(COLORIZINGDMD_QT_V2_BUILD_APP ON CACHE BOOL "" FORCE)
    set(COLORIZINGDMD_QT_V2_BUILD_UI_DUMP OFF CACHE BOOL "" FORCE)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    add_subdirectory("${UI_DUMP_TREE}" tree)
else()
    set(UI_DUMP_TREE "${PROJECT_SOURCE_DIR}")
endif()

add_executable(colorizing_qt_v2_ui_dump
    dump.cpp
    big_original_script.inc
    dump_import_script.inc
    fixes_script.inc
    inspector_script.inc
    native_original_script.inc
    runtime_view_script.inc
    sprite_script.inc
    structure_script.inc
    "${UI_DUMP_TREE}/app/MainWindow.cpp"
)

set_source_files_properties(big_original_script.inc dump_import_script.inc fixes_script.inc
                            inspector_script.inc
                            native_original_script.inc runtime_view_script.inc
                            sprite_script.inc structure_script.inc
                            PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(colorizing_qt_v2_ui_dump PRIVATE "${UI_DUMP_TREE}/app")

target_link_libraries(colorizing_qt_v2_ui_dump
    PRIVATE
        colorizing_qt_v2_widgets
        Qt6::Widgets
)

target_compile_definitions(colorizing_qt_v2_ui_dump PRIVATE QT_NO_KEYWORDS)

# Like the tests, the fixtures leave trailing fields of document records at their defaults.
if(COMMAND colorizing_qt_v2_enable_warnings)
    colorizing_qt_v2_enable_warnings(colorizing_qt_v2_ui_dump)
    if(NOT MSVC)
        target_compile_options(colorizing_qt_v2_ui_dump PRIVATE -Wno-missing-field-initializers)
    endif()
endif()
