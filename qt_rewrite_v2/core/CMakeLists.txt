add_library(colorizing_qt_v2_core STATIC
    document/EditorMetadata.cpp
    document/ProjectSnapshot.cpp
    document/ProjectDocument.cpp
    edit/DrawingGeometry.cpp
    edit/DrawingTools.cpp
    edit/EditCommand.cpp
    edit/UndoHistories.cpp
    legacy/LegacyCromFrames.cpp
    legacy/LegacyCromMetadata.cpp
    legacy/LegacyCrpOriginals.cpp
    legacy/LegacyProjectFileReader.cpp
    legacy/LegacyProjectFileWriter.cpp
    legacy/LegacyProjectIo.cpp
    legacy/LegacyProjectSerializer.cpp
    legacy/LegacySnapshotBuilder.cpp
    render/CanvasLayout.cpp
)

target_include_directories(colorizing_qt_v2_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
