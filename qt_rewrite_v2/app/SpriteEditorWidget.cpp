#include "SpriteEditorWidget.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include "DrawingGeometry.h"

namespace colorizing::app {

namespace {

constexpr int kGap = 12;
constexpr render::Size kSpriteSize{static_cast<int>(document::kMaxSpriteWidth),
                                   static_cast<int>(document::kMaxSpriteHeight)};

std::size_t spritePixelIndex(int x, int y)
{
    return static_cast<std::size_t>(y) * document::kMaxSpriteWidth +
           static_cast<std::size_t>(x);
}

bool isValidDetectionArea(const document::SpriteDetectionArea& area)
{
    return area.x != 0xffff && area.y != 0xffff &&
           area.width != 0xffff && area.height != 0xffff &&
           area.width > 0 && area.height > 0;
}

QColor detectionAreaColor(std::size_t areaIndex, bool selected)
{
    static constexpr std::array<QColor, document::kMaxSpriteDetectAreas> colors{
        QColor(0, 255, 255, 86),
        QColor(255, 200, 0, 86),
        QColor(255, 0, 255, 86),
        QColor(0, 255, 0, 86),
    };
    QColor color = colors[areaIndex % colors.size()];
    if (selected) {
        color.setAlpha(128);
    }
    return color;
}

}  // namespace

SpriteEditorWidget::SpriteEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 220);
}

void SpriteEditorWidget::setDocument(const document::ProjectDocument* document)
{
    m_document = document;
    m_spriteIndex.reset();
    m_dragStart.reset();
    m_dragEnd.reset();
    update();
}

void SpriteEditorWidget::setSpriteIndex(std::optional<std::size_t> spriteIndex)
{
    if (m_spriteIndex == spriteIndex) {
        return;
    }
    m_spriteIndex = spriteIndex;
    m_dragStart.reset();
    m_dragEnd.reset();
    update();
}

void SpriteEditorWidget::setActiveResolution(render::Resolution resolution)
{
    if (m_activeResolution == resolution) {
        return;
    }
    m_activeResolution = resolution;
    update();
}

void SpriteEditorWidget::setDrawingTool(SpriteDrawingTool tool)
{
    if (m_drawingTool == tool) {
        return;
    }
    m_drawingTool = tool;
    m_colorizedStrokeStart.reset();
    m_colorizedStrokeEnd.reset();
    update();
}

void SpriteEditorWidget::setCanvasEditMode(SpriteCanvasEditMode mode)
{
    if (m_canvasEditMode == mode) {
        return;
    }
    m_canvasEditMode = mode;
    m_paintingColorized = false;
    m_paintingOriginal = false;
    m_dragStart.reset();
    m_dragEnd.reset();
    m_colorizedStrokeStart.reset();
    m_colorizedStrokeEnd.reset();
    m_originalStrokeStart.reset();
    m_originalStrokeEnd.reset();
    update();
}

void SpriteEditorWidget::setActiveDrawColor(uint16_t rgb565)
{
    if (m_activeDrawColor == rgb565) {
        return;
    }
    m_activeDrawColor = rgb565;
    update();
}

void SpriteEditorWidget::setOriginalDrawValue(uint8_t value)
{
    const uint8_t clampedValue = static_cast<uint8_t>(std::min<int>(value, 15));
    if (m_originalDrawValue == clampedValue) {
        return;
    }
    m_originalDrawValue = clampedValue;
    update();
}

void SpriteEditorWidget::setDmdRenderOptions(DmdRenderOptions options)
{
    if (m_renderOptions.pixelShape == options.pixelShape &&
        std::abs(m_renderOptions.pixelCoverage - options.pixelCoverage) < 0.001) {
        return;
    }
    m_renderOptions = options;
    update();
}

void SpriteEditorWidget::setDetectionAreasVisible(bool visible)
{
    if (m_showDetectionAreas == visible) {
        return;
    }
    m_showDetectionAreas = visible;
    update();
}

void SpriteEditorWidget::setSelectedDetectionAreaIndex(std::optional<std::size_t> areaIndex)
{
    if (m_selectedAreaIndex == areaIndex) {
        return;
    }
    m_selectedAreaIndex = areaIndex;
    update();
}

void SpriteEditorWidget::setDetectionAreaChangedHandler(
    std::function<void(std::size_t, document::SpriteDetectionArea)> handler)
{
    m_detectionAreaChangedHandler = std::move(handler);
}

void SpriteEditorWidget::setColorizedPixelEditedHandler(
    std::function<void(std::size_t, render::Resolution, int, int, bool)> handler)
{
    m_colorizedPixelEditedHandler = std::move(handler);
}

void SpriteEditorWidget::setColorizedPixelsEditedHandler(
    std::function<void(std::size_t, render::Resolution, const std::vector<render::Point>&, bool)> handler)
{
    m_colorizedPixelsEditedHandler = std::move(handler);
}

void SpriteEditorWidget::setColorizedPixelPickedHandler(
    std::function<void(std::size_t, render::Resolution, int, int)> handler)
{
    m_colorizedPixelPickedHandler = std::move(handler);
}

void SpriteEditorWidget::setColorizedFloodFillHandler(
    std::function<void(std::size_t, render::Resolution, int, int)> handler)
{
    m_colorizedFloodFillHandler = std::move(handler);
}

void SpriteEditorWidget::setColorizedPaintFinishedHandler(std::function<void()> handler)
{
    m_colorizedPaintFinishedHandler = std::move(handler);
}

void SpriteEditorWidget::setOriginalPixelsEditedHandler(
    std::function<void(std::size_t, const std::vector<render::Point>&, bool)> handler)
{
    m_originalPixelsEditedHandler = std::move(handler);
}

void SpriteEditorWidget::setOriginalPixelPickedHandler(
    std::function<void(std::size_t, int, int)> handler)
{
    m_originalPixelPickedHandler = std::move(handler);
}

void SpriteEditorWidget::setOriginalFloodFillHandler(
    std::function<void(std::size_t, int, int, bool)> handler)
{
    m_originalFloodFillHandler = std::move(handler);
}

void SpriteEditorWidget::setOriginalPaintFinishedHandler(std::function<void()> handler)
{
    m_originalPaintFinishedHandler = std::move(handler);
}

QSize SpriteEditorWidget::sizeHint() const
{
    return QSize(760, 440);
}

void SpriteEditorWidget::paintEvent(QPaintEvent* event)
{
    (void)event;
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    if (!m_document || !m_spriteIndex || *m_spriteIndex >= m_document->spriteCount()) {
        paintEmpty(painter);
        return;
    }

    const int scale = displayScale();
    const QPointF origin = displayOrigin();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing,
                          m_renderOptions.pixelShape == DmdPixelShape::Dot);
    painter.translate(origin);
    painter.scale(scale, scale);

    const RegionLayout colorized = colorizedRegion();
    const RegionLayout original = originalRegion();
    paintSpriteRegion(painter, colorized);
    paintSpriteRegion(painter, original);
    paintColorizedStrokePreview(painter, colorized);
    paintOriginalStrokePreview(painter, original);
    paintDetectionAreas(painter, original);
    paintDragPreview(painter, original);
    painter.restore();
}

void SpriteEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton &&
        m_canvasEditMode == SpriteCanvasEditMode::ColorizedPixels &&
        (m_colorizedPixelEditedHandler || m_colorizedPixelsEditedHandler ||
         m_colorizedPixelPickedHandler || m_colorizedFloodFillHandler) &&
        m_document && m_spriteIndex && *m_spriteIndex < m_document->spriteCount()) {
        const std::optional<render::Point> hit = hitColorized(event->pos());
        if (hit) {
            m_paintingColorized = true;
            m_paintErase = event->modifiers().testFlag(Qt::ShiftModifier);
            if (m_drawingTool == SpriteDrawingTool::Point) {
                m_colorizedPixelEditedHandler(*m_spriteIndex,
                                              colorizedPaintResolution(),
                                              hit->x,
                                              hit->y,
                                              m_paintErase);
            } else if (m_drawingTool == SpriteDrawingTool::ColorPicker &&
                       m_colorizedPixelPickedHandler) {
                m_colorizedPixelPickedHandler(*m_spriteIndex,
                                              colorizedPaintResolution(),
                                              hit->x,
                                              hit->y);
                m_paintingColorized = false;
                m_paintErase = false;
            } else if (m_drawingTool == SpriteDrawingTool::FloodFill &&
                       m_colorizedFloodFillHandler) {
                m_colorizedFloodFillHandler(*m_spriteIndex,
                                            colorizedPaintResolution(),
                                            hit->x,
                                            hit->y);
                m_paintingColorized = false;
                m_paintErase = false;
            } else if (editsColorizedStroke() && m_colorizedPixelsEditedHandler) {
                m_colorizedStrokeStart = *hit;
                m_colorizedStrokeEnd = *hit;
                update();
            } else {
                m_paintingColorized = false;
                m_paintErase = false;
                QWidget::mousePressEvent(event);
                return;
            }
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton &&
        m_canvasEditMode == SpriteCanvasEditMode::OriginalMask &&
        (m_originalPixelsEditedHandler || m_originalPixelPickedHandler ||
         m_originalFloodFillHandler) &&
        m_document && m_spriteIndex && *m_spriteIndex < m_document->spriteCount()) {
        const std::optional<render::Point> hit = hitOriginal(event->pos());
        if (hit) {
            m_paintingOriginal = true;
            m_paintErase = event->modifiers().testFlag(Qt::ShiftModifier);
            if (m_drawingTool == SpriteDrawingTool::Point && m_originalPixelsEditedHandler) {
                m_originalPixelsEditedHandler(*m_spriteIndex, {*hit}, m_paintErase);
            } else if (m_drawingTool == SpriteDrawingTool::ColorPicker &&
                       m_originalPixelPickedHandler) {
                m_originalPixelPickedHandler(*m_spriteIndex, hit->x, hit->y);
                m_paintingOriginal = false;
                m_paintErase = false;
            } else if (m_drawingTool == SpriteDrawingTool::FloodFill &&
                       m_originalFloodFillHandler) {
                m_originalFloodFillHandler(*m_spriteIndex, hit->x, hit->y, m_paintErase);
                m_paintingOriginal = false;
                m_paintErase = false;
            } else if (editsOriginalStroke() && m_originalPixelsEditedHandler) {
                m_originalStrokeStart = *hit;
                m_originalStrokeEnd = *hit;
                update();
            } else {
                m_paintingOriginal = false;
                m_paintErase = false;
                QWidget::mousePressEvent(event);
                return;
            }
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton &&
        m_canvasEditMode == SpriteCanvasEditMode::DetectionAreas &&
        m_selectedAreaIndex) {
        const std::optional<render::Point> hit = hitOriginal(event->pos());
        if (hit) {
            m_dragStart = *hit;
            m_dragEnd = *hit;
            update();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void SpriteEditorWidget::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && m_paintingColorized &&
        m_colorizedPixelEditedHandler && m_spriteIndex) {
        const std::optional<render::Point> hit = hitColorized(event->pos());
        if (hit) {
            if (m_drawingTool == SpriteDrawingTool::Point) {
                m_colorizedPixelEditedHandler(*m_spriteIndex,
                                              colorizedPaintResolution(),
                                              hit->x,
                                              hit->y,
                                              m_paintErase);
            } else if (m_colorizedStrokeStart && editsColorizedStroke()) {
                m_colorizedStrokeEnd = *hit;
                update();
            }
            event->accept();
            return;
        }
    }
    if ((event->buttons() & Qt::LeftButton) && m_paintingOriginal &&
        m_originalPixelsEditedHandler && m_spriteIndex) {
        const std::optional<render::Point> hit = hitOriginal(event->pos());
        if (hit) {
            if (m_drawingTool == SpriteDrawingTool::Point) {
                m_originalPixelsEditedHandler(*m_spriteIndex, {*hit}, m_paintErase);
            } else if (m_originalStrokeStart && editsOriginalStroke()) {
                m_originalStrokeEnd = *hit;
                update();
            }
            event->accept();
            return;
        }
    }
    if ((event->buttons() & Qt::LeftButton) && m_dragStart) {
        const std::optional<render::Point> hit = hitOriginal(event->pos());
        if (hit) {
            m_dragEnd = *hit;
            update();
            event->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

void SpriteEditorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_paintingColorized) {
        const std::optional<render::Point> hit = hitColorized(event->pos());
        if (m_colorizedStrokeStart && m_colorizedPixelsEditedHandler && m_spriteIndex &&
            editsColorizedStroke()) {
            const render::Point start = *m_colorizedStrokeStart;
            const render::Point end = hit.value_or(m_colorizedStrokeEnd.value_or(start));
            const std::vector<render::Point> pixels = pixelsForColorizedStroke(start, end);
            if (!pixels.empty()) {
                m_colorizedPixelsEditedHandler(*m_spriteIndex,
                                               colorizedPaintResolution(),
                                               pixels,
                                               m_paintErase);
            }
        }
        m_paintingColorized = false;
        m_paintErase = false;
        m_colorizedStrokeStart.reset();
        m_colorizedStrokeEnd.reset();
        if (m_colorizedPaintFinishedHandler) {
            m_colorizedPaintFinishedHandler();
        }
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_paintingOriginal) {
        const std::optional<render::Point> hit = hitOriginal(event->pos());
        if (m_originalStrokeStart && m_originalPixelsEditedHandler && m_spriteIndex &&
            editsOriginalStroke()) {
            const render::Point start = *m_originalStrokeStart;
            const render::Point end = hit.value_or(m_originalStrokeEnd.value_or(start));
            const std::vector<render::Point> pixels = pixelsForOriginalStroke(start, end);
            if (!pixels.empty()) {
                m_originalPixelsEditedHandler(*m_spriteIndex, pixels, m_paintErase);
            }
        }
        m_paintingOriginal = false;
        m_paintErase = false;
        m_originalStrokeStart.reset();
        m_originalStrokeEnd.reset();
        if (m_originalPaintFinishedHandler) {
            m_originalPaintFinishedHandler();
        }
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_dragStart && m_selectedAreaIndex) {
        const std::optional<render::Point> hit = hitOriginal(event->pos());
        const render::Point start = *m_dragStart;
        const render::Point end = hit.value_or(*m_dragEnd);
        const std::size_t areaIndex = *m_selectedAreaIndex;
        m_dragStart.reset();
        m_dragEnd.reset();
        if (m_detectionAreaChangedHandler) {
            m_detectionAreaChangedHandler(areaIndex, areaFromDrag(start, end));
        }
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SpriteEditorWidget::paintOriginalStrokePreview(QPainter& painter,
                                                    const RegionLayout& region)
{
    if (!m_originalStrokeStart || !m_originalStrokeEnd || !m_spriteIndex ||
        !editsOriginalStroke()) {
        return;
    }
    QColor previewColor = m_paintErase ? QColor(0, 0, 0, 150)
                                       : originalIndexToColor(m_originalDrawValue);
    if (!m_paintErase) {
        previewColor.setAlpha(150);
    }
    painter.save();
    for (const render::Point point : pixelsForOriginalStroke(*m_originalStrokeStart,
                                                             *m_originalStrokeEnd)) {
        if (point.x < 0 || point.y < 0 ||
            point.x >= kSpriteSize.width || point.y >= kSpriteSize.height) {
            continue;
        }
        paintDmdPixel(painter,
                      region.rect.x + point.x,
                      region.rect.y + point.y,
                      1.0,
                      previewColor,
                      m_renderOptions);
    }
    painter.restore();
}

int SpriteEditorWidget::displayScale() const
{
    const int logicalWidth = kSpriteSize.width;
    const int logicalHeight = kSpriteSize.height * 2 + kGap;
    const double fittedScale = std::min(
        static_cast<double>(std::max(1, width() - 24)) / static_cast<double>(logicalWidth),
        static_cast<double>(std::max(1, height() - 36)) / static_cast<double>(logicalHeight));
    return std::max(1, static_cast<int>(std::floor(fittedScale)));
}

QPointF SpriteEditorWidget::displayOrigin() const
{
    const int scale = displayScale();
    const int logicalWidth = kSpriteSize.width;
    const int logicalHeight = kSpriteSize.height * 2 + kGap;
    return QPointF(std::floor((width() - logicalWidth * scale) / 2.0),
                   std::floor((height() - logicalHeight * scale) / 2.0));
}

SpriteEditorWidget::RegionLayout SpriteEditorWidget::colorizedRegion() const
{
    return RegionLayout{
        render::CanvasRegion::SpriteColorized,
        render::Rect{0, 0, kSpriteSize.width, kSpriteSize.height},
    };
}

SpriteEditorWidget::RegionLayout SpriteEditorWidget::originalRegion() const
{
    return RegionLayout{
        render::CanvasRegion::SpriteOriginal,
        render::Rect{0, kSpriteSize.height + kGap, kSpriteSize.width, kSpriteSize.height},
    };
}

render::Resolution SpriteEditorWidget::colorizedPaintResolution() const
{
    if (!m_document || !m_spriteIndex || *m_spriteIndex >= m_document->spriteCount()) {
        return render::Resolution::Sd;
    }
    const document::SpriteRecord& sprite = m_document->sprite(*m_spriteIndex);
    return m_activeResolution == render::Resolution::Hd && sprite.hasHd
               ? render::Resolution::Hd
               : render::Resolution::Sd;
}

std::optional<render::Point> SpriteEditorWidget::hitColorized(QPoint position) const
{
    if (!m_document || !m_spriteIndex || *m_spriteIndex >= m_document->spriteCount()) {
        return std::nullopt;
    }
    const int scale = displayScale();
    if (scale <= 0) {
        return std::nullopt;
    }
    const QPointF origin = displayOrigin();
    const RegionLayout colorized = colorizedRegion();
    const double logicalX = (position.x() - origin.x()) / scale;
    const double logicalY = (position.y() - origin.y()) / scale;
    if (logicalX < colorized.rect.x || logicalY < colorized.rect.y ||
        logicalX >= colorized.rect.x + colorized.rect.width ||
        logicalY >= colorized.rect.y + colorized.rect.height) {
        return std::nullopt;
    }
    const render::Resolution resolution = colorizedPaintResolution();
    const double localX = logicalX - colorized.rect.x;
    const double localY = logicalY - colorized.rect.y;
    const int sourceX =
        resolution == render::Resolution::Hd ? static_cast<int>(std::floor(localX * 2.0))
                                             : static_cast<int>(std::floor(localX));
    const int sourceY =
        resolution == render::Resolution::Hd ? static_cast<int>(std::floor(localY * 2.0))
                                             : static_cast<int>(std::floor(localY));
    if (sourceX < 0 || sourceY < 0 ||
        sourceX >= kSpriteSize.width || sourceY >= kSpriteSize.height) {
        return std::nullopt;
    }
    return render::Point{sourceX, sourceY};
}

std::optional<render::Point> SpriteEditorWidget::hitOriginal(QPoint position) const
{
    if (!m_document || !m_spriteIndex || *m_spriteIndex >= m_document->spriteCount()) {
        return std::nullopt;
    }
    const int scale = displayScale();
    if (scale <= 0) {
        return std::nullopt;
    }
    const QPointF origin = displayOrigin();
    const RegionLayout original = originalRegion();
    const int logicalX = static_cast<int>(std::floor((position.x() - origin.x()) / scale));
    const int logicalY = static_cast<int>(std::floor((position.y() - origin.y()) / scale));
    if (!original.rect.contains(render::Point{logicalX, logicalY})) {
        return std::nullopt;
    }
    return render::Point{logicalX - original.rect.x, logicalY - original.rect.y};
}

void SpriteEditorWidget::paintEmpty(QPainter& painter)
{
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No sprite selected"));
}

void SpriteEditorWidget::paintSpriteRegion(QPainter& painter, const RegionLayout& region)
{
    painter.fillRect(QRect(region.rect.x, region.rect.y, region.rect.width, region.rect.height),
                     QColor(0, 0, 0));

    const document::SpriteRecord& sprite = m_document->sprite(*m_spriteIndex);
    const bool useHd = m_activeResolution == render::Resolution::Hd && sprite.hasHd;
    const document::IndexedFrame& mask =
        region.region == render::CanvasRegion::SpriteOriginal
            ? sprite.originalSd
            : useHd ? sprite.maskHd : sprite.originalSd;
    const document::Rgb565Frame& colorized = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    const bool drawHdColorized =
        region.region == render::CanvasRegion::SpriteColorized && useHd;
    const double cellSize = drawHdColorized ? 0.5 : 1.0;
    for (int y = 0; y < kSpriteSize.height; ++y) {
        for (int x = 0; x < kSpriteSize.width; ++x) {
            const std::size_t index = spritePixelIndex(x, y);
            if (mask.pixels[index] == 255) {
                continue;
            }
            const QColor color =
                region.region == render::CanvasRegion::SpriteOriginal
                    ? originalIndexToColor(mask.pixels[index])
                    : rgb565ToColor(colorized.pixels[index]);
            paintDmdPixel(painter,
                          region.rect.x + x * cellSize,
                          region.rect.y + y * cellSize,
                          cellSize,
                          color,
                          m_renderOptions);
        }
    }
}

void SpriteEditorWidget::paintDetectionAreas(QPainter& painter, const RegionLayout& region)
{
    if (!m_showDetectionAreas || !m_spriteIndex) {
        return;
    }
    const document::SpriteRecord& sprite = m_document->sprite(*m_spriteIndex);
    painter.save();
    painter.setPen(Qt::NoPen);
    for (std::size_t areaIndex = 0;
         areaIndex < sprite.detectionAreas.size() &&
             areaIndex < document::kMaxSpriteDetectAreas;
         ++areaIndex) {
        const document::SpriteDetectionArea& area = sprite.detectionAreas[areaIndex];
        if (!isValidDetectionArea(area)) {
            continue;
        }
        const int x = std::clamp(static_cast<int>(area.x), 0, kSpriteSize.width - 1);
        const int y = std::clamp(static_cast<int>(area.y), 0, kSpriteSize.height - 1);
        const int right =
            std::clamp(static_cast<int>(area.x) + static_cast<int>(area.width),
                       0,
                       kSpriteSize.width);
        const int bottom =
            std::clamp(static_cast<int>(area.y) + static_cast<int>(area.height),
                       0,
                       kSpriteSize.height);
        if (right <= x || bottom <= y) {
            continue;
        }
        painter.fillRect(QRect(region.rect.x + x,
                               region.rect.y + y,
                               right - x,
                               bottom - y),
                         detectionAreaColor(areaIndex, m_selectedAreaIndex == areaIndex));
    }
    painter.restore();
}

void SpriteEditorWidget::paintDragPreview(QPainter& painter, const RegionLayout& region)
{
    if (!m_dragStart || !m_dragEnd || !m_selectedAreaIndex) {
        return;
    }
    const document::SpriteDetectionArea area = areaFromDrag(*m_dragStart, *m_dragEnd);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.fillRect(QRect(region.rect.x + static_cast<int>(area.x),
                           region.rect.y + static_cast<int>(area.y),
                           static_cast<int>(area.width),
                           static_cast<int>(area.height)),
                     detectionAreaColor(*m_selectedAreaIndex, true));
    painter.restore();
}

void SpriteEditorWidget::paintColorizedStrokePreview(QPainter& painter, const RegionLayout& region)
{
    if (!m_colorizedStrokeStart || !m_colorizedStrokeEnd || !m_spriteIndex ||
        !editsColorizedStroke()) {
        return;
    }
    const document::SpriteRecord& sprite = m_document->sprite(*m_spriteIndex);
    const render::Resolution resolution = colorizedPaintResolution();
    QColor previewColor = m_paintErase ? QColor(0, 0, 0, 150) : rgb565ToColor(m_activeDrawColor);
    if (!m_paintErase) {
        previewColor.setAlpha(150);
    }

    painter.save();
    for (const render::Point point : pixelsForColorizedStroke(*m_colorizedStrokeStart,
                                                              *m_colorizedStrokeEnd)) {
        if (!isColorizedPixelEditable(sprite, resolution, point, m_paintErase)) {
            continue;
        }
        const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
        const double cellSize = useHd ? 0.5 : 1.0;
        paintDmdPixel(painter,
                      region.rect.x + point.x * cellSize,
                      region.rect.y + point.y * cellSize,
                      cellSize,
                      previewColor,
                      m_renderOptions);
    }
    painter.restore();
}

bool SpriteEditorWidget::isColorizedPixelEditable(const document::SpriteRecord& sprite,
                                                  render::Resolution resolution,
                                                  render::Point point,
                                                  bool erase) const
{
    if (point.x < 0 || point.y < 0 ||
        point.x >= kSpriteSize.width || point.y >= kSpriteSize.height) {
        return false;
    }
    const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
    const document::IndexedFrame& mask = useHd ? sprite.maskHd : sprite.originalSd;
    const document::Rgb565Frame& colorized = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    const std::size_t index = spritePixelIndex(point.x, point.y);
    if (mask.pixels[index] == 255) {
        return false;
    }
    return erase || colorized.pixels[index] != 0;
}

std::vector<render::Point> SpriteEditorWidget::pixelsForColorizedStroke(render::Point start,
                                                                        render::Point end) const
{
    if (m_drawingTool == SpriteDrawingTool::Line) {
        return makeLinePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::Rectangle) {
        return makeRectanglePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::FilledRectangle) {
        return makeFilledRectanglePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::Circle) {
        return makeEllipsePixels(start, squareConstrainedEnd(start, end));
    }
    if (m_drawingTool == SpriteDrawingTool::FilledCircle) {
        return makeFilledEllipsePixels(start, squareConstrainedEnd(start, end));
    }
    if (m_drawingTool == SpriteDrawingTool::Ellipse) {
        return makeEllipsePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::FilledEllipse) {
        return makeFilledEllipsePixels(start, end);
    }
    return {};
}

std::vector<render::Point> SpriteEditorWidget::pixelsForOriginalStroke(render::Point start,
                                                                       render::Point end) const
{
    return pixelsForColorizedStroke(start, end);
}

bool SpriteEditorWidget::editsColorizedStroke() const
{
    return m_drawingTool == SpriteDrawingTool::Line ||
           m_drawingTool == SpriteDrawingTool::Rectangle ||
           m_drawingTool == SpriteDrawingTool::FilledRectangle ||
           m_drawingTool == SpriteDrawingTool::Circle ||
           m_drawingTool == SpriteDrawingTool::FilledCircle ||
           m_drawingTool == SpriteDrawingTool::Ellipse ||
           m_drawingTool == SpriteDrawingTool::FilledEllipse;
}

bool SpriteEditorWidget::editsOriginalStroke() const
{
    return editsColorizedStroke();
}

document::SpriteDetectionArea SpriteEditorWidget::areaFromDrag(render::Point start,
                                                               render::Point end) const
{
    const int minX = std::clamp(std::min(start.x, end.x), 0, kSpriteSize.width - 1);
    const int maxX = std::clamp(std::max(start.x, end.x), 0, kSpriteSize.width - 1);
    const int minY = std::clamp(std::min(start.y, end.y), 0, kSpriteSize.height - 1);
    const int maxY = std::clamp(std::max(start.y, end.y), 0, kSpriteSize.height - 1);
    return document::SpriteDetectionArea{
        static_cast<uint16_t>(minX),
        static_cast<uint16_t>(minY),
        static_cast<uint16_t>(maxX - minX + 1),
        static_cast<uint16_t>(maxY - minY + 1),
    };
}

}  // namespace colorizing::app
