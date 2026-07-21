#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include <QPoint>
#include <QWidget>

#include "document/ProjectDocument.h"
#include "FrameImageUtils.h"
#include "render/CanvasTypes.h"

class QPainter;

namespace colorizing::app {

class SpriteEditorWidget final : public QWidget {
public:
    explicit SpriteEditorWidget(QWidget* parent = nullptr);

    void setDocument(const document::ProjectDocument* document);
    void setSpriteIndex(std::optional<std::size_t> spriteIndex);
    void setActiveResolution(render::Resolution resolution);
    void setDmdRenderOptions(DmdRenderOptions options);
    void setDetectionAreasVisible(bool visible);
    void setSelectedDetectionAreaIndex(std::optional<std::size_t> areaIndex);
    void setDetectionAreaChangedHandler(
        std::function<void(std::size_t, document::SpriteDetectionArea)> handler);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct RegionLayout {
        render::CanvasRegion region = render::CanvasRegion::SpriteColorized;
        render::Rect rect;
    };

    [[nodiscard]] int displayScale() const;
    [[nodiscard]] QPointF displayOrigin() const;
    [[nodiscard]] RegionLayout colorizedRegion() const;
    [[nodiscard]] RegionLayout originalRegion() const;
    [[nodiscard]] std::optional<render::Point> hitOriginal(QPoint position) const;
    void paintEmpty(QPainter& painter);
    void paintSpriteRegion(QPainter& painter, const RegionLayout& region);
    void paintDetectionAreas(QPainter& painter, const RegionLayout& region);
    void paintDragPreview(QPainter& painter, const RegionLayout& region);
    [[nodiscard]] document::SpriteDetectionArea areaFromDrag(render::Point start,
                                                             render::Point end) const;

    const document::ProjectDocument* m_document = nullptr;
    std::optional<std::size_t> m_spriteIndex;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    DmdRenderOptions m_renderOptions;
    bool m_showDetectionAreas = true;
    std::optional<std::size_t> m_selectedAreaIndex;
    std::function<void(std::size_t, document::SpriteDetectionArea)> m_detectionAreaChangedHandler;
    std::optional<render::Point> m_dragStart;
    std::optional<render::Point> m_dragEnd;
};

}  // namespace colorizing::app
