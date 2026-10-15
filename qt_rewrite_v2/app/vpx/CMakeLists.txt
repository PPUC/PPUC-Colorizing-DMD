# The editor's side of VPX plugin hosting (VpxHost), and the plugins built with the editor.
#
# colorizing_qt_v2_vpx is the only target that sees the plugin SDK: its header has no SDK
# types, so the rest of the editor stays C++17 while this target, like the SDK, is C++20.

add_library(colorizing_qt_v2_vpx STATIC
    EditorApi.cpp
    EditorApi.h
    EditorApiHost.h
    VpxHost.cpp
    VpxHost.h
)
target_include_directories(colorizing_qt_v2_vpx PUBLIC "${PROJECT_SOURCE_DIR}/app")
target_link_libraries(colorizing_qt_v2_vpx PUBLIC Qt6::Core PRIVATE colorizing_qt_v2_core)
colorizing_qt_v2_enable_warnings(colorizing_qt_v2_vpx)

set(vpx_libvpxhost_dir "")
set(vpx_vpinball_dir "")
if(COLORIZINGDMD_LIBVPXHOST_DIR)
    get_filename_component(vpx_libvpxhost_dir "${COLORIZINGDMD_LIBVPXHOST_DIR}" ABSOLUTE
        BASE_DIR "${PROJECT_SOURCE_DIR}")
endif()
if(COLORIZINGDMD_VPINBALL_DIR)
    get_filename_component(vpx_vpinball_dir "${COLORIZINGDMD_VPINBALL_DIR}" ABSOLUTE
        BASE_DIR "${PROJECT_SOURCE_DIR}")
endif()

if(NOT vpx_libvpxhost_dir OR NOT vpx_vpinball_dir)
    if(vpx_libvpxhost_dir OR vpx_vpinball_dir)
        message(FATAL_ERROR
            "VPX plugin hosting needs both COLORIZINGDMD_LIBVPXHOST_DIR and "
            "COLORIZINGDMD_VPINBALL_DIR.")
    endif()
    if(COLORIZINGDMD_DMDUTIL_PREFIX)
        message(FATAL_ERROR
            "DMD output goes through VPX's DMDUtil plugin: COLORIZINGDMD_DMDUTIL_PREFIX needs "
            "COLORIZINGDMD_LIBVPXHOST_DIR and COLORIZINGDMD_VPINBALL_DIR as well.")
    endif()
    message(STATUS "VPX plugins: off (no COLORIZINGDMD_LIBVPXHOST_DIR / COLORIZINGDMD_VPINBALL_DIR)")
    return()
endif()

if(NOT EXISTS "${vpx_libvpxhost_dir}/include/vpxhost/PluginBus.h")
    message(FATAL_ERROR "No libvpxhost in '${vpx_libvpxhost_dir}'.")
endif()
set(VPXHOST_VPINBALL_DIR "${vpx_vpinball_dir}" CACHE PATH "" FORCE)
set(VPXHOST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${vpx_libvpxhost_dir}" "${CMAKE_BINARY_DIR}/libvpxhost" EXCLUDE_FROM_ALL)
set_target_properties(vpxhost PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
if(MSVC)
    target_compile_options(vpxhost PRIVATE /w)
else()
    target_compile_options(vpxhost PRIVATE -w)
endif()
# Its include folders (and the plugin SDK's) are third-party headers here.
get_target_property(vpxhost_include_dirs vpxhost INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET vpxhost APPEND PROPERTY
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${vpxhost_include_dirs})

set_target_properties(colorizing_qt_v2_vpx PROPERTIES CXX_STANDARD 20)
target_link_libraries(colorizing_qt_v2_vpx PRIVATE vpxhost::vpxhost)
target_compile_definitions(colorizing_qt_v2_vpx PRIVATE COLORIZINGDMD_HAS_VPX_PLUGINS)
# The editor API for plugins (plugin-api/ColorizingDMDPlugin.h, a C header on top of the SDK's
# MsgPlugin.h). Public, so that tests can call it as a plugin does.
target_include_directories(colorizing_qt_v2_vpx SYSTEM PUBLIC "${vpx_vpinball_dir}/plugins")
target_include_directories(colorizing_qt_v2_vpx PUBLIC "${PROJECT_SOURCE_DIR}/plugin-api")
target_compile_definitions(colorizing_qt_v2_vpx PUBLIC COLORIZINGDMD_HAS_EDITOR_PLUGIN_API)
message(STATUS "VPX plugins: libvpxhost from ${vpx_libvpxhost_dir}, SDK from ${vpx_vpinball_dir}")

# Plugins built here go into <build>/vpx-plugins/<name>/ with their plugin.cfg, as VPX ships
# them; a development build finds them there, and the packaging scripts copy the folder.
set(vpx_plugin_dir "${CMAKE_BINARY_DIR}/vpx-plugins")
target_compile_definitions(colorizing_qt_v2_vpx PRIVATE
    COLORIZINGDMD_BUILD_VPX_PLUGIN_DIR="${vpx_plugin_dir}")

if(NOT COLORIZINGDMD_DMDUTIL_PREFIX)
    message(STATUS "DMD output: off (no COLORIZINGDMD_DMDUTIL_PREFIX)")
    return()
endif()

# The DMDUtil plugin, from the vpinball checkout, linked with the staged libdmdutil.
get_filename_component(dmdutil_prefix "${COLORIZINGDMD_DMDUTIL_PREFIX}" ABSOLUTE)
find_library(COLORIZINGDMD_DMDUTIL_LIBRARY NAMES dmdutil dmdutil64
    PATHS "${dmdutil_prefix}/lib" NO_DEFAULT_PATH)
if(NOT COLORIZINGDMD_DMDUTIL_LIBRARY OR NOT EXISTS "${dmdutil_prefix}/include/DMDUtil/DMD.h")
    message(FATAL_ERROR
        "No libdmdutil in '${dmdutil_prefix}': stage it with packaging/build-libdmdutil.sh, "
        "or leave COLORIZINGDMD_DMDUTIL_PREFIX empty to build without DMD output.")
endif()
set(dmdutil_plugin_source "${vpx_vpinball_dir}/plugins/dmdutil")
if(NOT EXISTS "${dmdutil_plugin_source}/DMDUtilPlugin.cpp")
    message(FATAL_ERROR "No DMDUtil plugin in '${dmdutil_plugin_source}'.")
endif()

add_library(DMDUtilPlugin MODULE "${dmdutil_plugin_source}/DMDUtilPlugin.cpp")
# Third-party code: its warnings are not this project's to fix.
set_target_properties(DMDUtilPlugin PROPERTIES
    CXX_STANDARD 20 PREFIX "" COMPILE_WARNING_AS_ERROR OFF)
if(MSVC)
    target_compile_options(DMDUtilPlugin PRIVATE /w)
else()
    target_compile_options(DMDUtilPlugin PRIVATE -w)
endif()
target_include_directories(DMDUtilPlugin PRIVATE
    "${vpx_vpinball_dir}/plugins"
    "${dmdutil_prefix}/include"
)
target_link_libraries(DMDUtilPlugin PRIVATE "${COLORIZINGDMD_DMDUTIL_LIBRARY}")
# The names plugin.cfg gives the library.
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(DMDUtilPlugin PROPERTIES OUTPUT_NAME plugin-dmdutil64)
    else()
        set_target_properties(DMDUtilPlugin PROPERTIES OUTPUT_NAME plugin-dmdutil)
    endif()
else()
    set_target_properties(DMDUtilPlugin PROPERTIES OUTPUT_NAME plugin-dmdutil)
endif()
if(APPLE)
    # libdmdutil and the libraries it loads lie beside the plugin. They name each other by
    # @rpath; dyld also searches the rpaths of the image that loaded them, this one.
    set_target_properties(DMDUtilPlugin PROPERTIES
        SUFFIX ".dylib"
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(DMDUtilPlugin PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "$ORIGIN")
endif()

# The plugin's folder: plugin.cfg, the plugin, libdmdutil and its libraries. On Windows the
# plugin loader adds the folder to the DLL search path while it loads the plugin.
set(dmdutil_plugin_dir "${vpx_plugin_dir}/dmdutil")
file(GLOB dmdutil_runtime_libraries
    "${dmdutil_prefix}/lib/*.dylib" "${dmdutil_prefix}/lib/*.so*" "${dmdutil_prefix}/lib/*.dll")
add_custom_command(TARGET DMDUtilPlugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${dmdutil_plugin_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${dmdutil_plugin_source}/plugin.cfg"
            "${dmdutil_plugin_dir}/plugin.cfg"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:DMDUtilPlugin>" "${dmdutil_plugin_dir}/"
    COMMAND "${CMAKE_COMMAND}" -E copy ${dmdutil_runtime_libraries} "${dmdutil_plugin_dir}/"
    VERBATIM
)
# Built with the editor, so a build always has it.
add_dependencies(colorizing_qt_v2_vpx DMDUtilPlugin)
message(STATUS "DMD output: DMDUtil plugin with libdmdutil from ${dmdutil_prefix}")
