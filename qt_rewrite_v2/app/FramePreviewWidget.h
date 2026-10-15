#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <QPoint>
#include <QColor>
#include <QCoreApplication>
#include <QWidget>

class QScrollBar;

#include "document/ProjectDocument.h"
#include "FrameImageUtils.h"
#include "render/CanvasLayout.h"

namespace colorizing::app {

class RuntimeView;

// The floating picture of paste mode: colored pixels of one plane and the frame of the box.
struct CanvasPasteOverlay {
    render::Resolution resolution = render::Resolution::Sd;
    render::Rect box;
    std::vector<std::pair<render::Point, QColor>> pixels;
};

class FramePreviewWidget final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(FramePreviewWidget)

public:
    explicit FramePreviewWidget(QWidget* parent = nullptr);

    void setDocument(const document::ProjectDocument* document);
    // While the view is enabled the colorized region shows libserum's output for the frame
    // instead of the editable layers, overlays are not drawn and the mouse does nothing.
    // Not owned; has to outlive the widget or be reset.
    void setRuntimeView(RuntimeView* runtimeView);
    void setFrameIndex(std::size_t frameIndex);
    void setActiveResolution(render::Resolution resolution);
    void setComparisonMaskOverlayEnabled(bool enabled);
    // Marks pixels of the original frame, e.g. those that differ among the selected frames:
    // every pixel of the frame that is not 0 is marked. The frame has the size of the ROM
    // frame; nothing switches the overlay off.
    void setDifferenceOverlay(std::optional<document::IndexedFrame> differingPixels);
    // With the Dynamic overlay the dynamic zones are outlined on both pictures, the selected
    // set brighter than the others, and every area carries its set number when the zoom leaves
    // room for it; the mouse over a zone names its set.
    void setDynamicMaskOverlayEnabled(bool enabled);
    void setSelectedDynamicSet(std::optional<uint8_t> setIndex);
    void setBackgroundMaskOverlayEnabled(bool enabled);
    void setBackgroundLayerEnabled(bool enabled);
    void setBackgroundImageEditModeEnabled(bool enabled);
    void setSelectedBackgroundIndex(std::optional<std::size_t> backgroundIndex);
    void setDmdRenderOptions(DmdRenderOptions options);
    void setColorRotationPreviewElapsedMs(std::optional<int> elapsedMs);
    void setSelectedSpriteBoundsOverlayEnabled(bool enabled);
    void setSelectedSpriteReference(std::optional<document::FrameSpriteReference> reference);
    void setSelectionOverlayPixels(render::CanvasRegion region,
                                   render::Resolution resolution,
                                   std::vector<render::Point> pixels,
                                   QColor color);
    void clearSelectionOverlayPixels();
    void setPixelClickedHandler(std::function<void(render::CanvasHit, bool)> handler);
    void setOriginalClickedHandler(std::function<void(render::CanvasHit, bool)> handler);
    void setStrokePreviewHandler(std::function<void(render::CanvasHit, render::CanvasHit, bool)> handler);
    void setStrokeFinishedHandler(std::function<void(render::CanvasHit, render::CanvasHit, bool)> handler);
    void setPreviewPixels(render::CanvasRegion region,
                          render::Resolution resolution,
                          std::vector<render::Point> pixels,
                          QColor color);
    void clearPreviewPixels();
    void setPasteOverlay(std::optional<CanvasPasteOverlay> overlay);
    // Called first on a left press over the colorized frame; returning true takes the press,
    // so that no click or stroke follows (paste mode stamps with it).
    void setPressInterceptor(std::function<bool(render::CanvasHit, Qt::KeyboardModifiers)> handler);
    // Called on mouse moves without a button over the colorized frame; while one is set the
    // widget tracks the mouse.
    void setHoverHandler(std::function<void(render::CanvasHit)> handler);
    // Drops from other widgets of the window (a mask or background dragged from its list):
    // asked whether it takes what is dragged while it is over the canvas (drop false), then
    // told to take it (drop true). source: the widget the drag started in. Returns whether it
    // is taken.
    void setDropHandler(std::function<bool(QObject* source, bool drop)> handler);
    // The keyboard modifiers of the last left press, for the handlers of its click and stroke.
    [[nodiscard]] Qt::KeyboardModifiers pressModifiers() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] std::optional<render::CanvasHit> hitTestAt(QPoint position) const;

    // The canvas is split into halves one above the other, the colorized frame in the upper,
    // the original in the lower. Zoom 1 fits the whole frame into its half; above it the
    // frames grow beyond their halves, which show the same part of both. The shown part moves
    // with the scroll bars, the mouse wheel (with Shift sideways) and a trackpad; Ctrl (Cmd on
    // macOS) with the wheel, and a pinch, zoom around the mouse. The frame, the plane and the
    // view change, the zoom stays; a new project starts at 1.
    static constexpr double kMaxZoom = 32.0;
    [[nodiscard]] double zoom() const;
    // anchor: the widget point that stays on the same pixel; none keeps the middle.
    void setZoom(double zoom, std::optional<QPoint> anchor = std::nullopt);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    // The left and top of the shown part, as fractions of the frame (0 up to 1 minus the shown
    // fraction).
    [[nodiscard]] QPointF viewOffset() const;
    void setViewOffset(QPointF offset);
    // After the zoom or the shown part changed.
    std::function<void()> onViewChanged;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    [[nodiscard]] render::CanvasLayout currentLayout() const;
    [[nodiscard]] bool showsRuntimeView() const;
    // pictureRect: where the colorized picture is on screen.
    void paintRuntimeViewTexts(QPainter& painter, const QRectF& pictureRect);
    // The frame of an HD-only project has no HD data: nothing to show or to paint on.
    [[nodiscard]] bool isHdFrameMissing() const;
    // A region's half of the canvas and where its frame is drawn: the top left of the frame on
    // screen (outside the half when zoomed) and the screen pixels per display unit.
    struct RegionView {
        render::CanvasRegionLayout region;
        QRectF viewport;
        QPointF origin;
        double cell = 1.0;
        QSizeF contentSize;
    };
    [[nodiscard]] std::vector<RegionView> regionViews() const;
    [[nodiscard]] std::optional<RegionView> firstRegionView() const;
    // The fraction of the frame its half shows, across and down (1 when it fits).
    [[nodiscard]] QSizeF shownFraction() const;
    // Keeps the offset in range for the zoom and updates the scroll bars.
    void viewChanged();
    void updateScrollBars();
    void paintEmpty(QPainter& painter);
    void paintFrame(QPainter& painter);
    void paintRegion(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintComparisonMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintDifferenceOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintDynamicMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    // The dynamic mask the region shows zones of, or none.
    [[nodiscard]] const document::IndexedFrame* dynamicMaskFor(
        const render::CanvasRegionLayout& region) const;
    // The set numbers on the areas of the zones, in screen coordinates.
    void paintDynamicSetLabels(QPainter& painter, const RegionView& view);
    void updateMouseTracking();
    void paintBackgroundMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintSelectedSpriteBounds(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintSelectionOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintPreview(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintPasteOverlay(QPainter& painter, const render::CanvasRegionLayout& region);

    const document::ProjectDocument* m_document = nullptr;
    RuntimeView* m_runtimeView = nullptr;
    std::size_t m_frameIndex = 0;
    bool m_hasFrame = false;
    bool m_showComparisonMaskOverlay = false;
    std::optional<document::IndexedFrame> m_differenceOverlay;
    bool m_showDynamicMaskOverlay = false;
    std::optional<uint8_t> m_selectedDynamicSet;
    bool m_showBackgroundMaskOverlay = false;
    bool m_showBackgroundLayer = false;
    bool m_showBackgroundImageEditMode = false;
    std::optional<std::size_t> m_selectedBackgroundIndex;
    std::optional<int> m_colorRotationPreviewElapsedMs;
    bool m_showSelectedSpriteBoundsOverlay = true;
    DmdRenderOptions m_renderOptions;
    std::optional<document::FrameSpriteReference> m_selectedSpriteReference;
    render::CanvasRegion m_selectionOverlayRegion = render::CanvasRegion::OriginalFrame;
    std::optional<render::Resolution> m_selectionOverlayResolution;
    std::vector<render::Point> m_selectionOverlayPixels;
    QColor m_selectionOverlayColor;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    std::function<void(render::CanvasHit, bool)> m_pixelClickedHandler;
    std::function<void(render::CanvasHit, bool)> m_originalClickedHandler;
    std::function<void(render::CanvasHit, render::CanvasHit, bool)> m_strokePreviewHandler;
    std::function<void(render::CanvasHit, render::CanvasHit, bool)> m_strokeFinishedHandler;
    std::optional<render::CanvasHit> m_strokeStart;
    bool m_strokeErase = false;
    render::CanvasRegion m_previewRegion = render::CanvasRegion::ColorizedFrame;
    std::optional<render::Resolution> m_previewResolution;
    std::vector<render::Point> m_previewPixels;
    QColor m_previewColor;
    std::optional<CanvasPasteOverlay> m_pasteOverlay;
    std::function<bool(render::CanvasHit, Qt::KeyboardModifiers)> m_pressInterceptor;
    std::function<void(render::CanvasHit)> m_hoverHandler;
    std::function<bool(QObject* source, bool drop)> m_dropHandler;
    Qt::KeyboardModifiers m_pressModifiers;
    double m_zoom = 1.0;
    QPointF m_viewOffset;
    QScrollBar* m_horizontalScrollBar = nullptr;
    QScrollBar* m_verticalScrollBar = nullptr;
    bool m_updatingScrollBars = false;
};

}  // namespace colorizing::app
