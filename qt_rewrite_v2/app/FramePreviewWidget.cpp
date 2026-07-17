#include "FramePreviewWidget.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include "FrameImageUtils.h"

namespace colorizing::app {

FramePreviewWidget::FramePreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 220);
}

void FramePreviewWidget::setDocument(const document::ProjectDocument* document)
{
    m_document = document;
    m_hasFrame = false;
    update();
}

void FramePreviewWidget::setFrameIndex(std::size_t frameIndex)
{
    m_frameIndex = frameIndex;
    m_hasFrame = true;
    update();
}

void FramePreviewWidget::setActiveResolution(render::Resolution resolution)
{
    m_activeResolution = resolution;
    update();
}

void FramePreviewWidget::setPixelClickedHandler(std::function<void(render::CanvasHit)> handler)
{
    m_pixelClickedHandler = std::move(handler);
}

void FramePreviewWidget::setStrokePreviewHandler(
    std::function<void(render::CanvasHit, render::CanvasHit)> handler)
{
    m_strokePreviewHandler = std::move(handler);
}

void FramePreviewWidget::setStrokeFinishedHandler(
    std::function<void(render::CanvasHit, render::CanvasHit)> handler)
{
    m_strokeFinishedHandler = std::move(handler);
}

void FramePreviewWidget::setPreviewPixels(render::Resolution resolution,
                                          std::vector<render::Point> pixels,
                                          uint16_t rgb565)
{
    m_previewResolution = resolution;
    m_previewPixels = std::move(pixels);
    m_previewColor = rgb565;
    update();
}

void FramePreviewWidget::clearPreviewPixels()
{
    if (!m_previewResolution && m_previewPixels.empty()) {
        return;
    }
    m_previewResolution.reset();
    m_previewPixels.clear();
    update();
}

QSize FramePreviewWidget::sizeHint() const
{
    return QSize(620, 360);
}

std::optional<render::CanvasHit> FramePreviewWidget::hitTestAt(QPoint position) const
{
    if (!m_document || !m_hasFrame || m_frameIndex >= m_document->frameCount()) {
        return std::nullopt;
    }

    const render::CanvasLayout layout = currentLayout();
    const double scale = currentDisplayScale(layout);
    if (scale <= 0.0) {
        return std::nullopt;
    }

    const QPointF origin = currentDisplayOrigin(layout);
    const int layoutX = static_cast<int>(std::floor((position.x() - origin.x()) / scale));
    const int layoutY = static_cast<int>(std::floor((position.y() - origin.y()) / scale));
    return layout.hitTest(render::Point{layoutX, layoutY});
}

void FramePreviewWidget::paintEvent(QPaintEvent* event)
{
    (void)event;
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    if (!m_document || !m_hasFrame || m_frameIndex >= m_document->frameCount()) {
        paintEmpty(painter);
        return;
    }

    paintFrame(painter);
}

void FramePreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const std::optional<render::CanvasHit> hit = hitTestAt(event->pos());
        if (hit && hit->region == render::CanvasRegion::ColorizedFrame) {
            m_strokeStart = *hit;
            clearPreviewPixels();
            if (m_pixelClickedHandler) {
                m_pixelClickedHandler(*hit);
            }
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void FramePreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && m_strokeStart && m_strokePreviewHandler) {
        const std::optional<render::CanvasHit> hit = hitTestAt(event->pos());
        if (hit && hit->region == render::CanvasRegion::ColorizedFrame &&
            hit->resolution == m_strokeStart->resolution) {
            m_strokePreviewHandler(*m_strokeStart, *hit);
            event->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

void FramePreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_strokeStart) {
        const std::optional<render::CanvasHit> hit = hitTestAt(event->pos());
        const std::optional<render::CanvasHit> start = m_strokeStart;
        m_strokeStart.reset();
        clearPreviewPixels();
        if (hit && hit->region == render::CanvasRegion::ColorizedFrame &&
            start->resolution == hit->resolution && m_strokeFinishedHandler) {
            m_strokeFinishedHandler(*start, *hit);
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

render::CanvasLayout FramePreviewWidget::currentLayout() const
{
    const bool hasHd = m_document && m_hasFrame && m_frameIndex < m_document->frameCount() &&
                       m_document->hasHdFrame(m_frameIndex);
    return render::CanvasLayout::buildFrameLayout({
        hasHd ? m_activeResolution : render::Resolution::Sd,
        true,
        false,
        hasHd,
        8,
    });
}

double FramePreviewWidget::currentDisplayScale(const render::CanvasLayout& layout) const
{
    const QSize available = size();
    const render::Size displaySize = layout.displaySize();
    if (displaySize.width <= 0 || displaySize.height <= 0) {
        return 1.0;
    }
    return std::max(1.0, std::min(
        static_cast<double>(std::max(1, available.width() - 24)) /
            static_cast<double>(displaySize.width),
        static_cast<double>(std::max(1, available.height() - 36)) /
            static_cast<double>(displaySize.height)));
}

QPointF FramePreviewWidget::currentDisplayOrigin(const render::CanvasLayout& layout) const
{
    const double scale = currentDisplayScale(layout);
    const render::Size displaySize = layout.displaySize();
    return QPointF((width() - displaySize.width * scale) / 2.0,
                   (height() - displaySize.height * scale) / 2.0);
}

void FramePreviewWidget::paintEmpty(QPainter& painter)
{
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No frame selected"));
}

void FramePreviewWidget::paintFrame(QPainter& painter)
{
    const render::CanvasLayout layout = currentLayout();
    const double scale = currentDisplayScale(layout);
    const QPointF origin = currentDisplayOrigin(layout);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.translate(origin);
    painter.scale(scale, scale);

    for (const render::CanvasRegionLayout& region : layout.regions()) {
        paintRegion(painter, region);
        paintPreview(painter, region);
    }

    painter.restore();
}

void FramePreviewWidget::paintRegion(QPainter& painter, const render::CanvasRegionLayout& region)
{
    painter.fillRect(QRect(region.displayRect.x,
                           region.displayRect.y,
                           region.displayRect.width,
                           region.displayRect.height),
                     QColor(0, 0, 0));

    const bool drawOriginal = region.region == render::CanvasRegion::OriginalFrame;
    const document::IndexedFrame* original = nullptr;
    const document::Rgb565Frame* colorized = nullptr;

    if (drawOriginal) {
        original = &m_document->originalFrame(m_frameIndex);
    } else {
        colorized = &m_document->colorizedFrame(m_frameIndex, region.resolution);
    }

    const int scale = region.displayScale;
    for (int y = 0; y < region.logicalSize.height; ++y) {
        for (int x = 0; x < region.logicalSize.width; ++x) {
            QColor color;
            if (original) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(original->size.width) +
                    static_cast<std::size_t>(x);
                color = originalIndexToColor(original->pixels[index]);
            } else {
                const std::size_t index =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(colorized->size.width) +
                    static_cast<std::size_t>(x);
                color = rgb565ToColor(colorized->pixels[index]);
            }

            painter.fillRect(QRect(region.displayRect.x + x * scale,
                                   region.displayRect.y + y * scale,
                                   scale,
                                   scale),
                             color);
        }
    }

}

void FramePreviewWidget::paintPreview(QPainter& painter, const render::CanvasRegionLayout& region)
{
    if (!m_previewResolution || m_previewPixels.empty() ||
        region.region != render::CanvasRegion::ColorizedFrame ||
        region.resolution != *m_previewResolution) {
        return;
    }

    const int scale = region.displayScale;
    painter.save();
    QColor previewColor = rgb565ToColor(m_previewColor);
    previewColor.setAlpha(150);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
    for (const render::Point& pixel : m_previewPixels) {
        if (pixel.x < 0 || pixel.y < 0 ||
            pixel.x >= region.logicalSize.width || pixel.y >= region.logicalSize.height) {
            continue;
        }
        painter.fillRect(QRect(region.displayRect.x + pixel.x * scale,
                               region.displayRect.y + pixel.y * scale,
                               scale,
                               scale),
                         previewColor);
    }
    painter.restore();
}

}  // namespace colorizing::app
