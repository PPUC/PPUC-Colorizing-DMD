add_executable(colorizing_qt_v2_tests
    CanvasLayoutTests.cpp
    LegacyCromFramesTests.cpp
    LegacyCromMetadataTests.cpp
    LegacyCrpOriginalsTests.cpp
    LegacyProjectFileReaderTests.cpp
    LegacyProjectIoTests.cpp
    ProjectDocumentTests.cpp
)

target_link_libraries(colorizing_qt_v2_tests
    PRIVATE
        colorizing_qt_v2_core
)

add_test(NAME colorizing_qt_v2_tests COMMAND colorizing_qt_v2_tests)

if(TARGET colorizing_qt_v2_widgets)
    add_executable(colorizing_qt_v2_widget_tests
        FramePreviewWidgetRenderTests.cpp
    )

    target_link_libraries(colorizing_qt_v2_widget_tests
        PRIVATE
            colorizing_qt_v2_widgets
    )

    add_test(NAME colorizing_qt_v2_widget_tests COMMAND colorizing_qt_v2_widget_tests)
endif()
