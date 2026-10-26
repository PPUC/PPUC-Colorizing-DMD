# DEPRECATED (tests/legacy-writers/README.md): the writers of the legacy files the editor no
# longer writes, kept only for the tests that still make legacy projects with them.
add_library(colorizing_legacy_writers STATIC
    legacy/CrpxWriter.cpp
    legacy/CrpxWriter.h
    legacy/CrzWriter.cpp
    legacy/CrzWriter.h
    legacy/LegacyCrpWriter.cpp
    legacy/LegacyCrpWriter.h
    legacy/LegacyProjectFileWriter.cpp
    legacy/LegacyProjectFileWriter.h
)

target_include_directories(colorizing_legacy_writers
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# libserum privately for its miniz (the .cRZ).
target_link_libraries(colorizing_legacy_writers
    PUBLIC
        colorizing_qt_v2_core
    PRIVATE
        serum_static
)

colorizing_qt_v2_enable_warnings(colorizing_legacy_writers)
