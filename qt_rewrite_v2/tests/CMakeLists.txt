# Fixtures deliberately leave the trailing fields of document records at their defaults, so
# that one -Wextra warning is switched off again for the test executables.
function(colorizing_qt_v2_enable_test_warnings target)
    colorizing_qt_v2_enable_warnings(${target})
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
endfunction()

add_executable(colorizing_qt_v2_tests
    CanvasLayoutTests.cpp
    ColorSetTests.cpp
    CompactStorageTests.cpp
    CromcExportTests.cpp
    DrawingToolsTests.cpp
    EditCommandTests.cpp
    FrameDumpImportTests.cpp
    LegacyCromFramesTests.cpp
    LegacyCromMetadataTests.cpp
    LegacyCrpOriginalsTests.cpp
    LegacyProjectFileReaderTests.cpp
    LegacyProjectIoTests.cpp
    LegacyProjectSerializerTests.cpp
    NativeOriginalProjectTests.cpp
    PlaybackIdentificationTests.cpp
    ProjectDocumentTests.cpp
    ProjectGeometryTests.cpp
    RuntimePreviewTests.cpp
    SameFramesTests.cpp
    SectionsTests.cpp
    FrameMoveTests.cpp
    SelectionEditTests.cpp
    SpriteClipboardTests.cpp
    StateCommandsTests.cpp
    StructureCommandTests.cpp
    UpscaleTests.cpp
)

# libserum directly too: the cROMc export test loads what it wrote as a machine does.
target_link_libraries(colorizing_qt_v2_tests
    PRIVATE
        colorizing_qt_v2_core
        serum_static
)

colorizing_qt_v2_enable_test_warnings(colorizing_qt_v2_tests)

add_test(NAME colorizing_qt_v2_tests COMMAND colorizing_qt_v2_tests)

# Manual check against real projects: reports where a load + serialize differs from disk.
add_executable(colorizing_qt_v2_roundtrip
    LegacyRoundTripTool.cpp
)

target_link_libraries(colorizing_qt_v2_roundtrip
    PRIVATE
        colorizing_qt_v2_core
)

colorizing_qt_v2_enable_warnings(colorizing_qt_v2_roundtrip)

if(TARGET colorizing_qt_v2_widgets)
    add_executable(colorizing_qt_v2_widget_tests
        AppHelpersWidgetTests.cpp
        AutosaveWidgetTests.cpp
        BigOriginalWidgetTests.cpp
        CanvasEditControllerTests.cpp
        DmdOutputWidgetTests.cpp
        EditorPluginsWidgetTests.cpp
        HelperLinesWidgetTests.cpp
        PupHostWidgetTests.cpp
        SettingsWidgetTests.cpp
        HandbookWidgetTests.cpp
        ColorWidgetTests.cpp
        DumpImportWidgetTests.cpp
        EditorMatchesRuntimeTests.cpp
        FramePreviewWidgetRenderTests.cpp
        FrameTesterWidgetTests.cpp
        GifExportWidgetTests.cpp
        GifWriterWidgetTests.cpp
        NativeOriginalWidgetTests.cpp
        PlaneLabelsWidgetTests.cpp
        ProjectTaskWidgetTests.cpp
        RuntimeViewWidgetTests.cpp
        SelectionWidgetTests.cpp
        VideoClipWidgetTests.cpp
    )

    target_link_libraries(colorizing_qt_v2_widget_tests
        PRIVATE
            colorizing_qt_v2_widgets
    )

    colorizing_qt_v2_enable_test_warnings(colorizing_qt_v2_widget_tests)

    add_test(NAME colorizing_qt_v2_widget_tests COMMAND colorizing_qt_v2_widget_tests)

    # Manual check against real projects: where the editor's renderer differs from libserum.
    add_executable(colorizing_qt_v2_preview_diff
        PreviewDiffTool.cpp
    )

    target_link_libraries(colorizing_qt_v2_preview_diff
        PRIVATE
            colorizing_qt_v2_widgets
    )

    colorizing_qt_v2_enable_warnings(colorizing_qt_v2_preview_diff)
endif()
