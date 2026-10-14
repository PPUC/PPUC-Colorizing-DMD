#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QColor>
#include <QString>

#include "document/ProjectDocument.h"
#include "edit/ColorSetEdits.h"
#include "edit/DrawingGeometry.h"
#include "edit/DrawingTools.h"
#include "edit/EditCommand.h"
#include "edit/FramePixelSelection.h"
#include "edit/PlaneSelection.h"
#include "edit/RecolorHelpers.h"
#include "PaintTarget.h"
#include "render/CanvasLayout.h"
#include "render/CanvasTypes.h"

namespace colorizing::app {

// The data the frame canvas edits. An active mask mode wins over the paint target.
enum class CanvasEditLayer {
    FramePixels,
    BackgroundImage,
    ComparisonMask,
    DynamicMask,
    BackgroundMask,
};

// The state of the window a canvas event is handled in. The controller keeps none of it.
struct CanvasEditContext {
    const document::ProjectDocument* document = nullptr;
    std::optional<std::size_t> currentFrame;
    // Sorted and unique; the current frame if no other frame is selected.
    std::vector<std::size_t> selectedFrames;
    CanvasEditLayer layer = CanvasEditLayer::FramePixels;
    // Clicks and strokes on the original frame mark the sprite capture selection instead of
    // editing the layer.
    bool spriteCaptureSelection = false;
    // The chosen target, which stays Background while no background is selected; layer is
    // FramePixels then.
    PaintTarget paintTarget = PaintTarget::Frame;
    std::optional<std::size_t> selectedBackground;
    std::optional<std::size_t> selectedPalette;
    edit::DrawingTool tool = edit::DrawingTool::Point;
    uint16_t drawColor = 0xffff;
    // Points, shapes and flood fills of frame and background pixels; the edit colors come
    // from the document. Gradients ignore it.
    edit::DrawColorMode drawColorMode = edit::DrawColorMode::SingleColor;
    uint8_t dynamicSet = 0;
    edit::PaletteGradientShape gradientShape = edit::PaletteGradientShape::Rectangle;
    edit::PaletteGradientDirection gradientDirection = edit::PaletteGradientDirection::LeftToRight;
    edit::PaletteGradientPattern gradientPattern = edit::PaletteGradientPattern::Linear;
    // The plane being edited, for the selection commands that have no canvas hit.
    render::Resolution activePlane = render::Resolution::Sd;
    // Whether the canvas shows the background layer, for the colors "select by color" sees.
    bool includeBackgroundLayer = false;
    // Zero-based palette color indices. Asked only when a gradient is filled, because reading
    // them makes the inspector interpret a half-typed number.
    std::function<std::pair<int, int>()> gradientEndpoints;
};

struct CanvasPixelOverlay {
    render::CanvasRegion region = render::CanvasRegion::ColorizedFrame;
    render::Resolution resolution = render::Resolution::Sd;
    std::vector<render::Point> pixels;
    QColor color;
};

// Everything the controller asks of its owner. Unset callbacks are skipped.
struct CanvasEditOutputs {
    // Apply the command to the document and record it. Called once per edited frame, mask or
    // background, before the next command is built.
    std::function<void(std::unique_ptr<edit::EditCommand> command)> commit;
    // After the last commit of one user action.
    std::function<void()> commitsFinished;
    std::function<void(const QString& text, int timeoutMs)> showStatus;
    std::function<void(uint16_t rgb565)> pickColor;
    // The pipette with Shift: select the edit color slot of this color instead.
    std::function<void(uint16_t rgb565)> pickEditColorSlot;
    std::function<void(uint8_t dynamicSet)> pickDynamicSet;
    std::function<void(CanvasPixelOverlay overlay)> showPreview;
    std::function<void()> clearPreview;
    std::function<void(CanvasPixelOverlay overlay)> showSelectionOverlay;
    std::function<void()> clearSelectionOverlay;
    // The canvas selection was set or cleared.
    std::function<void()> selectionChanged;
    std::function<void(bool active)> selectionDirectionModeChanged;
    std::function<void()> captureSelectionChanged;
};

// Turns clicks and strokes on the frame canvas into undoable commands for the layer being
// edited, and owns the editing state that lives between two canvas events: the canvas
// selection, a pending free palette gradient and the sprite capture selection. It never edits
// the document or a widget itself; see CanvasEditOutputs.
class CanvasEditController {
public:
    explicit CanvasEditController(CanvasEditOutputs outputs = {});

    void colorizedPixelClicked(const CanvasEditContext& context,
                               const render::CanvasHit& hit,
                               bool erase);
    void originalPixelClicked(const CanvasEditContext& context,
                              const render::CanvasHit& hit,
                              bool erase);
    void strokePreviewed(const CanvasEditContext& context,
                         const render::CanvasHit& start,
                         const render::CanvasHit& end,
                         bool erase);
    void strokeFinished(const CanvasEditContext& context,
                        const render::CanvasHit& start,
                        const render::CanvasHit& end,
                        bool erase);

    // The first of the two strokes of a free palette gradient has been drawn.
    [[nodiscard]] bool isFreeGradientPending() const;
    void cancelFreeGradient();

    [[nodiscard]] bool hasSelection() const;
    // Whether the next stroke draws the direction line of the selection. Can still be set
    // after the selection is gone, see isSelectionDirectionModeActive().
    [[nodiscard]] bool isSelectionDirectionMode() const;
    [[nodiscard]] bool isSelectionDirectionModeActive() const;
    void toggleSelectionDirectionMode();
    // Without telling the owner.
    void leaveSelectionDirectionMode();
    void clearSelection(const CanvasEditContext& context);
    void fillSelectionWithPaletteGradient(const CanvasEditContext& context);

    // The selection, or null when nothing is selected. It is on the plane of the frame or
    // background it was made on and is dropped when the frame, plane or target changes.
    [[nodiscard]] const edit::PlaneSelection* selection() const;
    // Whether a click or stroke edits the selection instead of the layer: frame pixels or a
    // background image are being edited.
    [[nodiscard]] static bool canEditSelection(const CanvasEditContext& context);
    // Ctrl+click and Ctrl+drag (the Win32 Copy_Mask editing): the tool's pixels join the
    // selection, or leave it with remove; the fill tool takes the connected pixels of the
    // clicked color. nonBlackOnly keeps black pixels out (Win32: W held). A selection of another
    // plane or target is replaced.
    void editSelectionAtPoint(const CanvasEditContext& context,
                              const render::CanvasHit& hit,
                              bool remove,
                              bool nonBlackOnly);
    void editSelectionWithStroke(const CanvasEditContext& context,
                                 const render::CanvasHit& start,
                                 const render::CanvasHit& end,
                                 bool remove,
                                 bool nonBlackOnly);
    void previewSelectionStroke(const CanvasEditContext& context,
                                const render::CanvasHit& start,
                                const render::CanvasHit& end,
                                bool remove);
    void selectAll(const CanvasEditContext& context);
    void invertSelection(const CanvasEditContext& context);
    // Adds the pixels next to the selection in the direction (Win32 Ctrl+shadow buttons).
    void growSelection(const CanvasEditContext& context, edit::SelectionDirection direction);
    void mirrorSelection(const CanvasEditContext& context, bool horizontally, bool vertically);
    // Every pixel of the clicked pixel's color as the canvas shows it (with dynamic content,
    // sprites and, when shown, the background layer); remove takes them out instead.
    void selectByColor(const CanvasEditContext& context, const render::CanvasHit& hit, bool remove);
    // The current frame's background mask on the plane (Win32 Ctrl+B).
    void selectBackgroundMask(const CanvasEditContext& context);
    void selectStripes(const CanvasEditContext& context, const edit::StripePattern& pattern);

    [[nodiscard]] std::optional<std::size_t> captureSelectionFrame() const;
    // Pixels of the original frame, which is the grid sprites are detected on.
    [[nodiscard]] std::vector<render::Point> captureSelectionPixels(
        std::optional<std::size_t> frameIndex) const;
    // Shows the capture selection of the frame as the selection overlay, or no overlay.
    void showCaptureSelection(std::optional<std::size_t> frameIndex);
    void clearCaptureSelection();
    // Without telling the owner.
    void resetCaptureSelection();

private:
    struct PendingFreeGradient {
        render::CanvasHit shapeStart;
        render::CanvasHit shapeEnd;
        PaintTarget target = PaintTarget::Frame;
        std::optional<std::size_t> backgroundIndex;
    };

    struct Selection {
        // The box the gradient fill spreads over: the drawn shape, or the selection's bounds
        // once it was edited in another way.
        render::CanvasHit shapeStart;
        render::CanvasHit shapeEnd;
        PaintTarget target = PaintTarget::Frame;
        std::optional<std::size_t> backgroundIndex;
        edit::PlaneSelection pixels;
        std::optional<render::CanvasHit> gradientStart;
        std::optional<render::CanvasHit> gradientEnd;
    };

    // The selection to edit for the context on the plane: the current one if it belongs to
    // the same target and plane, otherwise a new empty one.
    [[nodiscard]] Selection selectionFor(const CanvasEditContext& context,
                                         render::Resolution plane) const;
    // Makes the selection current (or clears it when empty), shows it and tells the owner.
    void setSelection(const CanvasEditContext& context, Selection selection, const QString& status);
    // The colorized picture the selection is made on: the current frame's or the selected
    // background's, on the plane; null when there is none.
    [[nodiscard]] const document::Rgb565Frame* selectionPicture(const CanvasEditContext& context,
                                                                render::Resolution plane) const;

    void pickDynamicSet(const CanvasEditContext& context, const render::CanvasHit& hit);
    void pickFrameColor(const CanvasEditContext& context,
                        const render::CanvasHit& hit,
                        bool selectEditColorSlot);
    void floodFillFrame(const CanvasEditContext& context, const render::CanvasHit& hit);
    void pickBackgroundImageColor(const CanvasEditContext& context,
                                  const render::CanvasHit& hit,
                                  bool selectEditColorSlot);
    void pickedColor(uint16_t color, bool selectEditColorSlot, const QString& statusText);
    void floodFillBackgroundImage(const CanvasEditContext& context,
                                  const render::CanvasHit& hit);
    void paintComparisonMaskPixel(const CanvasEditContext& context,
                                  const render::CanvasHit& hit,
                                  bool erase);
    void floodFillComparisonMask(const CanvasEditContext& context,
                                 const render::CanvasHit& hit,
                                 bool erase);
    void floodFillDynamicMask(const CanvasEditContext& context,
                              const render::CanvasHit& hit,
                              bool erase);
    void floodFillBackgroundMask(const CanvasEditContext& context,
                                 const render::CanvasHit& hit,
                                 bool erase);
    // pixels are coordinates of the original frame.
    void paintCaptureSelection(const CanvasEditContext& context,
                               const std::vector<render::Point>& pixels,
                               bool erase);

    void beginFreeGradient(const CanvasEditContext& context,
                           const render::CanvasHit& start,
                           const render::CanvasHit& end);
    void previewFreeGradientDirection(const CanvasEditContext& context,
                                      const render::CanvasHit& start,
                                      const render::CanvasHit& end);
    void showPendingFreeGradient(const CanvasEditContext& context);
    void setSelectionFromStroke(const CanvasEditContext& context,
                                const render::CanvasHit& start,
                                const render::CanvasHit& end);
    void showSelection();
    void setSelectionDirection(const render::CanvasHit& start, const render::CanvasHit& end);
    void previewSelectionDirection(const render::CanvasHit& start, const render::CanvasHit& end);

    [[nodiscard]] static std::vector<render::Point> strokePixels(const CanvasEditContext& context,
                                                                 const render::CanvasHit& start,
                                                                 const render::CanvasHit& end);
    [[nodiscard]] static edit::PixelColorSource gradientColorSource(
        const CanvasEditContext& context,
        std::size_t paletteIndex,
        const render::CanvasHit& shapeStart,
        const render::CanvasHit& shapeEnd,
        const render::CanvasHit& gradientStart,
        const render::CanvasHit& gradientEnd);

    void commitFramePixels(const CanvasEditContext& context,
                           render::Resolution resolution,
                           const std::vector<render::Point>& pixels,
                           const QString& statusText);
    void commitFrameGradient(const CanvasEditContext& context,
                             render::Resolution resolution,
                             const render::CanvasHit& start,
                             const render::CanvasHit& end,
                             const render::CanvasHit& gradientStart,
                             const render::CanvasHit& gradientEnd,
                             const QString& statusText);
    void commitFramePixelColors(const CanvasEditContext& context,
                                render::Resolution resolution,
                                const std::vector<render::Point>& pixels,
                                const edit::PixelColorSource& colorForPoint,
                                const QString& statusText);
    // With a color source per frame, for color set replace: each frame's own original shades.
    void commitFramePixelColors(
        const CanvasEditContext& context,
        render::Resolution resolution,
        const std::vector<render::Point>& pixels,
        const std::function<edit::PixelColorSource(std::size_t frameIndex)>& sourceForFrame,
        const QString& statusText);
    void commitComparisonMaskPixels(const CanvasEditContext& context,
                                    const std::vector<render::Point>& pixels,
                                    uint8_t value,
                                    const QString& statusText);
    // Dynamic zones exist on ProjectDocument::dynamicPlane() alone. Says so in the status
    // line and returns false for the other plane.
    [[nodiscard]] bool isDynamicPlane(const CanvasEditContext& context,
                                      render::Resolution resolution);
    // kind is DynamicMask or BackgroundMask.
    void commitFrameMaskPixels(const CanvasEditContext& context,
                               edit::EditKind kind,
                               render::Resolution resolution,
                               const std::vector<render::Point>& pixels,
                               uint8_t value,
                               const QString& statusText);
    void commitBackgroundImagePixels(const CanvasEditContext& context,
                                     render::Resolution resolution,
                                     const std::vector<render::Point>& pixels,
                                     const QString& statusText);
    void commitBackgroundImageGradient(const CanvasEditContext& context,
                                       render::Resolution resolution,
                                       const render::CanvasHit& start,
                                       const render::CanvasHit& end,
                                       const render::CanvasHit& gradientStart,
                                       const render::CanvasHit& gradientEnd,
                                       const QString& statusText);
    void commitBackgroundImageColors(const CanvasEditContext& context,
                                     std::size_t backgroundIndex,
                                     render::Resolution resolution,
                                     const std::vector<render::Point>& pixels,
                                     const edit::PixelColorSource& colorForPoint,
                                     const QString& statusText);
    // Both return false, without committing, when there is nothing to change.
    bool commitPixelChanges(const edit::EditLayer& layer, std::vector<edit::PixelChange> changes);
    bool commitMaskChanges(const edit::EditLayer& layer,
                           std::vector<edit::MaskPixelChange> changes);

    void showStatus(const QString& text, int timeoutMs) const;
    void showError(const std::exception& error) const;
    void showPreview(render::CanvasRegion region,
                     render::Resolution resolution,
                     std::vector<render::Point> pixels,
                     QColor color) const;
    void clearPreview() const;
    void showSelectionOverlay(render::CanvasRegion region,
                              render::Resolution resolution,
                              std::vector<render::Point> pixels,
                              QColor color) const;
    void clearSelectionOverlay() const;
    void commitsFinished() const;
    void selectionChanged() const;
    void selectionDirectionModeChanged() const;

    CanvasEditOutputs m_outputs;
    std::optional<PendingFreeGradient> m_pendingFreeGradient;
    std::optional<Selection> m_selection;
    bool m_selectionDirectionMode = false;
    // Sized like the original frames of the document it was painted on last.
    edit::FramePixelSelection m_captureSelection{render::ProjectGeometry().romFrameSize()};
    render::ProjectGeometry m_captureGeometry;
};

}  // namespace colorizing::app
