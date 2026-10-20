set(COLORIZING_QT_V2_APP_NAME ColorizingDMDQtV2)

add_library(colorizing_qt_v2_widgets STATIC
    ActivityClock.h
    Appearance.cpp
    Appearance.h
    Autosave.cpp
    Autosave.h
    DmdOutput.cpp
    DmdOutput.h
    CanvasEditController.cpp
    CanvasHistory.cpp
    CanvasHistory.h
    CanvasEditController.h
    ColorGridWidget.cpp
    ColorGridWidget.h
    dialogs/ColorPickerDialog.cpp
    dialogs/ColorPickerDialog.h
    dialogs/DmdOutputSettingsPage.cpp
    dialogs/DmdOutputSettingsPage.h
    dialogs/DumpImportDialog.cpp
    dialogs/AboutDialog.cpp
    dialogs/AboutDialog.h
    dialogs/HelperLineDialog.cpp
    dialogs/HelperLineDialog.h
    dialogs/ProjectSettingsDialog.cpp
    dialogs/ProjectSettingsDialog.h
    dialogs/SceneDialog.cpp
    dialogs/SceneDialog.h
    dialogs/ImageImportDialog.cpp
    dialogs/ImageImportDialog.h
    dialogs/DumpImportDialog.h
    dialogs/FrameTesterDialog.cpp
    dialogs/FrameTesterDialog.h
    dialogs/ProjectTaskDialog.cpp
    dialogs/SettingsDialog.cpp
    dialogs/SettingsDialog.h
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
    FlowLayout.cpp
    FlowLayout.h
    FrameListWidget.cpp
    FrameListWidget.h
    FrameImageUtils.h
    FramePreviewWidget.cpp
    FramePreviewWidget.h
    FrameThumbnail.cpp
    GifExport.cpp
    HandbookWindow.cpp
    HandbookWindow.h
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
    SectionStack.cpp
    SectionStack.h
    RuntimeView.h
    panels/HelperLinesPanel.cpp
    panels/HelperLinesPanel.h
    panels/InspectorForm.h
    panels/ProjectSettingsPanel.cpp
    panels/ProjectSettingsPanel.h
    panels/ScenesPanel.cpp
    panels/ScenesPanel.h
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
    EditorPlugins.cpp
    EditorPlugins.h
    KeyNames.cpp
    KeyNames.h
    ImageImport.cpp
    ImageImport.h
    MovieImport.cpp
    MovieImport.h
    PluginLogWindow.cpp
    PluginLogWindow.h
    PupTesterWindow.cpp
    PupTesterWindow.h
    SpriteEditorWidget.cpp
    SpriteEditorWidget.h
    Translations.cpp
    Translations.h
    VideoClip.cpp
    VideoClip.h
    UnsavedChanges.cpp
    UnsavedChanges.h
)

# The handbook, every language's chapters, built in under :/handbook/<language>/ for
# Help -> Handbook (HandbookWindow).
file(GLOB colorizing_qt_v2_handbook_files
    CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/handbook/*/[0-9]*.md")
qt_add_resources(colorizing_qt_v2_widgets "handbook"
    PREFIX "/handbook"
    BASE "${PROJECT_SOURCE_DIR}/handbook"
    FILES ${colorizing_qt_v2_handbook_files})

target_link_libraries(colorizing_qt_v2_widgets
    PUBLIC
        colorizing_qt_v2_core
        colorizing_qt_v2_vpx
        Qt6::Widgets
)

add_subdirectory(vpx)

# Movie clip import: ffmpeg's headers from the media libraries (packaging/build-media-libs.sh);
# the libraries themselves are loaded at run time from the PUP plugin's folder (VideoClip).
if(COLORIZINGDMD_MEDIA_PREFIX)
    target_include_directories(colorizing_qt_v2_widgets SYSTEM PRIVATE
        "${COLORIZINGDMD_MEDIA_PREFIX}/include")
    target_compile_definitions(colorizing_qt_v2_widgets PRIVATE COLORIZINGDMD_HAS_VIDEO_IMPORT)
endif()

colorizing_qt_v2_enable_warnings(colorizing_qt_v2_widgets)

target_include_directories(colorizing_qt_v2_widgets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(colorizing_qt_v2_widgets
    PUBLIC
        QT_NO_KEYWORDS
        COLORIZINGDMD_VERSION="${PROJECT_VERSION}"
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

# The application icon: in the window (:/icons, main.cpp), in the .exe on Windows, in the
# bundle on macOS. Made from packaging/icon/colorizingdmd-1024.png.
qt_add_resources(${COLORIZING_QT_V2_APP_NAME} "app_icons"
    PREFIX "/"
    FILES
        icons/colorizingdmd-64.png
        icons/colorizingdmd-256.png
        icons/colorizingdmd-512.png
)
if(WIN32)
    target_sources(${COLORIZING_QT_V2_APP_NAME}
        PRIVATE "${PROJECT_SOURCE_DIR}/packaging/windows/ColorizingDMD.rc")
endif()

colorizing_qt_v2_enable_warnings(${COLORIZING_QT_V2_APP_NAME})

# The translations of the user interface (Translations.h), built in under :/i18n. Qt Linguist
# edits translations/colorizingdmd_<language>.ts; the target update_translations runs lupdate
# over all sources of the user interface to add new and changed texts to them.
if(COLORIZINGDMD_QT_V2_PACKAGE)
    # A package has the translations.
    find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
else()
    find_package(Qt6 QUIET COMPONENTS LinguistTools)
endif()
if(Qt6LinguistTools_FOUND)
    file(GLOB colorizing_qt_v2_ui_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/dialogs/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/dialogs/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/panels/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/panels/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/vpx/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/vpx/*.h")
    qt_add_translations(${COLORIZING_QT_V2_APP_NAME}
        TS_FILES
            "${PROJECT_SOURCE_DIR}/translations/colorizingdmd_de.ts"
            "${PROJECT_SOURCE_DIR}/translations/colorizingdmd_fr.ts"
        SOURCES ${colorizing_qt_v2_ui_sources}
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
        RESOURCE_PREFIX "/i18n"
        LUPDATE_OPTIONS -locations none)
    # Qt's own translations (standard buttons, file dialogs) of the same languages, built in
    # as well when this Qt has them, so that no package has to ship them.
    set(colorizing_qt_v2_qt_translations "")
    foreach(language de fr)
        set(file "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_${language}.qm")
        if(EXISTS "${file}")
            list(APPEND colorizing_qt_v2_qt_translations "${file}")
        endif()
    endforeach()
    if(colorizing_qt_v2_qt_translations)
        qt_add_resources(${COLORIZING_QT_V2_APP_NAME} "qt_translations"
            PREFIX "/i18n"
            BASE "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}"
            FILES ${colorizing_qt_v2_qt_translations})
    endif()
else()
    message(WARNING "Qt6 LinguistTools not found: the editor is built in English only")
endif()

target_compile_definitions(${COLORIZING_QT_V2_APP_NAME}
    PRIVATE
        QT_NO_KEYWORDS
)

# The target keeps its name; the file is called PPUC-Colorizing-DMD. On macOS the editor is an
# app bundle in every build, "PPUC Colorizing DMD.app": the Dock, its tooltip and the menu bar
# name it after the bundle (a program outside one gets the name of its folder).
set_target_properties(${COLORIZING_QT_V2_APP_NAME} PROPERTIES OUTPUT_NAME PPUC-Colorizing-DMD)
if(APPLE)
    set_target_properties(${COLORIZING_QT_V2_APP_NAME} PROPERTIES
        OUTPUT_NAME "PPUC Colorizing DMD"
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "PPUC Colorizing DMD"
        MACOSX_BUNDLE_GUI_IDENTIFIER org.ppuc.colorizingdmd
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE ColorizingDMD.icns
    )
    set(colorizing_qt_v2_icns "${PROJECT_SOURCE_DIR}/packaging/macos/ColorizingDMD.icns")
    set_source_files_properties("${colorizing_qt_v2_icns}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${COLORIZING_QT_V2_APP_NAME} PRIVATE "${colorizing_qt_v2_icns}")
endif()

if(COLORIZINGDMD_QT_V2_PACKAGE)
    # WIN32_EXECUTABLE: no console window; Qt6::Core links its WinMain entry point for it.
    set_target_properties(${COLORIZING_QT_V2_APP_NAME} PROPERTIES
        WIN32_EXECUTABLE ON
    )
endif()
