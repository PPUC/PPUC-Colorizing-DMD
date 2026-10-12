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
#include "edit/DrawingGeometry.h"
#include "edit/DrawingTools.h"
#include "edit/EditCommand.h"
#include "edit/FramePixelSelection.h"
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
    uint8_t dynamicSet = 0;
    edit::PaletteGradientShape gradientShape = edit::PaletteGradientShape::Rectangle;
    edit::PaletteGradientDirection gradientDirection = edit::PaletteGradientDirection::LeftToRight;
    edit::PaletteGradientPattern gradientPattern = edit::PaletteGradientPattern::Linear;
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

    [[nodiscard]] std::optional<std::size_t> captureSelectionFrame() const;
    // In SD frame coordinates, the grid sprites live on. On a 256x64 original a click marks
    // the SD pixel that covers the clicked original pixel.
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
        render::CanvasHit shapeStart;
        render::CanvasHit shapeEnd;
        PaintTarget target = PaintTarget::Frame;
        std::optional<std::size_t> backgroundIndex;
        std::vector<render::Point> pixels;
        std::optional<render::CanvasHit> gradientStart;
        std::optional<render::CanvasHit> gradientEnd;
    };

    void pickDynamicSet(const CanvasEditContext& context, const render::CanvasHit& hit);
    void pickFrameColor(const CanvasEditContext& context, const render::CanvasHit& hit);
    void floodFillFrame(const CanvasEditContext& context, const render::CanvasHit& hit);
    void pickBackgroundImageColor(const CanvasEditContext& context,
                                  const render::CanvasHit& hit);
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
    void commitComparisonMaskPixels(const CanvasEditContext& context,
                                    const std::vector<render::Point>& pixels,
                                    uint8_t value,
                                    const QString& statusText);
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
    edit::FramePixelSelection m_captureSelection{render::kSdFrameSize};
    // Original size of the document the capture selection was painted on.
    render::Size m_captureOriginalSize = render::kSdFrameSize;
};

}  // namespace colorizing::app
