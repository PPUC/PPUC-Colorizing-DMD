#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <QPoint>
#include <QColor>
#include <QWidget>

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
    void setDynamicMaskOverlayEnabled(bool enabled);
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
    // The keyboard modifiers of the last left press, for the handlers of its click and stroke.
    [[nodiscard]] Qt::KeyboardModifiers pressModifiers() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] std::optional<render::CanvasHit> hitTestAt(QPoint position) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] render::CanvasLayout currentLayout() const;
    [[nodiscard]] bool showsRuntimeView() const;
    void paintRuntimeViewTexts(QPainter& painter, const render::CanvasLayout& layout);
    // The frame of an HD-only project has no HD data: nothing to show or to paint on.
    [[nodiscard]] bool isHdFrameMissing() const;
    [[nodiscard]] double currentDisplayScale(const render::CanvasLayout& layout) const;
    [[nodiscard]] QPointF currentDisplayOrigin(const render::CanvasLayout& layout) const;
    void paintEmpty(QPainter& painter);
    void paintFrame(QPainter& painter);
    void paintRegion(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintComparisonMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintDifferenceOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintDynamicMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
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
    Qt::KeyboardModifiers m_pressModifiers;
};

}  // namespace colorizing::app
