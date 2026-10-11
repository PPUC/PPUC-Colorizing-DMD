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
    DrawingToolsTests.cpp
    EditCommandTests.cpp
    LegacyCromFramesTests.cpp
    LegacyCromMetadataTests.cpp
    LegacyCrpOriginalsTests.cpp
    LegacyProjectFileReaderTests.cpp
    LegacyProjectIoTests.cpp
    LegacyProjectSerializerTests.cpp
    ProjectDocumentTests.cpp
)

target_link_libraries(colorizing_qt_v2_tests
    PRIVATE
        colorizing_qt_v2_core
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
        FramePreviewWidgetRenderTests.cpp
    )

    target_link_libraries(colorizing_qt_v2_widget_tests
        PRIVATE
            colorizing_qt_v2_widgets
    )

    colorizing_qt_v2_enable_test_warnings(colorizing_qt_v2_widget_tests)

    add_test(NAME colorizing_qt_v2_widget_tests COMMAND colorizing_qt_v2_widget_tests)
endif()
