#include "SpriteEditorWidget.h"

#include <QGuiApplication>
#include <tuple>

#include <algorithm>
#include <array>
#include <cmath>

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include "edit/DrawingGeometry.h"
#include "edit/DrawingTools.h"

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
    m_horizontalScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_verticalScrollBar = new QScrollBar(Qt::Vertical, this);
    m_horizontalScrollBar->hide();
    m_verticalScrollBar->hide();
    connect(m_horizontalScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        if (!m_updatingScrollBars) {
            m_scroll.setX(value);
            update();
        }
    });
    connect(m_verticalScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        if (!m_updatingScrollBars) {
            m_scroll.setY(value);
            update();
        }
    });
}

QRect SpriteEditorWidget::viewport(bool original) const
{
    const int thickness = m_horizontalScrollBar->sizeHint().height();
    const int available = std::max(2, height() - thickness - kGap);
    const int half = available / 2;
    const int width = std::max(1, this->width() - thickness);
    return original ? QRect(0, half + kGap, width, half) : QRect(0, 0, width, half);
}

void SpriteEditorWidget::viewChanged()
{
    const int scale = displayScale();
    const QRect half = viewport(false);
    const int hidden = std::max(0, kSpriteSize.width * scale - half.width());
    const int hiddenRows = std::max(0, kSpriteSize.height * scale - half.height());
    m_scroll = QPointF(std::clamp(m_scroll.x(), 0.0, static_cast<double>(hidden)),
                       std::clamp(m_scroll.y(), 0.0, static_cast<double>(hiddenRows)));
    m_updatingScrollBars = true;
    const auto set = [](QScrollBar* bar, int range, int page, double value) {
        bar->setVisible(range > 0);
        bar->setRange(0, range);
        bar->setPageStep(page);
        bar->setSingleStep(std::max(1, page / 8));
        bar->setValue(static_cast<int>(std::lround(value)));
    };
    set(m_horizontalScrollBar, hidden, half.width(), m_scroll.x());
    set(m_verticalScrollBar, hiddenRows, half.height(), m_scroll.y());
    m_updatingScrollBars = false;
    update();
}

QPointF SpriteEditorWidget::pixelCentre(bool original, render::Point pixel) const
{
    const double scale = displayScale();
    return displayOrigin(original) + QPointF((pixel.x + 0.5) * scale, (pixel.y + 0.5) * scale);
}

void SpriteEditorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const int thickness = m_horizontalScrollBar->sizeHint().height();
    m_horizontalScrollBar->setGeometry(0, height() - thickness, width() - thickness, thickness);
    m_verticalScrollBar->setGeometry(width() - thickness, 0, thickness, height() - thickness);
    viewChanged();
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

void SpriteEditorWidget::setColorSetReplace(bool enabled)
{
    if (m_colorSetReplace == enabled) {
        return;
    }
    m_colorSetReplace = enabled;
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
        std::abs(m_renderOptions.pixelCoverage - options.pixelCoverage) < 0.001 &&
        m_renderOptions.offPixelColor == options.offPixelColor) {
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
    for (const bool original : {false, true}) {
        painter.save();
        painter.setClipRect(viewport(original));
        painter.setRenderHint(QPainter::Antialiasing,
                              m_renderOptions.pixelShape == DmdPixelShape::Dot);
        painter.translate(displayOrigin(original));
        painter.scale(scale, scale);
        if (original) {
            const RegionLayout region = originalRegion();
            paintSpriteRegion(painter, region);
            paintDetectionAreas(painter, region);
            paintDragPreview(painter, region);
        } else {
            const RegionLayout region = colorizedRegion();
            paintSpriteRegion(painter, region);
            paintColorizedStrokePreview(painter, region);
        }
        painter.restore();
    }
}

void SpriteEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_panMode && event->button() == Qt::LeftButton) {
        m_panFrom = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton &&
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
    if (m_panFrom) {
        m_scroll -= event->position() - *m_panFrom;
        m_panFrom = event->position();
        viewChanged();
        event->accept();
        return;
    }
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
    if (m_panFrom && event->button() == Qt::LeftButton) {
        m_panFrom.reset();
        if (m_panMode) {
            setCursor(Qt::OpenHandCursor);
        } else {
            unsetCursor();
        }
        event->accept();
        return;
    }
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

int SpriteEditorWidget::fittedScale() const
{
    // Whole screen pixels per sprite pixel that fit a half.
    const QRect half = viewport(false);
    const double fitted = std::min(
        static_cast<double>(std::max(1, half.width() - 16)) / kSpriteSize.width,
        static_cast<double>(std::max(1, half.height() - 8)) / kSpriteSize.height);
    return std::max(1, static_cast<int>(std::floor(fitted)));
}

int SpriteEditorWidget::displayScale() const
{
    return std::max(1, static_cast<int>(std::lround(fittedScale() * m_zoom)));
}

QPointF SpriteEditorWidget::displayOrigin(bool original) const
{
    const int scale = displayScale();
    const QRect half = viewport(original);
    const double contentWidth = kSpriteSize.width * scale;
    const double contentHeight = kSpriteSize.height * scale;
    // Centred while it fits; otherwise moved by the scroll, which keeps it covering the half.
    const auto along = [](double extent, double content, double scrolled) {
        if (content <= extent) {
            return std::floor((extent - content) / 2.0);
        }
        return -std::clamp(scrolled, 0.0, content - extent);
    };
    return QPointF(half.left() + along(half.width(), contentWidth, m_scroll.x()),
                   half.top() + along(half.height(), contentHeight, m_scroll.y()));
}

double SpriteEditorWidget::zoom() const
{
    return m_zoom;
}

void SpriteEditorWidget::setZoom(double zoom, std::optional<QPoint> anchor)
{
    zoom = std::clamp(zoom, 1.0, kMaxZoom);
    // The sprite point under the anchor stays there, in the half it is in (the middle of the
    // upper half without one).
    const bool original = anchor && viewport(true).contains(*anchor);
    const QRect half = viewport(original);
    const QPointF at = anchor ? QPointF(*anchor) : QPointF(half.center());
    const QPointF origin = displayOrigin(original);
    const double oldScale = displayScale();
    const QPointF logical = (at - origin) / oldScale;
    m_zoom = zoom;
    const double newScale = displayScale();
    m_scroll = logical * newScale - (at - QPointF(half.topLeft()));
    viewChanged();
    if (onZoomChanged) {
        onZoomChanged();
    }
}

void SpriteEditorWidget::setPanMode(bool on)
{
    if (m_panMode == on) {
        return;
    }
    m_panMode = on;
    if (!on) {
        m_panFrom.reset();
        unsetCursor();
        return;
    }
    setCursor(m_panFrom ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
}

void SpriteEditorWidget::zoomIn()
{
    setZoom(m_zoom * 1.25);
}

void SpriteEditorWidget::zoomOut()
{
    setZoom(m_zoom / 1.25);
}

void SpriteEditorWidget::resetZoom()
{
    m_scroll = QPointF();
    setZoom(1.0);
}

void SpriteEditorWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        const int steps = event->angleDelta().y() / 120;
        if (steps != 0) {
            setZoom(m_zoom * std::pow(1.25, steps), event->position().toPoint());
        }
        event->accept();
        return;
    }
    // Pixels from a trackpad, notches from a wheel; Shift turns the wheel sideways.
    QPointF delta = !event->pixelDelta().isNull() ? QPointF(event->pixelDelta())
                                                  : QPointF(event->angleDelta()) / 120.0 * 40.0;
    if (event->modifiers() & Qt::ShiftModifier && delta.x() == 0.0) {
        delta = QPointF(delta.y(), 0.0);
    }
    m_scroll -= delta;
    viewChanged();
    event->accept();
}

bool SpriteEditorWidget::event(QEvent* event)
{
    if (event->type() == QEvent::NativeGesture) {
        const auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            setZoom(m_zoom * (1.0 + gesture->value()), gesture->position().toPoint());
            return true;
        }
    }
    return QWidget::event(event);
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
    // In the lower half, at its own origin.
    return RegionLayout{
        render::CanvasRegion::SpriteOriginal,
        render::Rect{0, 0, kSpriteSize.width, kSpriteSize.height},
    };
}

render::Resolution SpriteEditorWidget::colorizedPaintResolution() const
{
    if (!m_document || !m_spriteIndex || *m_spriteIndex >= m_document->spriteCount()) {
        return render::Resolution::Sd;
    }
    const document::SpriteRecord& sprite = m_document->sprite(*m_spriteIndex);
    return isHdOnly() || (m_activeResolution == render::Resolution::Hd && sprite.hasHd)
               ? render::Resolution::Hd
               : render::Resolution::Sd;
}

bool SpriteEditorWidget::isHdOnly() const
{
    return m_document && m_document->rendersHdPlaneOnly();
}

int SpriteEditorWidget::hdScale() const
{
    // Only asked for an HD sprite, which a project without an editable extra plane never has.
    return m_document && m_document->geometry().isEditable(render::Resolution::Hd)
               ? m_document->geometry().planeScale(render::Resolution::Hd)
               : 2;
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
    if (!viewport(false).contains(position)) {
        return std::nullopt;
    }
    const QPointF origin = displayOrigin(false);
    const RegionLayout colorized = colorizedRegion();
    const double logicalX = (position.x() - origin.x()) / scale;
    const double logicalY = (position.y() - origin.y()) / scale;
    if (logicalX < colorized.rect.x || logicalY < colorized.rect.y ||
        logicalX >= colorized.rect.x + colorized.rect.width ||
        logicalY >= colorized.rect.y + colorized.rect.height) {
        return std::nullopt;
    }
    const render::Resolution resolution = colorizedPaintResolution();
    if (isHdOnly() && !m_document->sprite(*m_spriteIndex).hasHd) {
        return std::nullopt;
    }
    const double localX = logicalX - colorized.rect.x;
    const double localY = logicalY - colorized.rect.y;
    const double sourceScale = resolution == render::Resolution::Hd ? hdScale() : 1;
    const int sourceX = static_cast<int>(std::floor(localX * sourceScale));
    const int sourceY = static_cast<int>(std::floor(localY * sourceScale));
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
    if (!viewport(true).contains(position)) {
        return std::nullopt;
    }
    const QPointF origin = displayOrigin(true);
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
    painter.drawText(rect(), Qt::AlignCenter, tr("No sprite selected"));
}

void SpriteEditorWidget::setDynamicPaintMode(bool enabled)
{
    if (m_dynamicPaint == enabled) {
        return;
    }
    m_dynamicPaint = enabled;
    update();
}

bool SpriteEditorWidget::isDynamicPaintMode() const
{
    return m_dynamicPaint;
}

void SpriteEditorWidget::setTransparentPaintMode(bool enabled)
{
    if (m_transparentPaint == enabled) {
        return;
    }
    m_transparentPaint = enabled;
    update();
}

bool SpriteEditorWidget::isTransparentPaintMode() const
{
    return m_transparentPaint;
}

void SpriteEditorWidget::paintSpriteRegion(QPainter& painter, const RegionLayout& region)
{
    painter.fillRect(QRect(region.rect.x, region.rect.y, region.rect.width, region.rect.height),
                     QColor(0, 0, 0));

    const document::SpriteRecord& sprite = m_document->sprite(*m_spriteIndex);
    const bool useHd = colorizedPaintResolution() == render::Resolution::Hd && sprite.hasHd;
    if (region.region == render::CanvasRegion::SpriteColorized && isHdOnly() && !useHd) {
        // Its SD colors are never shown; there is no HD sprite to show yet.
        return;
    }
    const document::IndexedFrame& mask =
        region.region == render::CanvasRegion::SpriteOriginal
            ? sprite.originalSd
            : useHd ? sprite.maskHd : sprite.originalSd;
    const document::Rgb565Frame& colorized = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    const bool drawHdColorized =
        region.region == render::CanvasRegion::SpriteColorized && useHd;
    const double cellSize = drawHdColorized ? 1.0 / hdScale() : 1.0;
    for (int y = 0; y < kSpriteSize.height; ++y) {
        for (int x = 0; x < kSpriteSize.width; ++x) {
            const std::size_t index = spritePixelIndex(x, y);
            if (mask.pixels[index] == 255) {
                continue;
            }
            const QColor color =
                region.region == render::CanvasRegion::SpriteOriginal
                    ? originalIndexToColor(mask.pixels[index], m_document->colorCount())
                    : rgb565ToColor(colorized.pixels[index]);
            paintDmdPixel(painter,
                          region.rect.x + x * cellSize,
                          region.rect.y + y * cellSize,
                          cellSize,
                          color,
                          m_renderOptions);
        }
    }
    // The HD pixels made transparent over the original: a stroke with Shift covers them again.
    if (m_transparentPaint && drawHdColorized) {
        for (int y = 0; y < kSpriteSize.height; ++y) {
            for (int x = 0; x < kSpriteSize.width; ++x) {
                const std::size_t index = spritePixelIndex(x, y);
                const std::optional<uint8_t> original =
                    m_document->spriteOriginalUnderHd(*m_spriteIndex, render::Point{x, y});
                if (mask.pixels[index] != 255 || !original || *original == 255) {
                    continue;
                }
                painter.fillRect(QRectF(region.rect.x + x * cellSize,
                                        region.rect.y + y * cellSize,
                                        cellSize,
                                        cellSize),
                                 QBrush(QColor(255, 0, 255, 110), Qt::Dense5Pattern));
            }
        }
    }
    // The dynamic zones, over the picture of the plane they are on.
    const render::Resolution shownPlane = useHd ? render::Resolution::Hd : render::Resolution::Sd;
    if (m_dynamicPaint && region.region == render::CanvasRegion::SpriteColorized &&
        shownPlane == m_document->dynamicPlane()) {
        const document::IndexedFrame& dynamicMask =
            useHd ? sprite.dynamicMaskHd : sprite.dynamicMaskSd;
        for (int y = 0; y < kSpriteSize.height; ++y) {
            for (int x = 0; x < kSpriteSize.width; ++x) {
                const std::size_t index = spritePixelIndex(x, y);
                if (mask.pixels[index] == 255 || index >= dynamicMask.pixels.size() ||
                    dynamicMask.pixels[index] == 255) {
                    continue;
                }
                painter.fillRect(QRectF(region.rect.x + x * cellSize,
                                        region.rect.y + y * cellSize,
                                        cellSize,
                                        cellSize),
                                 QColor(0, 180, 255, 150));
            }
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
    if (m_transparentPaint) {
        previewColor = m_paintErase ? QColor(255, 255, 255, 150) : QColor(255, 0, 255, 150);
    }
    // In dynamic paint mode the stroke paints dynamic zones, not colors.
    const edit::PixelColorSource replaced =
        m_colorSetReplace && !m_paintErase && !m_dynamicPaint && !m_transparentPaint
            ? edit::spriteColorSetReplaceColorSource(*m_document, *m_spriteIndex, resolution)
            : edit::PixelColorSource();

    painter.save();
    for (const render::Point point : pixelsForColorizedStroke(*m_colorizedStrokeStart,
                                                              *m_colorizedStrokeEnd)) {
        if (!isColorizedPixelEditable(sprite, resolution, point, m_paintErase)) {
            continue;
        }
        const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
        const double cellSize = useHd ? 1.0 / hdScale() : 1.0;
        QColor color = previewColor;
        if (replaced) {
            color = rgb565ToColor(replaced(point));
            color.setAlpha(150);
        }
        paintDmdPixel(painter,
                      region.rect.x + point.x * cellSize,
                      region.rect.y + point.y * cellSize,
                      cellSize,
                      color,
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
    if (isHdOnly() && !useHd) {
        return false;
    }
    const document::IndexedFrame& mask = useHd ? sprite.maskHd : sprite.originalSd;
    const document::Rgb565Frame& colorized = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    const std::size_t index = spritePixelIndex(point.x, point.y);
    if (m_transparentPaint) {
        if (!useHd) {
            return false;
        }
        // Erase (Shift) covers a transparent pixel again where the original is.
        const std::optional<uint8_t> original =
            m_document->spriteOriginalUnderHd(*m_spriteIndex, point);
        return erase ? mask.pixels[index] == 255 && original && *original != 255
                     : mask.pixels[index] != 255;
    }
    if (mask.pixels[index] == 255) {
        return false;
    }
    if (m_dynamicPaint) {
        return (useHd ? render::Resolution::Hd : render::Resolution::Sd) ==
               m_document->dynamicPlane();
    }
    return erase || colorized.pixels[index] != 0;
}

std::vector<render::Point> SpriteEditorWidget::pixelsForColorizedStroke(render::Point start,
                                                                        render::Point end) const
{
    // Alt (Option on a Mac): shapes from their centre, as on the frame canvas.
    const bool circle = m_drawingTool == SpriteDrawingTool::Circle ||
                        m_drawingTool == SpriteDrawingTool::FilledCircle;
    const bool shape = circle || m_drawingTool == SpriteDrawingTool::Rectangle ||
                       m_drawingTool == SpriteDrawingTool::FilledRectangle ||
                       m_drawingTool == SpriteDrawingTool::Ellipse ||
                       m_drawingTool == SpriteDrawingTool::FilledEllipse;
    if (shape && QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier)) {
        std::tie(start, end) = edit::centredShapeCorners(start, end, circle);
    }
    if (m_drawingTool == SpriteDrawingTool::Line) {
        return edit::makeLinePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::Rectangle) {
        return edit::makeRectanglePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::FilledRectangle) {
        return edit::makeFilledRectanglePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::Circle) {
        return edit::makeEllipsePixels(start, edit::squareConstrainedEnd(start, end));
    }
    if (m_drawingTool == SpriteDrawingTool::FilledCircle) {
        return edit::makeFilledEllipsePixels(start, edit::squareConstrainedEnd(start, end));
    }
    if (m_drawingTool == SpriteDrawingTool::Ellipse) {
        return edit::makeEllipsePixels(start, end);
    }
    if (m_drawingTool == SpriteDrawingTool::FilledEllipse) {
        return edit::makeFilledEllipsePixels(start, end);
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
