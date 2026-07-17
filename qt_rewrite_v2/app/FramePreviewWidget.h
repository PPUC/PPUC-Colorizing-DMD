#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <QPoint>
#include <QWidget>

#include "document/ProjectDocument.h"
#include "render/CanvasLayout.h"

namespace colorizing::app {

class FramePreviewWidget final : public QWidget {
public:
    explicit FramePreviewWidget(QWidget* parent = nullptr);

    void setDocument(const document::ProjectDocument* document);
    void setFrameIndex(std::size_t frameIndex);
    void setActiveResolution(render::Resolution resolution);
    void setPixelClickedHandler(std::function<void(render::CanvasHit)> handler);
    void setStrokePreviewHandler(std::function<void(render::CanvasHit, render::CanvasHit)> handler);
    void setStrokeFinishedHandler(std::function<void(render::CanvasHit, render::CanvasHit)> handler);
    void setPreviewPixels(render::Resolution resolution,
                          std::vector<render::Point> pixels,
                          uint16_t rgb565);
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
    void paintPreview(QPainter& painter, const render::CanvasRegionLayout& region);

    const document::ProjectDocument* m_document = nullptr;
    std::size_t m_frameIndex = 0;
    bool m_hasFrame = false;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    std::function<void(render::CanvasHit)> m_pixelClickedHandler;
    std::function<void(render::CanvasHit, render::CanvasHit)> m_strokePreviewHandler;
    std::function<void(render::CanvasHit, render::CanvasHit)> m_strokeFinishedHandler;
    std::optional<render::CanvasHit> m_strokeStart;
    std::optional<render::Resolution> m_previewResolution;
    std::vector<render::Point> m_previewPixels;
    uint16_t m_previewColor = 0xffff;
};

}  // namespace colorizing::app
