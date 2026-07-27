#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <QPoint>
#include <QColor>
#include <QWidget>

#include "document/ProjectDocument.h"
#include "FrameImageUtils.h"
#include "render/CanvasLayout.h"

namespace colorizing::app {

class FramePreviewWidget final : public QWidget {
public:
    explicit FramePreviewWidget(QWidget* parent = nullptr);

    void setDocument(const document::ProjectDocument* document);
    void setFrameIndex(std::size_t frameIndex);
    void setActiveResolution(render::Resolution resolution);
    void setComparisonMaskOverlayEnabled(bool enabled);
    void setDynamicMaskOverlayEnabled(bool enabled);
    void setBackgroundMaskOverlayEnabled(bool enabled);
    void setBackgroundLayerEnabled(bool enabled);
    void setBackgroundImageEditModeEnabled(bool enabled);
    void setSelectedBackgroundIndex(std::optional<std::size_t> backgroundIndex);
    void setDmdRenderOptions(DmdRenderOptions options);
    void setSelectedSpriteBoundsOverlayEnabled(bool enabled);
    void setSelectedSpriteReference(std::optional<document::FrameSpriteReference> reference);
    void setPixelClickedHandler(std::function<void(render::CanvasHit, bool)> handler);
    void setOriginalClickedHandler(std::function<void(render::CanvasHit, bool)> handler);
    void setStrokePreviewHandler(std::function<void(render::CanvasHit, render::CanvasHit, bool)> handler);
    void setStrokeFinishedHandler(std::function<void(render::CanvasHit, render::CanvasHit, bool)> handler);
    void setPreviewPixels(render::CanvasRegion region,
                          render::Resolution resolution,
                          std::vector<render::Point> pixels,
                          QColor color);
    void clearPreviewPixels();

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] std::optional<render::CanvasHit> hitTestAt(QPoint position) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] render::CanvasLayout currentLayout() const;
    [[nodiscard]] double currentDisplayScale(const render::CanvasLayout& layout) const;
    [[nodiscard]] QPointF currentDisplayOrigin(const render::CanvasLayout& layout) const;
    void paintEmpty(QPainter& painter);
    void paintFrame(QPainter& painter);
    void paintRegion(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintComparisonMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintDynamicMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintBackgroundMaskOverlay(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintSelectedSpriteBounds(QPainter& painter, const render::CanvasRegionLayout& region);
    void paintPreview(QPainter& painter, const render::CanvasRegionLayout& region);

    const document::ProjectDocument* m_document = nullptr;
    std::size_t m_frameIndex = 0;
    bool m_hasFrame = false;
    bool m_showComparisonMaskOverlay = false;
    bool m_showDynamicMaskOverlay = false;
    bool m_showBackgroundMaskOverlay = false;
    bool m_showBackgroundLayer = false;
    bool m_showBackgroundImageEditMode = false;
    std::optional<std::size_t> m_selectedBackgroundIndex;
    bool m_showSelectedSpriteBoundsOverlay = true;
    DmdRenderOptions m_renderOptions;
    std::optional<document::FrameSpriteReference> m_selectedSpriteReference;
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
};

}  // namespace colorizing::app
