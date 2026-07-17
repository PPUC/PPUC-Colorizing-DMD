#pragma once

#include <cstddef>
#include <functional>
#include <optional>

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

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] std::optional<render::CanvasHit> hitTestAt(QPoint position) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] render::CanvasLayout currentLayout() const;
    [[nodiscard]] double currentDisplayScale(const render::CanvasLayout& layout) const;
    [[nodiscard]] QPointF currentDisplayOrigin(const render::CanvasLayout& layout) const;
    void paintEmpty(QPainter& painter);
    void paintFrame(QPainter& painter);
    void paintRegion(QPainter& painter, const render::CanvasRegionLayout& region);

    const document::ProjectDocument* m_document = nullptr;
    std::size_t m_frameIndex = 0;
    bool m_hasFrame = false;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    std::function<void(render::CanvasHit)> m_pixelClickedHandler;
};

}  // namespace colorizing::app
