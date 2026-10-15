# The editor's side of VPX plugin hosting (VpxHost), and the plugins built with the editor.
#
# colorizing_qt_v2_vpx is the only target that sees the plugin SDK: its header has no SDK
# types, so the rest of the editor stays C++17 while this target, like the SDK, is C++20.

add_library(colorizing_qt_v2_vpx STATIC
    EditorApi.cpp
    EditorApi.h
    EditorApiHost.h
    PupHost.cpp
    PupHost.h
    VpxHost.cpp
    VpxHost.h
)
target_include_directories(colorizing_qt_v2_vpx PUBLIC "${PROJECT_SOURCE_DIR}/app")
target_link_libraries(colorizing_qt_v2_vpx PUBLIC Qt6::Core Qt6::Gui PRIVATE colorizing_qt_v2_core)
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
set(dmdutil_runtime_libraries
    "${dmdutil_prefix}/lib/*.dylib" "${dmdutil_prefix}/lib/*.so*" "${dmdutil_prefix}/lib/*.dll")
add_custom_command(TARGET DMDUtilPlugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${dmdutil_plugin_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${dmdutil_plugin_source}/plugin.cfg"
            "${dmdutil_plugin_dir}/plugin.cfg"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:DMDUtilPlugin>" "${dmdutil_plugin_dir}/"
    COMMAND "${CMAKE_COMMAND}" "-DPATTERNS=$<JOIN:${dmdutil_runtime_libraries},|>"
            "-DDESTINATION=${dmdutil_plugin_dir}" -P "${CMAKE_CURRENT_SOURCE_DIR}/CopyLibraries.cmake"
    VERBATIM
)
# Built with the editor, so a build always has it.
add_dependencies(colorizing_qt_v2_vpx DMDUtilPlugin)
message(STATUS "DMD output: DMDUtil plugin with libdmdutil from ${dmdutil_prefix}")

# ---- VPX's PUP plugin (Pin Up Player), for the PuP tester ----------------------------------
# Built from the vpinball checkout against the media libraries packaging/build-media-libs.sh
# installs (SDL3, SDL3_image, SDL3_ttf, ffmpeg) and the libpupdmd staged with libdmdutil, into
# <build>/vpx-plugins/pup with its plugin.cfg and those libraries. The same ffmpeg serves the
# editor's movie clip import.
set(COLORIZINGDMD_MEDIA_PREFIX "" CACHE PATH
    "Media libraries from packaging/build-media-libs.sh; empty builds without PuP")
if(NOT COLORIZINGDMD_MEDIA_PREFIX)
    message(STATUS "PuP: off (no COLORIZINGDMD_MEDIA_PREFIX)")
    return()
endif()
get_filename_component(media_prefix "${COLORIZINGDMD_MEDIA_PREFIX}" ABSOLUTE)
if(NOT EXISTS "${media_prefix}/include/SDL3/SDL.h" OR
   NOT EXISTS "${media_prefix}/include/libavformat/avformat.h")
    message(FATAL_ERROR
        "No media libraries in '${media_prefix}': build them with "
        "packaging/build-media-libs.sh, or leave COLORIZINGDMD_MEDIA_PREFIX empty.")
endif()
if(NOT EXISTS "${dmdutil_prefix}/include/pupdmd.h")
    message(FATAL_ERROR "No pupdmd.h in '${dmdutil_prefix}': stage libdmdutil again.")
endif()
set(pup_plugin_source "${vpx_vpinball_dir}/plugins/pup")
if(NOT EXISTS "${pup_plugin_source}/PUPPlugin.cpp")
    message(FATAL_ERROR "No PUP plugin in '${pup_plugin_source}'.")
endif()

file(GLOB pup_plugin_sources "${pup_plugin_source}/*.cpp")
add_library(PUPPlugin MODULE
    ${pup_plugin_sources}
    "${vpx_vpinball_dir}/plugins/plugins/B2SPluginEventStream.cpp"
)
# Third-party code: its warnings are not this project's to fix.
set_target_properties(PUPPlugin PROPERTIES
    CXX_STANDARD 20 PREFIX "" COMPILE_WARNING_AS_ERROR OFF)
if(MSVC)
    target_compile_options(PUPPlugin PRIVATE /w)
else()
    target_compile_options(PUPPlugin PRIVATE -w)
endif()
# The media prefix first: the vpinball checkout's third-party/include may hold other versions
# of the same headers (a tree vpinball's own build staged). From there only RSJparser and
# unordered_dense.
target_include_directories(PUPPlugin PRIVATE
    "${media_prefix}/include"
    "${dmdutil_prefix}/include"
    "${vpx_vpinball_dir}/plugins"
    "${pup_plugin_source}"
    "${vpx_vpinball_dir}/third-party/include"
)
find_library(pup_sdl3 NAMES SDL3 PATHS "${media_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(pup_sdl3_image NAMES SDL3_image PATHS "${media_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(pup_sdl3_ttf NAMES SDL3_ttf PATHS "${media_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(pup_pupdmd NAMES pupdmd pupdmd64 PATHS "${dmdutil_prefix}/lib" NO_DEFAULT_PATH
    REQUIRED)
target_link_libraries(PUPPlugin PRIVATE ${pup_sdl3} ${pup_sdl3_image} ${pup_sdl3_ttf} ${pup_pupdmd})
if(NOT WIN32)
    # On Windows the plugin loads ffmpeg itself when it plays a video (LibAv.h).
    foreach(lib avcodec avformat avutil swresample swscale)
        find_library(pup_${lib} NAMES ${lib} PATHS "${media_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(PUPPlugin PRIVATE ${pup_${lib}})
    endforeach()
endif()
if(WIN32)
    set_target_properties(PUPPlugin PROPERTIES OUTPUT_NAME plugin-pup64)
else()
    set_target_properties(PUPPlugin PROPERTIES OUTPUT_NAME plugin-pup)
endif()
if(APPLE)
    set_target_properties(PUPPlugin PROPERTIES
        SUFFIX ".dylib"
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(PUPPlugin PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "$ORIGIN")
endif()

# The plugin's folder: plugin.cfg, the plugin, the media libraries and libpupdmd.
set(pup_plugin_dir "${vpx_plugin_dir}/pup")
set(pup_runtime_libraries
    "${media_prefix}/lib/*.dylib" "${media_prefix}/lib/*.so*" "${media_prefix}/bin/*.dll"
    "${dmdutil_prefix}/lib/libpupdmd*.dylib" "${dmdutil_prefix}/lib/libpupdmd.so*"
    "${dmdutil_prefix}/lib/*pupdmd*.dll")
add_custom_command(TARGET PUPPlugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${pup_plugin_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${pup_plugin_source}/plugin.cfg" "${pup_plugin_dir}/plugin.cfg"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:PUPPlugin>" "${pup_plugin_dir}/"
    COMMAND "${CMAKE_COMMAND}" "-DPATTERNS=$<JOIN:${pup_runtime_libraries},|>"
            "-DDESTINATION=${pup_plugin_dir}" -P "${CMAKE_CURRENT_SOURCE_DIR}/CopyLibraries.cmake"
    VERBATIM
)
add_dependencies(colorizing_qt_v2_vpx PUPPlugin)
message(STATUS "PuP: PUP plugin with media libraries from ${media_prefix}")
