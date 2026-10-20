#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>

#include "ActivityClock.h"
#include "Autosave.h"
#include "DmdOutput.h"
#include "EditorPlugins.h"
#include "dialogs/SettingsDialog.h"
#include "CanvasEditController.h"
#include "EditHelpersController.h"
#include "PasteModeController.h"
#include "document/ProjectDocument.h"
#include "edit/DrawingTools.h"
#include "edit/FrameMove.h"
#include "edit/SameFrames.h"
#include "edit/SceneEdits.h"
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"
#include "FrameImageUtils.h"
#include "RuntimeView.h"
#include "CanvasHistory.h"
#include "PaintTarget.h"
#include "SectionStack.h"
#include "UnsavedChanges.h"
#include "panels/FramePanel.h"

namespace colorizing::app {
class BackgroundPanel;
class ColorRotationPanel;
class ColorSetPanel;
class DynamicPanel;
class EditColorBar;
class EditMenuActions;
class FrameListWidget;
class HandbookWindow;
class PluginLogWindow;
class VideoClip;
class PupTesterWindow;
class FramePanel;
class SectionBar;
class HelperLinesPanel;
class ScenesPanel;
class SceneSettingsPanel;
class FramePreviewWidget;
class LazyListIcons;
class MaskPanel;
class PalettePanel;
class RecordListPage;
class SameFramesPanel;
class SpritePanel;
class SpriteEditorWidget;
}

class QLabel;
class QListWidget;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QAction;
class QCloseEvent;
class QListWidgetItem;
class QMenu;
class QComboBox;
class QDialog;
class QVBoxLayout;
class QCheckBox;
class QSpinBox;
class QTimer;

namespace colorizing::app {

class MainWindow final : public QMainWindow {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // At start: offers the newest autosave of a project that was never saved, if there is one.
    void offerUntitledAutosave();

protected:
    // Asks about unsaved changes first, see confirmClosingProject().
    void closeEvent(QCloseEvent* event) override;
    // The edit color keys while the frame canvas has the focus, see EditColorKeys.h.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    using DrawingTool = edit::DrawingTool;

    enum class FrameFilter {
        All,
        // Offered where HD is an extra resolution only.
        Hd,
        Background,
        Dynamic,
        Sprites,
        // Frames with a color rotation (a slot of length above 0) on a plane they have.
        Rotations,
        // Offered for HD-only projects alone, as "Not rendered": frames the runtime shows
        // nothing for.
        HdIncomplete,
        // Frames a save flags inactive and the runtime skips, see
        // legacy::isFrameSkippedAtRuntime().
        Inactive,
        // Frames that use the asset selected on the left: the mask, background or sprite of
        // the tab that is open there.
        UsesSelectedAsset,
        // Frames of the project's scenes, and the others.
        SceneFrames,
        WithoutSceneFrames,
    };

    void createActions();
    // After the selected mask, background or sprite, or the tab on the left, changed: the frame
    // strip follows when it shows the frames that use the selected asset.
    void refreshSelectedAssetFilter();
    void createCentralView();
    void openProject();
    // File -> New Project From Dump and Add Frames From Dump, see DumpImportDialog.
    void newProjectFromDump();
    void addFramesFromDump();
    // Asks (Save / Discard / Cancel) before a project with unsaved changes is closed or
    // replaced; true if it may be. action names what is about to happen, for the question.
    // See mayCloseProject().
    [[nodiscard]] bool confirmClosingProject(const QString& action);
    [[nodiscard]] UnsavedChangesAnswer askAboutUnsavedChanges(const QString& action);
    void openRecentProject(const QString& entry);
    // A project that has no files yet (a new project) asks for them, as Save As does.
    // Both return whether the project was written.
    bool saveProject();
    bool saveProjectAs();
    [[nodiscard]] bool isUntitledProject() const;
    void updateWindowTitle();
    // Writes the project's .cROMc through libserum, see runtime::exportCromc().
    void exportCromc();
    // See FrameTesterDialog.
    void openFrameTester();
    // The selected frames as an animated GIF, see GifExport.h.
    // withOriginals: each original frame above its colorized picture; bothPlanes: the 128x32
    // and the 256x64 picture (GifExportOptions).
    void exportAnimatedGif(bool withOriginals = false, bool bothPlanes = false);
    // View -> Output To DMD, see DmdOutput.h.
    void setDmdOutputEnabled(bool enabled);
    // File -> Settings (the application menu on macOS), opened at the page.
    void editSettings(SettingsDialog::Page page);
    void sendDmdPicture();
    // Copy and Paste on the sprite canvas, see edit/SpriteClipboard.h.
    [[nodiscard]] render::Resolution spriteCanvasPlane(std::size_t spriteIndex) const;
    void copySpriteToClipboard();
    // The sprite canvas in dynamic paint mode: pixels into the sprite panel's dynamic set, or
    // out of any (erase).
    void paintSpriteDynamic(std::size_t spriteIndex,
                            const std::vector<render::Point>& pixels,
                            bool erase);
    void pasteClipboardIntoSprite();
    // Writes libserum's output for the current frame, see RuntimeView.
    void exportCurrentFramePng();
    void importImageIntoSelectedFrames();
    void importImageFromClipboard();
    // File -> Import Movie Clip Into Selected Frames: a picture of a clip per selected frame.
    void importMovieIntoSelectedFrames();
    // The Images window (ImageImportDialog) for a still picture or a clip, and its result
    // written into the selection of the selected frames or into the selected background, on
    // the chosen planes (HD created where it is missing).
    void importPicture(const QImage& still, const QString& name, VideoClip* clip,
                       bool toBackground);
    // Offers the project's autosave when it is newer than the project files (see Autosave.h).
    void loadProject(const QString& cromPath, const QString& crpPath);
    // Reads the project from readFrom (an autosave) instead; it is then the project at
    // cromPath/crpPath with unsaved changes (an untitled one when both are empty).
    void loadProjectFrom(const AutosaveFiles& readFrom,
                         const QString& cromPath,
                         const QString& crpPath);
    void loadProjectFiles(const QString& readCromPath,
                          const QString& readCrpPath,
                          const QString& cromPath,
                          const QString& crpPath,
                          bool recovered);
    [[nodiscard]] std::optional<Autosaver::Job> autosaveJob();
    void autosaveFinished(const AutosaveFiles& files,
                          uint64_t revision,
                          const legacy::LegacySaveResult& result);
    // Changes were discarded: the autosave holding them is not offered again.
    void forgetAutosave();
    void rememberRecentProject(const QString& cromPath, const QString& crpPath);
    void refreshRecentProjectsMenu();
    void setStatusText(const QString& text);
    void setFrameStatusText(const QString& text);
    void refreshProjectSummary();
    void refreshFrameList(std::optional<std::size_t> selectedFrameIndex = std::nullopt);
    [[nodiscard]] QString frameListItemText(std::size_t frameIndex) const;
    // The frame list row of a frame, through m_frameRows; nullptr when it is not listed.
    [[nodiscard]] QListWidgetItem* frameListItem(std::size_t frameIndex) const;
    void rebuildFrameRows();
    // References of the frame the runtime cannot follow (dangling IDs, a gap in the sprite
    // slots), one line each; empty if there are none.
    [[nodiscard]] QStringList frameReferenceProblemTexts(std::size_t frameIndex) const;
    // See ProjectDocument::rendersHdPlaneOnly(): there are no SD mode and no SD fallbacks, and
    // the 256x64 data cannot be removed.
    [[nodiscard]] bool isHdOnlyProject() const;
    // See ProjectDocument::editsOriginalPlaneOnly(): there is no extra plane at all.
    [[nodiscard]] bool isOriginalPlaneOnlyProject() const;
    // Whether "HD" and "SD" are shown at all, see PlaneLabels.h.
    [[nodiscard]] bool showsHdAndSd() const;
    // "SD" or "HD"; the plane's size in a project with one plane.
    [[nodiscard]] QString planeName(render::Resolution resolution) const;
    // Why the runtime shows nothing for the frame of an HD-only project; empty otherwise.
    [[nodiscard]] QString frameHdGapsText(std::size_t frameIndex) const;
    // The gaps of frameHdGapsText() and what "inactive" means, for a frame list item.
    [[nodiscard]] QString frameListItemToolTip(std::size_t frameIndex) const;
    // A frame's "inactive" marker in the list and the status line follows an edit.
    void updateInactiveFrameMarker(std::size_t frameIndex);
    void updateFrameFilterChoices();
    [[nodiscard]] bool frameMatchesFilter(std::size_t frameIndex) const;
    [[nodiscard]] std::optional<std::size_t> currentFrameIndex() const;
    [[nodiscard]] std::vector<std::size_t> selectedFrameIndices() const;
    void selectFrame(int frameIndex);
    void updateResolutionMode();
    // Enabled state of the SD/HD, overlay and tool buttons: off while the Runtime view is on.
    void updateCanvasModeButtons();
    void setDrawingTool(DrawingTool tool);
    void setActiveDrawColor(uint16_t rgb565);
    void updateFrameMetadataControls();
    // The section and trigger boxes above the frame list, see SectionBar.
    void updateSectionControls();
    void recordSectionsEdit(std::unique_ptr<edit::EditCommand> command, const QString& status);
    void nameSectionAtCurrentFrame();
    void deleteCurrentSection();
    void orderSections();
    void updateMaskControls();
    // The comparison side of the Mask page, see SameFramesPanel. The same frames are worked
    // out only while the page is shown.
    void updateSameFrameControls();
    void updateSameFrameCounts();
    void updateSpotCommonOverlay();
    enum class SameFrameDeletion {
        SameAsCurrent,
        SameAsSelected,
        AllDuplicates,
    };
    void deleteSameFrames(SameFrameDeletion deletion);
    void writeCommonPixelsToMask();
    void showFramesUsingMask();
    void selectFramesUsingMask();
    // Makes the frame the current one if the frame filter lists it.
    void goToFrame(std::size_t frameIndex);
    void updateBackgroundControls();
    void updatePaintTargetMode();
    void setPaintTarget(PaintTarget target);
    void paintTargetChanged();
    void setCurrentFrameComparisonMaskId(int maskId);
    void setCurrentFrameShapeComparisonMode(int mode);
    void setCurrentFrameBackgroundId(int backgroundId);
    // Sets the trigger of that kind on the selected frames, skipping frames that have one of
    // another kind; kNoTriggerId clears it on the frames that have one of that kind.
    void setSelectedFramesTrigger(document::TriggerKind kind, uint32_t triggerId);
    void setCurrentFrameDurationMs(int durationMs);
    struct FrameEditCount {
        int changed = 0;
        int skipped = 0;
    };
    // Sets the attribute on every selected frame, one undoable command per frame. Frames that
    // already have the value are left alone; frames the attribute cannot apply to are skipped.
    // skip: frames the edit leaves alone, counted as skipped.
    FrameEditCount editSelectedFramesAttribute(
        edit::FrameAttribute attribute,
        uint32_t value,
        const std::function<bool(std::size_t frameIndex)>& skip = {});
    void updateFrameStatusText();
    void leaveSpriteCanvas();
    void assignSelectedMaskToFrames();
    void editAssignedMask();
    void assignSelectedBackgroundToFrames();
    // Edit BG and a double click on a background: the background on the canvas. fromFrame (Edit
    // BG): the current frame's own background when it has one, else the selected one.
    void editAssignedBackground(bool fromFrame = false);
    // Ends editing a background: the current frame is edited again.
    void editFrameInsteadOfBackground();
    void copyCurrentFrameToSelectedBackground();
    void copySelectedBackgroundToFrames();
    void importImageIntoSelectedBackground();
    // The Background section's Create HD and Remove HD, for the selected background.
    void createSelectedBackgroundHd();
    // Shows only the right sidebar's sections that serve the canvas shown: the frame, a
    // background being edited, or the sprite editor.
    void updateInspectorSections();
    // The left sidebar's Scene section: shown with the scene of the frame on the canvas.
    // reload: the scene changed (unapplied changes go).
    void updateSceneSettingsSection(bool reload);
    // Edit Scene and the Scene section's Apply: the scene as given; its frames follow (after a
    // question when frames go).
    void changeScene(uint16_t sceneId, const document::SceneDefinition& scene);
    // The toolbar's < and >: what the canvas showed before (frames, backgrounds, sprites).
    [[nodiscard]] CanvasPlace currentCanvasPlace() const;
    void noteCanvasPlace(bool frameRested = false);
    void goBackOrForward(bool back);
    void showCanvasPlace(const CanvasPlace& place);
    void updateHistoryButtons();
    // Leave Scene: back to the latest place whose timeline is not a scene's.
    void leaveSceneTimeline();
    // The Filter row of the Create HD windows, set to the filter used last.
    QComboBox* addUpscaleFilterRow(QDialog& dialog, QVBoxLayout& layout) const;
    // Asks for the filter; false when cancelled. The choice becomes m_createHdFilter.
    bool askUpscaleFilter(const QString& title, const QString& text);
    void removeSelectedBackgroundHd();
    void exportSelectedBackgroundPng();
    void assignSelectedSpriteToCurrentSlot();
    void captureSelectedSpriteFromCurrentFrame();
    void removeCurrentSpriteSlot();
    void setCurrentSpriteSlotBounds();
    void setCurrentSpriteName(const QString& name);
    void setCurrentSpriteDetectionArea();
    void clearCurrentSpriteDetectionArea();
    void createCurrentSpriteHd();
    void removeCurrentSpriteHd();
    void createCurrentFrameHd();
    // HD-only project: creates what the frame lacks, without a dialog; nothing is upscaled.
    void createSelectedFramesHdFromOriginal();
    void removeCurrentFrameHd();
    void updateDynamicColorControls();
    void updateDynamicShadowControls();
    void dynamicSetChanged(int setIndex);
    // color: the new color (dropped from the palette); none asks for it.
    void editDynamicColor(std::size_t originalColorIndex,
                          std::optional<uint16_t> color = std::nullopt);
    void setDynamicShadowDirectionBit(uint8_t bit, bool enabled);
    void editDynamicShadowColor();
    void updateColorRotationControls();
    void setCurrentColorRotationLength(std::size_t slotIndex, int length);
    void setCurrentColorRotationDelayMs(std::size_t slotIndex, int delayMs);
    void editCurrentColorRotationColor(std::size_t slotIndex, std::size_t colorIndex);
    void clearCurrentColorRotationSlot(std::size_t slotIndex);
    void updatePaletteControls();
    void setCurrentPaletteName(const QString& paletteName);
    void pickCurrentPaletteColor(std::size_t colorIndex);
    void editCurrentPaletteColor(std::size_t colorIndex);
    void applyCurrentPaletteGradient();
    void setFavoriteDrawColor(std::size_t favoriteIndex, uint16_t rgb565);
    void editFavoriteColor(std::size_t favoriteIndex);
    // Edit colors and color sets, see EditColorBar and ColorSetPanel. A selected slot makes
    // its edit color the draw color, also after the edit color changes; any other choice of
    // draw color (favorite, palette, pipette) leaves no slot selected.
    void selectEditColorSlot(std::size_t slot);
    void editEditColor(std::size_t slot);
    void setEditColor(std::size_t slot, uint16_t rgb565);
    void setDrawColorMode(edit::DrawColorMode mode);
    // Records the change of the edit colors and color sets as one undoable step.
    void commitEditorColors(document::ColorSetTables after, const QString& statusText);
    void storeColorSet(std::size_t setIndex);
    void loadColorSet(std::size_t setIndex);
    void deactivateColorSet(std::size_t setIndex);
    void renameColorSet(std::size_t setIndex, const QString& name);
    void copyEditColorsToDynamicSet();
    void copyDynamicSetToEditColors();
    // The pipette with Shift (Win32 behaviour): selects the edit color slot of the color.
    void pickEditColorSlot(uint16_t rgb565);
    void updateEditColorControls(bool projectChanged);
    void paintSpriteColorizedPixel(std::size_t spriteIndex,
                                   render::Resolution resolution,
                                   int x,
                                   int y,
                                   bool erase);
    void paintSpriteColorizedPixels(std::size_t spriteIndex,
                                    render::Resolution resolution,
                                    const std::vector<render::Point>& pixels,
                                    bool erase);
    void pickSpriteColorizedPixel(std::size_t spriteIndex,
                                  render::Resolution resolution,
                                  int x,
                                  int y);
    void floodFillSpriteColorized(std::size_t spriteIndex,
                                  render::Resolution resolution,
                                  int x,
                                  int y);
    void setSpriteDependentViewsDirty();
    void applyPendingSpriteDependentViews();
    [[nodiscard]] uint8_t activeDynamicSet() const;
    [[nodiscard]] PaintTarget activePaintTarget() const;
    [[nodiscard]] CanvasEditContext canvasEditContext() const;
    [[nodiscard]] CanvasEditOutputs canvasEditOutputs();
    // Copy and paste and the selection helpers of the Edit menu, see EditMenuActions,
    // PasteModeController and EditHelpersController.
    [[nodiscard]] PasteModeOutputs pasteModeOutputs();
    [[nodiscard]] EditHelpersOutputs editHelpersOutputs();
    void createSelectionActions(QMenu* editMenu);
    void updateEditMenuActions();
    // The helpers work on the frame canvas only; says so in the status line otherwise.
    [[nodiscard]] bool canUseFrameCanvasHelpers();
    void startPaste(edit::PasteMode mode);
    void showAutoSelectDialog();
    void showAutoCopyDialog();
    void runScrollingAutoFill();
    // A press on the colorized frame before it becomes a click or stroke: stamps in paste mode
    // and picks the pixel of Select By Color or Recolor. True when it took the press.
    bool interceptCanvasPress(render::CanvasHit hit, Qt::KeyboardModifiers modifiers);
    void colorizedCanvasClicked(render::CanvasHit hit, bool erase);
    [[nodiscard]] bool selectsWithPress() const;
    [[nodiscard]] bool selectsNonBlackOnly() const;
    enum class PendingCanvasClick {
        None,
        SelectByColor,
        Recolor,
    };
    void armCanvasClick(PendingCanvasClick click);
    // Returns false, without committing, when there is nothing to change.
    bool commitPixelChanges(const edit::EditLayer& layer, std::vector<edit::PixelChange> changes);
    void commitEdit(std::unique_ptr<edit::EditCommand> command);
    // For a command that has been applied already.
    void recordEdit(std::unique_ptr<edit::EditCommand> command);
    // Applied commands of several frames, each into the history of its frame, with one refresh.
    void recordFrameEdits(std::vector<std::unique_ptr<edit::EditCommand>> commands);
    // Records a command the caller has already shown the effects of.
    void pushEdit(std::unique_ptr<edit::EditCommand> command);
    void refreshAfterEdit(const edit::EditCommand& command);
    // Adding and deleting records, see edit::StructureCommand. The delete functions ask
    // before they delete.
    void deleteSelectedFrames();
    // The project's time from its stored total on (a project was opened or created).
    void startProjectClock();
    // The zoom display and buttons follow the canvas that is shown.
    [[nodiscard]] bool spriteCanvasShown() const;
    void updateZoomButton();
    // Space over the canvas: panning with the mouse while it is held.
    bool handleSpacePanning(QEvent* event);
    void endSpacePanning();
    // A slice of packing, when the editor has been idle for a while.
    void packIdleBlocks();
    // Hands the project's <rom>.pup.csv, if it has one, to the Runtime view and frame tester.
    void updateRuntimeSceneFile();
    // The stored total and the time since the project was opened, in ms.
    [[nodiscard]] uint32_t projectTimeMs() const;
    void updateTimeLabel();
    // Edit -> New Frame From Original Colors (Win32 "To Source Frame").
    void addSourceFrame();
    // Asks for the duration and deletes the selected frames that are shorter.
    void deleteSelectedFramesShorterThan();
    // note: what chose the frames, for the question; empty for the selection itself.
    void deleteFrames(const std::vector<std::size_t>& frameIndices, const QString& note);
    // Moving the selected frames, see edit::moveFrames(): before the frame of a frame list
    // row (count() for behind the last frame), one row up or down, or before a frame number
    // the user gives.
    void moveSelectedFramesBeforeRow(int row);
    void moveSelectedFramesUp();
    void moveSelectedFramesDown();
    void moveSelectedFramesTo();
    void moveFrames(const std::vector<std::size_t>& frameIndices,
                    std::size_t beforeFrame,
                    bool joinSection = true);
    // After a frame move ran in either direction: the moved frames are selected where they
    // are now, and every view is refreshed.
    void refreshAfterFrameMove(const edit::FrameMoveCommand& command);
    void addSprite();
    // Asks for a name and adds the sprite; its index, or nothing when none was added.
    std::optional<std::size_t> addNamedSprite();
    void deleteSelectedSprite();
    void addBackgroundFromCurrentFrame();
    void deleteSelectedBackground();
    void addComparisonMask();
    void deleteSelectedComparisonMask();
    [[nodiscard]] bool confirmDeletion(const QString& title,
                                       const QString& question,
                                       const QString& consequences);
    // For a command that has been applied already.
    void recordStructureEdit(std::unique_ptr<edit::StructureCommand> command);
    // After a structure command ran in either direction: stops playback, drops the canvas
    // selections, moves the frame, mask, background or sprite selection to the added or
    // restored record or to the neighbour of a deleted one, and refreshes every view.
    // The status of a structure edit, with a note when old structure undo steps were dropped
    // to stay within the undo memory limit (edit::UndoHistories).
    void showStructureEditStatus(const QString& message, int timeoutMs);
    void refreshAfterStructureEdit(const edit::StructureCommand& command);
    // Texts and thumbnails of the frame list rows after a change that leaves the frames
    // where they are; the list is rebuilt only if other frames match the filter now.
    void refreshFrameListItems();
    // Makes the frame, or the nearest listed frame behind it, the current one.
    void selectFrameNear(std::size_t frameIndex);
    // Enabled state of the Delete Frames actions and of New and Delete on the browser pages.
    void updateStructureControls();
    [[nodiscard]] std::vector<edit::HistoryKey> currentHistoryKeys() const;
    [[nodiscard]] std::optional<std::size_t> currentComparisonMaskIndex() const;
    [[nodiscard]] bool isComparisonMaskModeActive() const;
    // The plane dynamic masks, colors and shadows are edited on, whatever the editing mode:
    // see ProjectDocument::dynamicPlane().
    [[nodiscard]] render::Resolution dynamicPlane() const;
    // False in HD mode of an ordinary project: the dynamic zones are those of the SD plane
    // and are not shown or painted over the HD frame.
    [[nodiscard]] bool isDynamicPlaneActive() const;
    [[nodiscard]] bool isDynamicMaskModeActive() const;
    [[nodiscard]] bool isBackgroundMaskModeActive() const;
    [[nodiscard]] std::optional<std::size_t> selectedPaletteIndex() const;
    // The color picker, with the project's palettes to pick a color from.
    [[nodiscard]] std::optional<uint16_t> pickColor(uint16_t initialRgb565, const QString& title);
    [[nodiscard]] std::optional<std::size_t> selectedBackgroundIndex() const;
    [[nodiscard]] std::optional<std::size_t> currentBackgroundIndex() const;
    [[nodiscard]] bool isBackgroundImageTargetActive() const;
    [[nodiscard]] std::optional<std::size_t> currentSpriteIndex() const;
    [[nodiscard]] bool isSpriteCanvasActive() const;
    // anywhere: the most recent step of the whole project (Edit -> Undo Last Edit Anywhere),
    // after going to its frame; otherwise the most recent of the current context's histories.
    void undo(bool anywhere = false);
    void redo(bool anywhere = false);
    // Shows the frame (and plane) an undo or redo of the history changes.
    void goToHistory(const edit::HistoryKey& key);
    void updateUndoRedoActions();
    void updateProjectHeaderStatus();
    // The frame's thumbnail is out of date, see LazyListIcons::invalidate().
    void updateFrameThumbnail(std::size_t frameIndex);
    void updateFrameListItem(std::size_t frameIndex);
    // The same for several frames; the list is rebuilt once if it has to be.
    void updateFrameListItems(const std::vector<std::size_t>& frameIndices);
    void updateFrameListItemText(std::size_t frameIndex);
    void refreshComponentBrowsers();
    // Hides the masks, backgrounds and sprites the current frame does not use while the left
    // sidebar's "Used by the current frame" is on (rows stay: a list's row is its record).
    void applyAssetFilter();
    // The mask's picture in the left sidebar: the current frame's original under it.
    [[nodiscard]] QIcon maskIcon(std::size_t maskIndex) const;
    // Updates what follows the current frame in the left sidebar (see m_frameRestTimer).
    void updateFrameRestViews();
    // The mask browser icon of the mask follows its pixels; several edits before the event
    // loop runs again redraw it once.
    void scheduleMaskIconUpdate(std::size_t maskIndex);
    void updatePendingMaskIcons();
    void updateMaskBrowserSelection();
    void updateBackgroundBrowserSelection();
    void updateSpriteBrowserSelection();
    void updatePaletteBrowserSelection();
    void updatePlaybackControls();
    void setPlaybackRunning(bool running);
    void advancePlaybackFrame();
    // The Runtime view shows libserum's output for the current frame on the canvas, see
    // RuntimeView. While it is on nothing can be edited on the canvas.
    void setRuntimeViewEnabled(bool enabled);
    void setRuntimeViewRows(runtime::PlaneRequest rows);
    // Actions, tool buttons and the status line follow the view's state.
    void updateRuntimeViewControls();
    // The frames playback goes through from the current one on, in list order.
    [[nodiscard]] std::vector<std::size_t> playbackFrameOrder() const;
    // Shows the current frame through libserum now and starts its color rotations.
    void startRuntimeRotations();
    void stepRuntimeRotations();
    [[nodiscard]] int playbackIntervalForCurrentFrame() const;
    [[nodiscard]] DmdRenderOptions currentDmdRenderOptions() const;
    // How DMD pixels are drawn (Settings -> Appearance), kept for the next start.
    DmdRenderOptions m_dmdRenderOptions = loadDmdRenderOptions();
    void updateDmdRenderOptions();
    void updateSpriteControls();
    [[nodiscard]] bool isSpriteCaptureSelectionModeActive() const;

    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_undoAnywhereAction = nullptr;
    QAction* m_exportGifBothPlanesAction = nullptr;
    QAction* m_exportGifAllAction = nullptr;
    QAction* m_redoAnywhereAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_addFramesFromDumpAction = nullptr;
    QAction* m_deleteFramesAction = nullptr;
    QAction* m_sourceFrameAction = nullptr;
    // The same in the frame list's menu, with the Delete and Backspace keys.
    QAction* m_deleteFramesListAction = nullptr;
    QAction* m_deleteShortFramesAction = nullptr;
    QAction* m_moveFramesUpAction = nullptr;
    QAction* m_moveFramesDownAction = nullptr;
    QAction* m_moveFramesToAction = nullptr;
    // The canvas zoom in the toolbar; a click shows the whole frame.
    QToolButton* m_zoomButton = nullptr;
    // Help -> Handbook, created when first asked for.
    HandbookWindow* m_handbookWindow = nullptr;
    PluginLogWindow* m_pluginLogWindow = nullptr;
    PupTesterWindow* m_pupTesterWindow = nullptr;
    QAction* m_runtimeViewAction = nullptr;
    QAction* m_helperLinesShownAction = nullptr;
    QAction* m_runtimeRows32Action = nullptr;
    QAction* m_runtimeRows64Action = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_frameStatusLabel = nullptr;
    // The assets on the left: Masks, Backgrounds, Sprites, in sections that open and close.
    SectionStack* m_leftBrowserTabs = nullptr;
    // The right sidebar's sections.
    SectionStack* m_inspectorSections = nullptr;
    // What the drawing tools edit: the frame, or the selected background (Edit BG).
    PaintTarget m_paintTarget = PaintTarget::Frame;
    CanvasHistory m_canvasHistory;
    // While the history shows a place: nothing is noted.
    bool m_showingCanvasPlace = false;
    QToolButton* m_historyBackButton = nullptr;
    QToolButton* m_historyForwardButton = nullptr;
    QToolButton* m_leaveSceneButton = nullptr;
    FrameListWidget* m_frameList = nullptr;
    LazyListIcons* m_frameThumbnails = nullptr;
    QListWidget* m_maskList = nullptr;
    QListWidget* m_backgroundList = nullptr;
    QListWidget* m_spriteList = nullptr;
    QComboBox* m_frameFilterComboBox = nullptr;
    RecordListPage* m_maskPage = nullptr;
    RecordListPage* m_backgroundPage = nullptr;
    RecordListPage* m_spritePage = nullptr;
    QToolButton* m_sdButton = nullptr;
    QToolButton* m_hdButton = nullptr;
    QToolButton* m_comparisonMaskOverlayButton = nullptr;
    QToolButton* m_dynamicMaskOverlayButton = nullptr;
    QToolButton* m_backgroundMaskOverlayButton = nullptr;
    QToolButton* m_playbackButton = nullptr;
    QToolButton* m_runtimeViewButton = nullptr;
    QToolButton* m_loopPlaybackButton = nullptr;
    QCheckBox* m_useProjectTimingCheckBox = nullptr;
    QCheckBox* m_currentFrameAssetsCheckBox = nullptr;
    QSpinBox* m_playbackIntervalSpinBox = nullptr;
    QComboBox* m_backgroundComboBox = nullptr;
    std::vector<QToolButton*> m_toolButtons;
    std::vector<QToolButton*> m_colorButtons;
    std::vector<uint16_t> m_favoriteColors;
    EditColorBar* m_editColorBar = nullptr;
    ColorSetPanel* m_colorSetPanel = nullptr;
    std::optional<std::size_t> m_selectedEditColor;
    edit::DrawColorMode m_drawColorMode = edit::DrawColorMode::SingleColor;
    BackgroundPanel* m_backgroundPanel = nullptr;
    ColorRotationPanel* m_colorRotationPanel = nullptr;
    DynamicPanel* m_dynamicPanel = nullptr;
    FramePanel* m_framePanel = nullptr;
    MaskPanel* m_maskPanel = nullptr;
    SameFramesPanel* m_sameFramesPanel = nullptr;
    PalettePanel* m_palettePanel = nullptr;
    SpritePanel* m_spritePanel = nullptr;
    QToolButton* m_activeColorButton = nullptr;
    QStackedWidget* m_canvasStack = nullptr;
    FramePreviewWidget* m_preview = nullptr;
    SpriteEditorWidget* m_spriteEditor = nullptr;
    QTimer* m_playbackTimer = nullptr;
    QTimer* m_colorRotationPreviewTimer = nullptr;
    QTimer* m_runtimeRotationTimer = nullptr;
    QElapsedTimer m_playbackElapsedTimer;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    DrawingTool m_drawingTool = DrawingTool::Point;
    uint16_t m_activeDrawColor = 0xffff;
    // Sprite shown on the Sprite page and in the sprite canvas; -1 once a project has none.
    int m_selectedSpriteIndex = 0;
    // Mask picked in the mask browser. It starts as the first frame's mask and is assigned to
    // frames only on request.
    std::optional<std::size_t> m_selectedMaskIndex;
    bool m_spriteDependentViewsDirty = false;
    CanvasEditController m_canvasEdit;
    PasteModeController m_pasteMode;
    EditHelpersController m_editHelpers;
    EditMenuActions* m_editMenuActions = nullptr;
    PendingCanvasClick m_pendingCanvasClick = PendingCanvasClick::None;
    // W held on the canvas: selection strokes leave black pixels out (Win32).
    bool m_nonBlackKeyDown = false;
    edit::StripePattern m_autoSelectPattern;
    RuntimeView m_runtimeView;
    edit::UndoHistories m_undoHistories;
    edit::SameFrameFinder m_sameFrameFinder;
    // The counts of "Update Same List" and what they were made for.
    struct SameFrameCountsFor {
        std::size_t frameIndex = 0;
        uint64_t identificationRevision = 0;
        std::vector<edit::SameFrameCounts> counts;
    };
    std::optional<SameFrameCountsFor> m_sameFrameCounts;
    // Where the last dump came from: the place a new project is offered to be saved in.
    QString m_dumpDirectory;
    // Instead of the message box, when set (tests, tools/ui_dump).
    std::function<UnsavedChangesAnswer(const QString& action)> m_unsavedChangesAnswer;
    // Frame index -> row of m_frameList, -1 for a frame the filter hides; rebuilt with the list.
    std::vector<int> m_frameRows;
    std::vector<std::size_t> m_pendingMaskIcons;
    // The project's .cRZ, which a save writes (also for a project opened from a .cROM).
    QString m_currentCromPath;
    QString m_currentCrpPath;
    std::optional<document::ProjectDocument> m_document;
    // Structure undo steps the last structure edit dropped, see showStructureEditStatus().
    std::size_t m_droppedStructureSteps = 0;
    // The document revision the last finished autosave wrote.
    uint64_t m_autosavedRevision = 0;
    std::unique_ptr<Autosaver> m_autosaver;
    SectionBar* m_sectionBar = nullptr;
    DmdOutput m_dmdOutput;
    QAction* m_dmdOutputAction = nullptr;
    QTimer m_dmdTimer;
    // Fires once the current frame has stayed the same for a moment: what shows the current
    // frame in the left sidebar (the mask pictures, "Used by the current frame") follows it
    // then, not at every step of a quick scroll through the frames.
    QTimer m_frameRestTimer;
    // The frame the mask pictures show; they are drawn again when another frame comes to rest.
    std::optional<std::size_t> m_maskIconFrame;
    // What the display shows while a dialog has its own picture (the Frame Tester).
    std::function<std::optional<document::Rgb565Frame>()> m_dmdPictureOverride;
    struct DmdPictureKey {
        uint64_t revision = 0;
        std::size_t frame = 0;
        render::Resolution plane = render::Resolution::Sd;
        bool runtime = false;
        bool background = false;
        bool sprites = true;
        bool operator==(const DmdPictureKey& other) const
        {
            return revision == other.revision && frame == other.frame && plane == other.plane &&
                   runtime == other.runtime && background == other.background &&
                   sprites == other.sprites;
        }
    };
    std::optional<DmdPictureKey> m_dmdPictureKey;
    // Which plane goes to the display (DMD Output Settings), while the output runs.
    DmdOutputSettings::Picture m_dmdPicture = DmdOutputSettings::Picture::Canvas;
    // The rows of the Runtime view's picture the display gets; none for the shown rows.
    [[nodiscard]] std::optional<runtime::PlaneRequest> dmdRuntimeRows() const;
    // The Frame Tester's picture for the display: its 64-row plane or its 32-row one.
    [[nodiscard]] bool dmdPrefersRows64() const;
    // The last percentage given to Edit -> Brightness.
    int m_brightnessPercent = 110;
    // The filter last chosen for Create HD.
    document::UpscaleFilter m_createHdFilter = document::UpscaleFilter::Nearest;
    // The document revision the section bar's lists show.
    std::optional<uint64_t> m_sectionBarRevision;

    // The editor's plugins (Settings -> Plugins) and their menu. Last: they answer plugins
    // from the members above, and go first when the window does.
    QMenu* m_pluginsMenu = nullptr;
    // The status bar's pixel under the mouse on the canvas (and the dragged area).
    QLabel* m_positionLabel = nullptr;
    // The time spent on the project (the cRP's TimeElapsed) and in this session, shown in the
    // status bar. Both count working time: they stop after a minute without input
    // (ActivityClock); m_sessionClock is only its time base.
    QLabel* m_timeLabel = nullptr;
    QTimer m_clockTimer;
    QElapsedTimer m_sessionClock;
    ActivityClock m_activityClock;
    // Packing the project's blocks while the editor is idle (ProjectDocument::packBlocks):
    // the time of the last input and when the next pass may start, on m_sessionClock.
    bool m_spacePanning = false;
    QTimer m_packTimer;
    qint64 m_lastInputMs = 0;
    qint64 m_nextPackPassMs = 0;
    uint32_t m_projectTimeAtOpenMs = 0;
    // Input to any of the editor's windows: the clocks go on.
    void noteActivity();
    // The last pixel the mouse pointed at on the canvas, where a new helper line goes.
    std::optional<render::CanvasHit> m_lastCanvasHit;
    // The Helper Lines section (HelperLine.h): changed through edit::setHelperLines.
    HelperLinesPanel* m_helperLinesPanel = nullptr;
    void refreshHelperLines();
    // View -> Show Helper Lines onto the canvas and the Helper Lines section.
    void applyHelperLinesShown();
    void addHelperLine(bool vertical);
    void editHelperLine(std::size_t line);
    void changeHelperLines(std::vector<document::HelperLine> lines, const QString& status);
    // The Scenes section (Scene.h): changed through the edit::SceneEdits functions.
    ScenesPanel* m_scenesPanel = nullptr;
    SceneSettingsPanel* m_sceneSettingsPanel = nullptr;
    // The Project Settings section (the scaling settings of ColorizationSettings).
    QAction* m_projectSettingsAction = nullptr;
    void refreshScenes();
    void addScene();
    // A double click on a scene: its frames in the strip, its first frame on the canvas; the
    // timeline before it stays in the back/forward history.
    void openScene(uint16_t sceneId);
    void deleteScene(uint16_t sceneId);
    void showSceneFrames(uint16_t sceneId);
    void recordSceneEdit(std::unique_ptr<edit::SceneEditCommand> command);
    void refreshAfterSceneEdit(const edit::SceneEditCommand& command);
    // File -> Project Settings: the dialog, and its settings as one undo step.
    void openProjectSettings();
    // A plugin's edit batch (editor API BeginEdit .. EndEdit): already applied.
    void recordPluginEdit(std::unique_ptr<edit::EditCommand> command);
    // After an edit batch ran either way: it may have changed any frame and record.
    void refreshAfterEditBatch();
    // A project of the Win32 editor takes the scenes and scaling settings of the files beside
    // its cROM (runtime::importColorizationSidecars()). Returns what to tell about it, empty
    // when there was nothing to take.
    QString importColorizationSidecars();
    void showCanvasPosition(std::optional<render::CanvasHit> hit,
                            std::optional<render::CanvasHit> dragStart);
    std::unique_ptr<EditorPlugins> m_plugins;
    // Makes the frame current and selects the frames, for a plugin.
    void showFramesForPlugin(std::size_t current, const std::vector<std::size_t>& selected);
};

}  // namespace colorizing::app
