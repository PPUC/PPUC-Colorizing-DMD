set(COLORIZING_QT_V2_APP_NAME ColorizingDMDQtV2)

add_library(colorizing_qt_v2_widgets STATIC
    Autosave.cpp
    Autosave.h
    DmdOutput.cpp
    DmdOutput.h
    CanvasEditController.cpp
    CanvasEditController.h
    ColorGridWidget.cpp
    ColorGridWidget.h
    dialogs/ColorPickerDialog.cpp
    dialogs/ColorPickerDialog.h
    dialogs/DmdOutputDialog.cpp
    dialogs/DmdOutputDialog.h
    dialogs/DumpImportDialog.cpp
    dialogs/DumpImportDialog.h
    dialogs/FrameTesterDialog.cpp
    dialogs/FrameTesterDialog.h
    dialogs/ProjectTaskDialog.cpp
    dialogs/ProjectTaskDialog.h
    dialogs/SelectionDialogs.cpp
    dialogs/SelectionDialogs.h
    EditHelpersController.cpp
    EditHelpersController.h
    EditMenuActions.cpp
    EditMenuActions.h
    EditColorKeys.cpp
    EditColorKeys.h
    FrameImageUtils.cpp
    FrameImageUtils.h
    FramePreviewWidget.cpp
    FramePreviewWidget.h
    FrameThumbnail.cpp
    GifExport.cpp
    GifExport.h
    FrameThumbnail.h
    LazyListIcons.cpp
    LazyListIcons.h
    ListSelection.cpp
    ListSelection.h
    PaintTarget.h
    PasteModeController.cpp
    PasteModeController.h
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
    panels/SectionBar.cpp
    panels/SectionBar.h
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

if(COLORIZINGDMD_DMDUTIL_PREFIX)
    get_filename_component(dmdutil_prefix "${COLORIZINGDMD_DMDUTIL_PREFIX}" ABSOLUTE)
    find_library(COLORIZINGDMD_DMDUTIL_LIBRARY NAMES dmdutil dmdutil64
        PATHS "${dmdutil_prefix}/lib" NO_DEFAULT_PATH)
    if(NOT COLORIZINGDMD_DMDUTIL_LIBRARY OR
       NOT EXISTS "${dmdutil_prefix}/include/DMDUtil/DMD.h")
        message(FATAL_ERROR
            "No libdmdutil in '${dmdutil_prefix}': stage it with packaging/build-libdmdutil.sh, "
            "or leave COLORIZINGDMD_DMDUTIL_PREFIX empty to build without DMD output.")
    endif()
    message(STATUS "DMD output with libdmdutil from ${dmdutil_prefix}")
    target_include_directories(colorizing_qt_v2_widgets SYSTEM PRIVATE "${dmdutil_prefix}/include")
    target_link_libraries(colorizing_qt_v2_widgets PRIVATE "${COLORIZINGDMD_DMDUTIL_LIBRARY}")
    target_compile_definitions(colorizing_qt_v2_widgets PRIVATE COLORIZINGDMD_HAS_DMDUTIL)
    # Linked by its full path, CMake gives everything that links it (the app, the widget
    # tests) the folder as build rpath; a package ships the libraries beside the app instead
    # (see the packaging scripts). Windows finds DLLs on PATH or beside the exe.
    set(COLORIZINGDMD_DMDUTIL_LIBRARY_DIR "${dmdutil_prefix}/lib" CACHE INTERNAL "")
else()
    message(STATUS "DMD output: off (no COLORIZINGDMD_DMDUTIL_PREFIX)")
endif()

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

if(COLORIZINGDMD_QT_V2_PACKAGE)
    # The target keeps its name; only the file (and bundle) is called ColorizingDMD.
    # WIN32_EXECUTABLE: no console window; Qt6::Core links its WinMain entry point for it.
    set_target_properties(${COLORIZING_QT_V2_APP_NAME} PROPERTIES
        OUTPUT_NAME ColorizingDMD
        WIN32_EXECUTABLE ON
    )
    if(APPLE)
        set_target_properties(${COLORIZING_QT_V2_APP_NAME} PROPERTIES
            MACOSX_BUNDLE ON
            MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME ColorizingDMD
            MACOSX_BUNDLE_GUI_IDENTIFIER org.ppuc.colorizingdmd
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        )
    endif()
endif()
