#include "SpriteEditorWidget.h"

#include <algorithm>
#include <array>
#include <cmath>

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
    paintDetectionAreas(painter, original);
    paintDragPreview(painter, original);
    painter.restore();
}

void SpriteEditorWidget::mousePressEvent(QMouseEvent* event)
{
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
