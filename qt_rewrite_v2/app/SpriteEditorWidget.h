#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <QPoint>
#include <QWidget>

#include "document/ProjectDocument.h"
#include "FrameImageUtils.h"
#include "render/CanvasTypes.h"

class QPainter;

namespace colorizing::app {

enum class SpriteDrawingTool {
    Point,
    Line,
    Rectangle,
    FilledRectangle,
    Circle,
    FilledCircle,
    Ellipse,
    FilledEllipse,
    ColorPicker,
    FloodFill,
};

class SpriteEditorWidget final : public QWidget {
public:
    explicit SpriteEditorWidget(QWidget* parent = nullptr);

    void setDocument(const document::ProjectDocument* document);
    void setSpriteIndex(std::optional<std::size_t> spriteIndex);
    void setActiveResolution(render::Resolution resolution);
    void setDrawingTool(SpriteDrawingTool tool);
    void setActiveDrawColor(uint16_t rgb565);
    void setDmdRenderOptions(DmdRenderOptions options);
    void setDetectionAreasVisible(bool visible);
    void setSelectedDetectionAreaIndex(std::optional<std::size_t> areaIndex);
    void setDetectionAreaChangedHandler(
        std::function<void(std::size_t, document::SpriteDetectionArea)> handler);
    void setColorizedPixelEditedHandler(
        std::function<void(std::size_t, render::Resolution, int, int, bool)> handler);
    void setColorizedPixelsEditedHandler(
        std::function<void(std::size_t, render::Resolution, const std::vector<render::Point>&, bool)> handler);
    void setColorizedPaintFinishedHandler(std::function<void()> handler);

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
    [[nodiscard]] render::Resolution colorizedPaintResolution() const;
    [[nodiscard]] std::optional<render::Point> hitColorized(QPoint position) const;
    [[nodiscard]] std::optional<render::Point> hitOriginal(QPoint position) const;
    void paintEmpty(QPainter& painter);
    void paintSpriteRegion(QPainter& painter, const RegionLayout& region);
    void paintDetectionAreas(QPainter& painter, const RegionLayout& region);
    void paintDragPreview(QPainter& painter, const RegionLayout& region);
    void paintColorizedStrokePreview(QPainter& painter, const RegionLayout& region);
    [[nodiscard]] bool isColorizedPixelEditable(const document::SpriteRecord& sprite,
                                                render::Resolution resolution,
                                                render::Point point,
                                                bool erase) const;
    [[nodiscard]] std::vector<render::Point> pixelsForColorizedStroke(render::Point start,
                                                                      render::Point end) const;
    [[nodiscard]] bool editsColorizedStroke() const;
    [[nodiscard]] document::SpriteDetectionArea areaFromDrag(render::Point start,
                                                             render::Point end) const;

    const document::ProjectDocument* m_document = nullptr;
    std::optional<std::size_t> m_spriteIndex;
    render::Resolution m_activeResolution = render::Resolution::Sd;
    SpriteDrawingTool m_drawingTool = SpriteDrawingTool::Point;
    uint16_t m_activeDrawColor = 0xffff;
    DmdRenderOptions m_renderOptions;
    bool m_showDetectionAreas = true;
    std::optional<std::size_t> m_selectedAreaIndex;
    std::function<void(std::size_t, document::SpriteDetectionArea)> m_detectionAreaChangedHandler;
    std::function<void(std::size_t, render::Resolution, int, int, bool)> m_colorizedPixelEditedHandler;
    std::function<void(std::size_t, render::Resolution, const std::vector<render::Point>&, bool)> m_colorizedPixelsEditedHandler;
    std::function<void()> m_colorizedPaintFinishedHandler;
    bool m_paintingColorized = false;
    bool m_paintErase = false;
    std::optional<render::Point> m_dragStart;
    std::optional<render::Point> m_dragEnd;
    std::optional<render::Point> m_colorizedStrokeStart;
    std::optional<render::Point> m_colorizedStrokeEnd;
};

}  // namespace colorizing::app
