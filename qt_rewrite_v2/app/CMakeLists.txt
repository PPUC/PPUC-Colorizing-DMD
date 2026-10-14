set(COLORIZING_QT_V2_APP_NAME ColorizingDMDQtV2)

add_library(colorizing_qt_v2_widgets STATIC
    CanvasEditController.cpp
    CanvasEditController.h
    ColorGridWidget.cpp
    ColorGridWidget.h
    dialogs/ColorPickerDialog.cpp
    dialogs/ColorPickerDialog.h
    dialogs/DumpImportDialog.cpp
    dialogs/DumpImportDialog.h
    EditColorKeys.cpp
    EditColorKeys.h
    FrameImageUtils.cpp
    FrameImageUtils.h
    FramePreviewWidget.cpp
    FramePreviewWidget.h
    FrameThumbnail.cpp
    FrameThumbnail.h
    LazyListIcons.cpp
    LazyListIcons.h
    ListSelection.cpp
    ListSelection.h
    PaintTarget.h
    PlaneLabels.cpp
    PlaneLabels.h
    RuntimeView.cpp
    RuntimeView.h
    panels/BackgroundPanel.cpp
    panels/BackgroundPanel.h
    panels/ColorRotationPanel.cpp
    panels/ColorRotationPanel.h
    panels/ColorSetPanel.cpp
    panels/ColorSetPanel.h
    panels/DynamicPanel.cpp
    panels/DynamicPanel.h
    panels/EditColorBar.cpp
    panels/EditColorBar.h
    panels/FramePanel.cpp
    panels/FramePanel.h
    panels/MaskPanel.cpp
    panels/MaskPanel.h
    panels/PalettePanel.cpp
    panels/PalettePanel.h
    panels/RecordListPage.cpp
    panels/RecordListPage.h
    panels/SameFramesPanel.cpp
    panels/SameFramesPanel.h
    panels/SpritePanel.cpp
    panels/SpritePanel.h
    SpriteEditorWidget.cpp
    SpriteEditorWidget.h
    UnsavedChanges.cpp
    UnsavedChanges.h
)

target_link_libraries(colorizing_qt_v2_widgets
    PUBLIC
        colorizing_qt_v2_core
        Qt6::Widgets
)

colorizing_qt_v2_enable_warnings(colorizing_qt_v2_widgets)

target_include_directories(colorizing_qt_v2_widgets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(colorizing_qt_v2_widgets
    PUBLIC
        QT_NO_KEYWORDS
)

add_executable(${COLORIZING_QT_V2_APP_NAME}
    MainWindow.cpp
    MainWindow.h
    main.cpp
)

target_link_libraries(${COLORIZING_QT_V2_APP_NAME}
    PRIVATE
        colorizing_qt_v2_widgets
        Qt6::Widgets
)

colorizing_qt_v2_enable_warnings(${COLORIZING_QT_V2_APP_NAME})

target_compile_definitions(${COLORIZING_QT_V2_APP_NAME}
    PRIVATE
        QT_NO_KEYWORDS
)
