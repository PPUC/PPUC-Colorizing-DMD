#include "SpriteEditorWidget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

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

std::vector<render::Point> makeLinePixels(render::Point start, render::Point end)
{
    std::vector<render::Point> pixels;
    int x0 = start.x;
    int y0 = start.y;
    const int x1 = end.x;
    const int y1 = end.y;
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        pixels.push_back(render::Point{x0, y0});
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubledError = 2 * error;
        if (doubledError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubledError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
    return pixels;
}

std::vector<render::Point> makeRectanglePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    std::vector<render::Point> pixels;
    for (int x = left; x <= right; ++x) {
        pixels.push_back(render::Point{x, top});
        if (bottom != top) {
            pixels.push_back(render::Point{x, bottom});
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        pixels.push_back(render::Point{left, y});
        if (right != left) {
            pixels.push_back(render::Point{right, y});
        }
    }
    return pixels;
}

std::vector<render::Point> makeFilledRectanglePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    std::vector<render::Point> pixels;
    pixels.reserve(static_cast<std::size_t>((right - left + 1) * (bottom - top + 1)));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            pixels.push_back(render::Point{x, y});
        }
    }
    return pixels;
}

std::vector<render::Point> makeFilledEllipsePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    const double centerX = (static_cast<double>(left) + static_cast<double>(right)) / 2.0;
    const double centerY = (static_cast<double>(top) + static_cast<double>(bottom)) / 2.0;
    const double radiusX = std::max(0.5, (static_cast<double>(right - left) + 1.0) / 2.0);
    const double radiusY = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) / 2.0);
    std::vector<render::Point> pixels;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const double normalizedX = (static_cast<double>(x) - centerX) / radiusX;
            const double normalizedY = (static_cast<double>(y) - centerY) / radiusY;
            if (normalizedX * normalizedX + normalizedY * normalizedY <= 1.0) {
                pixels.push_back(render::Point{x, y});
            }
        }
    }
    return pixels;
}

std::vector<render::Point> makeEllipsePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    const double centerX = (static_cast<double>(left) + static_cast<double>(right)) / 2.0;
    const double centerY = (static_cast<double>(top) + static_cast<double>(bottom)) / 2.0;
    const double radiusX = std::max(0.5, (static_cast<double>(right - left) + 1.0) / 2.0);
    const double radiusY = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) / 2.0);
    std::set<std::pair<int, int>> uniquePixels;

    auto insideEllipse = [&](int x, int y) {
        const double normalizedX = (static_cast<double>(x) - centerX) / radiusX;
        const double normalizedY = (static_cast<double>(y) - centerY) / radiusY;
        return normalizedX * normalizedX + normalizedY * normalizedY <= 1.0;
    };

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (insideEllipse(x, y) &&
                (!insideEllipse(x - 1, y) || !insideEllipse(x + 1, y) ||
                 !insideEllipse(x, y - 1) || !insideEllipse(x, y + 1))) {
                uniquePixels.insert({x, y});
            }
        }
    }

    std::vector<render::Point> pixels;
    pixels.reserve(uniquePixels.size());
    for (const auto& [x, y] : uniquePixels) {
        pixels.push_back(render::Point{x, y});
    }
    return pixels;
}

render::Point squareConstrainedEnd(render::Point start, render::Point end)
{
    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    const int side = std::min(std::abs(dx), std::abs(dy));
    const int xSign = dx < 0 ? -1 : 1;
    const int ySign = dy < 0 ? -1 : 1;
    return render::Point{start.x + xSign * side, start.y + ySign * side};
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

void SpriteEditorWidget::setActiveDrawColor(uint16_t rgb565)
{
    if (m_activeDrawColor == rgb565) {
        return;
    }
    m_activeDrawColor = rgb565;
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

void SpriteEditorWidget::setColorizedPaintFinishedHandler(std::function<void()> handler)
{
    m_colorizedPaintFinishedHandler = std::move(handler);
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
    paintDetectionAreas(painter, original);
    paintDragPreview(painter, original);
    painter.restore();
}

void SpriteEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_colorizedPixelEditedHandler &&
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
    if (event->button() == Qt::LeftButton && m_selectedAreaIndex) {
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
    const int logicalX = static_cast<int>(std::floor((position.x() - origin.x()) / scale));
    const int logicalY = static_cast<int>(std::floor((position.y() - origin.y()) / scale));
    if (!colorized.rect.contains(render::Point{logicalX, logicalY})) {
        return std::nullopt;
    }
    return render::Point{logicalX - colorized.rect.x, logicalY - colorized.rect.y};
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
    const int scale = 1;
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
                          region.rect.x + x * scale,
                          region.rect.y + y * scale,
                          scale,
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
        paintDmdPixel(painter,
                      region.rect.x + point.x,
                      region.rect.y + point.y,
                      1,
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
