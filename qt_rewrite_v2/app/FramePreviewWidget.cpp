#include "FramePreviewWidget.h"

#include <algorithm>

#include <QPainter>
#include <QPaintEvent>

#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

QString regionTitle(render::CanvasRegion region)
{
    switch (region) {
    case render::CanvasRegion::ColorizedFrame:
        return QStringLiteral("Colorized");
    case render::CanvasRegion::OriginalFrame:
        return QStringLiteral("Original");
    case render::CanvasRegion::HdFrame:
        return QStringLiteral("HD");
    case render::CanvasRegion::SpriteColorized:
        return QStringLiteral("Sprite");
    case render::CanvasRegion::SpriteOriginal:
        return QStringLiteral("Sprite Original");
    case render::CanvasRegion::Background:
        return QStringLiteral("Background");
    }
    return QString();
}

}  // namespace

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

QSize FramePreviewWidget::sizeHint() const
{
    return QSize(620, 360);
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

void FramePreviewWidget::paintEmpty(QPainter& painter)
{
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No frame selected"));
}

void FramePreviewWidget::paintFrame(QPainter& painter)
{
    const bool hasHd = m_document->hasHdFrame(m_frameIndex);
    const render::CanvasLayout layout = render::CanvasLayout::buildFrameLayout({
        hasHd ? m_activeResolution : render::Resolution::Sd,
        true,
        hasHd,
        hasHd,
        8,
    });

    const QSize available = size();
    const render::Size displaySize = layout.displaySize();
    const double scale = std::max(1.0, std::min(
        static_cast<double>(available.width() - 24) / static_cast<double>(displaySize.width),
        static_cast<double>(available.height() - 36) / static_cast<double>(displaySize.height)));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.translate((available.width() - displaySize.width * scale) / 2.0,
                      (available.height() - displaySize.height * scale) / 2.0);
    painter.scale(scale, scale);

    for (const render::CanvasRegionLayout& region : layout.regions()) {
        paintRegion(painter, region);
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

    painter.setPen(QPen(QColor(245, 245, 245), 1));
    painter.drawRect(QRect(region.displayRect.x,
                           region.displayRect.y,
                           region.displayRect.width,
                           region.displayRect.height)
                         .adjusted(0, 0, -1, -1));
    painter.drawText(region.displayRect.x + 4,
                     region.displayRect.y + 12,
                     regionTitle(region.region));
}

}  // namespace colorizing::app
