add_library(colorizing_qt_v2_core STATIC
    document/ProjectSnapshot.cpp
    document/ProjectDocument.cpp
    legacy/LegacyCromFrames.cpp
    legacy/LegacyCromMetadata.cpp
    legacy/LegacyCrpOriginals.cpp
    legacy/LegacyProjectFileReader.cpp
    legacy/LegacyProjectIo.cpp
    legacy/LegacySnapshotBuilder.cpp
    render/CanvasLayout.cpp
)

target_include_directories(colorizing_qt_v2_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
