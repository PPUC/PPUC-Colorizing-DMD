add_library(colorizing_qt_v2_core STATIC
    document/CompactArray.cpp
    document/EditorMetadata.cpp
    document/ProjectSnapshot.cpp
    document/Upscale.cpp
    document/ProjectDocument.cpp
    edit/AutoCopy.cpp
    edit/ColorModel.cpp
    edit/ColorSetEdits.cpp
    edit/CopyPaste.cpp
    edit/DrawingGeometry.cpp
    edit/DrawingTools.cpp
    edit/EditBatch.cpp
    edit/EditCommand.cpp
    edit/FrameContentCommand.cpp
    edit/FrameMove.cpp
    edit/FrameTags.cpp
    edit/FramePixelSelection.cpp
    edit/HelperLines.cpp
    edit/PlaneSelection.cpp
    edit/RecolorHelpers.cpp
    edit/SameFrames.cpp
    edit/SceneEdits.cpp
    edit/SpeakerLightEdits.cpp
    edit/SpriteClipboard.cpp
    edit/ScrollFill.cpp
    edit/SourceFrame.cpp
    edit/Sections.cpp
    edit/StateCommands.cpp
    edit/StructureCommand.cpp
    edit/UndoHistories.cpp
    export/GifWriter.cpp
    import/DumpFile.cpp
    import/FrameDumpImport.cpp
    import/FrameDumpReader.cpp
    legacy/CrpxFile.cpp
    legacy/LegacyByteSource.cpp
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
    render/ProjectGeometry.cpp
    runtime/FrameIdentification.cpp
    runtime/CromcExport.cpp
    runtime/FrameTester.cpp
    runtime/RuntimePreview.cpp
    runtime/Scenes.cpp
    runtime/SpeakerLightPlayer.cpp
)

target_include_directories(colorizing_qt_v2_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# PRIVATE: only document/CompactArray.cpp (lz4), legacy/LegacyCrzArchive.cpp, runtime/CromcExport.cpp,
# runtime/RuntimePreview.cpp and runtime/Scenes.cpp include libserum headers, and
# import/DumpFile.cpp its miniz, and they must not reach the targets that
# link the core.
target_link_libraries(colorizing_qt_v2_core
    PRIVATE
        serum_static
        ws2812fx_desktop
)

colorizing_qt_v2_enable_warnings(colorizing_qt_v2_core)
