add_library(colorizing_qt_v2_core STATIC
    document/EditorMetadata.cpp
    document/ProjectSnapshot.cpp
    document/ProjectDocument.cpp
    edit/DrawingGeometry.cpp
    edit/DrawingTools.cpp
    edit/EditCommand.cpp
    edit/FramePixelSelection.cpp
    edit/UndoHistories.cpp
    legacy/LegacyCromFrames.cpp
    legacy/LegacyCromMetadata.cpp
    legacy/LegacyCrpOriginals.cpp
    legacy/LegacyCrzArchive.cpp
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

# PRIVATE: only legacy/LegacyCrzArchive.cpp includes libserum headers, and they must not reach
# the targets that link the core.
target_link_libraries(colorizing_qt_v2_core
    PRIVATE
        serum_static
)

colorizing_qt_v2_enable_warnings(colorizing_qt_v2_core)
