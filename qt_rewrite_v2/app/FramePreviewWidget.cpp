#include "FramePreviewWidget.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include "FrameImageUtils.h"
#include "PlaneLabels.h"
#include "RuntimeView.h"

namespace colorizing::app {

FramePreviewWidget::FramePreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 220);
    setFocusPolicy(Qt::StrongFocus);
    m_horizontalScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_verticalScrollBar = new QScrollBar(Qt::Vertical, this);
    for (QScrollBar* bar : {m_horizontalScrollBar, m_verticalScrollBar}) {
        bar->hide();
        bar->setFocusPolicy(Qt::NoFocus);
        connect(bar, &QScrollBar::valueChanged, this, [this]() {
            if (m_updatingScrollBars || m_zoom <= 1.0) {
                return;
            }
            // The scroll bars count the zoomed picture in steps of 1/10000 of a region.
            const double total = 10000.0 * m_zoom;
            m_viewOffset = QPointF(m_horizontalScrollBar->value() / total,
                                   m_verticalScrollBar->value() / total);
            viewChanged();
        });
    }
}

double FramePreviewWidget::zoom() const
{
    return m_zoom;
}

void FramePreviewWidget::setZoom(double zoom, std::optional<QPoint> anchor)
{
    zoom = std::clamp(zoom, 1.0, kMaxZoom);
    if (zoom == m_zoom) {
        return;
    }
    const std::optional<RegionView> before = firstRegionView();
    if (!before) {
        m_zoom = zoom;
        viewChanged();
        return;
    }
    // The point of the frame under the anchor (or the middle of the half) stays there.
    QPointF point = before->viewport.center();
    if (anchor) {
        for (const RegionView& view : regionViews()) {
            if (view.viewport.contains(*anchor)) {
                point = *anchor;
                const QPointF shift = point - view.viewport.topLeft();
                point = before->viewport.topLeft() + shift;
            }
        }
    }
    const QPointF fraction((point.x() - before->origin.x()) / before->contentSize.width(),
                           (point.y() - before->origin.y()) / before->contentSize.height());
    const double factor = zoom / m_zoom;
    m_zoom = zoom;
    const QSizeF content = before->contentSize * factor;
    m_viewOffset = QPointF(fraction.x() - (point.x() - before->viewport.left()) / content.width(),
                           fraction.y() - (point.y() - before->viewport.top()) / content.height());
    viewChanged();
}

void FramePreviewWidget::zoomIn()
{
    setZoom(m_zoom * 1.25);
}

void FramePreviewWidget::zoomOut()
{
    setZoom(m_zoom / 1.25);
}

void FramePreviewWidget::resetZoom()
{
    m_zoom = 1.0;
    m_viewOffset = QPointF();
    viewChanged();
}

QPointF FramePreviewWidget::viewOffset() const
{
    return m_viewOffset;
}

void FramePreviewWidget::setViewOffset(QPointF offset)
{
    m_viewOffset = offset;
    viewChanged();
}

QSizeF FramePreviewWidget::shownFraction() const
{
    const std::optional<RegionView> view = firstRegionView();
    if (!view) {
        return QSizeF(1.0, 1.0);
    }
    return QSizeF(std::min(1.0, view->viewport.width() / view->contentSize.width()),
                  std::min(1.0, view->viewport.height() / view->contentSize.height()));
}

void FramePreviewWidget::viewChanged()
{
    const QSizeF shown = shownFraction();
    m_viewOffset = QPointF(std::clamp(m_viewOffset.x(), 0.0, 1.0 - shown.width()),
                           std::clamp(m_viewOffset.y(), 0.0, 1.0 - shown.height()));
    updateScrollBars();
    update();
    if (onViewChanged) {
        onViewChanged();
    }
}

void FramePreviewWidget::updateScrollBars()
{
    // In steps of 1/10000 of the frame; a page is the part that is shown.
    const QSizeF shown = shownFraction();
    m_updatingScrollBars = true;
    const auto set = [](QScrollBar* bar, double fraction, double offset) {
        const int page = static_cast<int>(std::lround(fraction * 10000.0));
        bar->setVisible(fraction < 1.0);
        bar->setRange(0, 10000 - page);
        bar->setPageStep(page);
        bar->setSingleStep(std::max(1, page / 8));
        bar->setValue(static_cast<int>(std::lround(offset * 10000.0)));
    };
    set(m_horizontalScrollBar, shown.width(), m_viewOffset.x());
    set(m_verticalScrollBar, shown.height(), m_viewOffset.y());
    m_updatingScrollBars = false;
}

void FramePreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const int thickness = m_horizontalScrollBar->sizeHint().height();
    m_horizontalScrollBar->setGeometry(0, height() - thickness, width() - thickness, thickness);
    m_verticalScrollBar->setGeometry(width() - thickness, 0, thickness, height() - thickness);
    viewChanged();
}

void FramePreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        // 120 is one notch of a wheel: 1.25 times.
        const double notches = event->angleDelta().y() / 120.0;
        if (notches != 0.0) {
            setZoom(m_zoom * std::pow(1.25, notches), event->position().toPoint());
        }
        event->accept();
        return;
    }
    const std::optional<RegionView> view = firstRegionView();
    const QSizeF shown = shownFraction();
    if (!view || (shown.width() >= 1.0 && shown.height() >= 1.0)) {
        QWidget::wheelEvent(event);
        return;
    }
    // A trackpad gives pixels; a wheel notches (120 each), a notch moving a tenth of the half.
    // Shift turns a plain wheel sideways.
    QPointF pixels = !event->pixelDelta().isNull()
                         ? QPointF(event->pixelDelta())
                         : QPointF(event->angleDelta().x() / 120.0 * view->viewport.width() / 10.0,
                                   event->angleDelta().y() / 120.0 * view->viewport.height() /
                                       10.0);
    if (event->modifiers().testFlag(Qt::ShiftModifier) && event->pixelDelta().isNull()) {
        pixels = QPointF(pixels.y(), pixels.x());
    }
    m_viewOffset -= QPointF(pixels.x() / view->contentSize.width(),
                            pixels.y() / view->contentSize.height());
    viewChanged();
    event->accept();
}

bool FramePreviewWidget::event(QEvent* event)
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

std::vector<FramePreviewWidget::RegionView> FramePreviewWidget::regionViews() const
{
    std::vector<RegionView> views;
    if (!m_document || !m_hasFrame || m_frameIndex >= m_document->frameCount()) {
        return views;
    }
    const render::CanvasLayout layout = currentLayout();
    const std::vector<render::CanvasRegionLayout>& regions = layout.regions();
    if (regions.empty()) {
        return views;
    }
    // The canvas split into one half per region, one above the other, leaving room for the
    // Runtime view's line at the top and the scroll bars at the right and the bottom.
    const int thickness = m_horizontalScrollBar->sizeHint().height();
    const QRectF inner = QRectF(rect()).adjusted(8, 22, -(8 + thickness), -(8 + thickness));
    constexpr double gap = 8.0;
    const double count = static_cast<double>(regions.size());
    const double height = std::max(1.0, (inner.height() - gap * (count - 1.0)) / count);
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const render::CanvasRegionLayout& region = regions[index];
        RegionView view;
        view.region = region;
        view.viewport = QRectF(inner.left(), inner.top() + static_cast<double>(index) * (height + gap),
                               std::max(1.0, inner.width()), height);
        // The whole frame fits its half at zoom 1, in whole screen pixels per display unit where
        // there is room for one.
        const double fit = std::min(view.viewport.width() / region.displayRect.width,
                                    view.viewport.height() / region.displayRect.height);
        view.cell = (fit >= 1.0 ? std::floor(fit) : fit) * m_zoom;
        view.contentSize = QSizeF(region.displayRect.width * view.cell,
                                  region.displayRect.height * view.cell);
        // Centred while it fits, otherwise moved by the shown part.
        const auto start = [](double viewportStart, double viewportSize, double contentSize,
                              double offset) {
            return contentSize <= viewportSize
                       ? viewportStart + (viewportSize - contentSize) / 2.0
                       : viewportStart - offset * contentSize;
        };
        view.origin = QPointF(
            std::round(start(view.viewport.left(), view.viewport.width(),
                             view.contentSize.width(), m_viewOffset.x())),
            std::round(start(view.viewport.top(), view.viewport.height(),
                             view.contentSize.height(), m_viewOffset.y())));
        views.push_back(view);
    }
    return views;
}

std::optional<FramePreviewWidget::RegionView> FramePreviewWidget::firstRegionView() const
{
    const std::vector<RegionView> views = regionViews();
    return views.empty() ? std::nullopt : std::optional<RegionView>(views.front());
}

void FramePreviewWidget::setDocument(const document::ProjectDocument* document)
{
    m_document = document;
    m_hasFrame = false;
    update();
}

void FramePreviewWidget::setRuntimeView(RuntimeView* runtimeView)
{
    m_runtimeView = runtimeView;
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

void FramePreviewWidget::setComparisonMaskOverlayEnabled(bool enabled)
{
    if (m_showComparisonMaskOverlay == enabled) {
        return;
    }
    m_showComparisonMaskOverlay = enabled;
    update();
}

void FramePreviewWidget::setDifferenceOverlay(std::optional<document::IndexedFrame> differingPixels)
{
    if (!m_differenceOverlay && !differingPixels) {
        return;
    }
    m_differenceOverlay = std::move(differingPixels);
    update();
}

void FramePreviewWidget::setDynamicMaskOverlayEnabled(bool enabled)
{
    if (m_showDynamicMaskOverlay == enabled) {
        return;
    }
    m_showDynamicMaskOverlay = enabled;
    update();
}

void FramePreviewWidget::setBackgroundMaskOverlayEnabled(bool enabled)
{
    if (m_showBackgroundMaskOverlay == enabled) {
        return;
    }
    m_showBackgroundMaskOverlay = enabled;
    update();
}

void FramePreviewWidget::setBackgroundLayerEnabled(bool enabled)
{
    if (m_showBackgroundLayer == enabled) {
        return;
    }
    m_showBackgroundLayer = enabled;
    update();
}

void FramePreviewWidget::setBackgroundImageEditModeEnabled(bool enabled)
{
    if (m_showBackgroundImageEditMode == enabled) {
        return;
    }
    m_showBackgroundImageEditMode = enabled;
    update();
}

void FramePreviewWidget::setSelectedBackgroundIndex(std::optional<std::size_t> backgroundIndex)
{
    if (m_selectedBackgroundIndex == backgroundIndex) {
        return;
    }
    m_selectedBackgroundIndex = backgroundIndex;
    update();
}

void FramePreviewWidget::setDmdRenderOptions(DmdRenderOptions options)
{
    if (m_renderOptions.pixelShape == options.pixelShape &&
        std::abs(m_renderOptions.pixelCoverage - options.pixelCoverage) < 0.001) {
        return;
    }
    m_renderOptions = options;
    update();
}

void FramePreviewWidget::setColorRotationPreviewElapsedMs(std::optional<int> elapsedMs)
{
    if (m_colorRotationPreviewElapsedMs == elapsedMs) {
        return;
    }
    m_colorRotationPreviewElapsedMs = elapsedMs;
    update();
}

void FramePreviewWidget::setSelectedSpriteBoundsOverlayEnabled(bool enabled)
{
    if (m_showSelectedSpriteBoundsOverlay == enabled) {
        return;
    }
    m_showSelectedSpriteBoundsOverlay = enabled;
    update();
}

void FramePreviewWidget::setSelectedSpriteReference(
    std::optional<document::FrameSpriteReference> reference)
{
    const auto sameReference =
        [](const std::optional<document::FrameSpriteReference>& lhs,
           const std::optional<document::FrameSpriteReference>& rhs) {
            if (lhs.has_value() != rhs.has_value()) {
                return false;
            }
            if (!lhs) {
                return true;
            }
            return lhs->spriteId == rhs->spriteId &&
                   lhs->minX == rhs->minX &&
                   lhs->minY == rhs->minY &&
                   lhs->maxX == rhs->maxX &&
                   lhs->maxY == rhs->maxY;
        };
    if (sameReference(m_selectedSpriteReference, reference)) {
        return;
    }
    m_selectedSpriteReference = reference;
    update();
}

void FramePreviewWidget::setSelectionOverlayPixels(render::CanvasRegion region,
                                                   render::Resolution resolution,
                                                   std::vector<render::Point> pixels,
                                                   QColor color)
{
    m_selectionOverlayRegion = region;
    m_selectionOverlayResolution = resolution;
    m_selectionOverlayPixels = std::move(pixels);
    m_selectionOverlayColor = color;
    update();
}

void FramePreviewWidget::clearSelectionOverlayPixels()
{
    if (!m_selectionOverlayResolution && m_selectionOverlayPixels.empty()) {
        return;
    }
    m_selectionOverlayResolution.reset();
    m_selectionOverlayPixels.clear();
    update();
}

void FramePreviewWidget::setPixelClickedHandler(std::function<void(render::CanvasHit, bool)> handler)
{
    m_pixelClickedHandler = std::move(handler);
}

void FramePreviewWidget::setOriginalClickedHandler(
    std::function<void(render::CanvasHit, bool)> handler)
{
    m_originalClickedHandler = std::move(handler);
}

void FramePreviewWidget::setStrokePreviewHandler(
    std::function<void(render::CanvasHit, render::CanvasHit, bool)> handler)
{
    m_strokePreviewHandler = std::move(handler);
}

void FramePreviewWidget::setStrokeFinishedHandler(
    std::function<void(render::CanvasHit, render::CanvasHit, bool)> handler)
{
    m_strokeFinishedHandler = std::move(handler);
}

void FramePreviewWidget::setPreviewPixels(render::CanvasRegion region,
                                          render::Resolution resolution,
                                          std::vector<render::Point> pixels,
                                          QColor color)
{
    m_previewRegion = region;
    m_previewResolution = resolution;
    m_previewPixels = std::move(pixels);
    m_previewColor = color;
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

    // The half under the point, then the pixel of its frame there.
    const render::CanvasLayout layout = currentLayout();
    std::optional<render::CanvasHit> hit;
    for (const RegionView& view : regionViews()) {
        if (!view.viewport.contains(position)) {
            continue;
        }
        const render::Rect& rect = view.region.displayRect;
        const double x = (position.x() - view.origin.x()) / view.cell;
        const double y = (position.y() - view.origin.y()) / view.cell;
        // Half open: the right and bottom edges belong to what is beside the frame.
        if (x >= 0.0 && y >= 0.0 && x < rect.width && y < rect.height) {
            hit = layout.hitTest(render::Point{rect.x + static_cast<int>(std::floor(x)),
                                               rect.y + static_cast<int>(std::floor(y))});
        }
        break;
    }
    if (hit && hit->region == render::CanvasRegion::ColorizedFrame && isHdFrameMissing() &&
        !m_showBackgroundImageEditMode) {
        return std::nullopt;
    }
    return hit;
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
    if (showsRuntimeView()) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        m_pressModifiers = event->modifiers();
        const std::optional<render::CanvasHit> hit = hitTestAt(event->pos());
        if (hit && hit->region == render::CanvasRegion::ColorizedFrame && m_pressInterceptor &&
            m_pressInterceptor(*hit, event->modifiers())) {
            m_strokeStart.reset();
            event->accept();
            return;
        }
        if (hit && hit->region == render::CanvasRegion::ColorizedFrame) {
            m_strokeStart = *hit;
            m_strokeErase = event->modifiers().testFlag(Qt::ShiftModifier);
            clearPreviewPixels();
            if (m_pixelClickedHandler) {
                m_pixelClickedHandler(*hit, m_strokeErase);
            }
            event->accept();
            return;
        }
        if (hit && hit->region == render::CanvasRegion::OriginalFrame && m_originalClickedHandler) {
            m_strokeStart = *hit;
            m_strokeErase = event->modifiers().testFlag(Qt::ShiftModifier);
            clearPreviewPixels();
            m_originalClickedHandler(*hit, m_strokeErase);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void FramePreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (showsRuntimeView()) {
        // A stroke that was under way when the view was switched on is dropped.
        m_strokeStart.reset();
    }
    if (event->buttons() == Qt::NoButton && m_hoverHandler && !showsRuntimeView()) {
        const std::optional<render::CanvasHit> hit = hitTestAt(event->pos());
        if (hit && hit->region == render::CanvasRegion::ColorizedFrame) {
            m_hoverHandler(*hit);
            event->accept();
            return;
        }
    }
    if ((event->buttons() & Qt::LeftButton) && m_strokeStart && m_strokePreviewHandler) {
        const std::optional<render::CanvasHit> hit = hitTestAt(event->pos());
        if (hit && hit->region == m_strokeStart->region &&
            hit->resolution == m_strokeStart->resolution) {
            m_strokePreviewHandler(*m_strokeStart, *hit, m_strokeErase);
            event->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

void FramePreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (showsRuntimeView()) {
        m_strokeStart.reset();
    }
    if (event->button() == Qt::LeftButton && m_strokeStart) {
        const std::optional<render::CanvasHit> hit = hitTestAt(event->pos());
        const std::optional<render::CanvasHit> start = m_strokeStart;
        const bool erase = m_strokeErase;
        m_strokeStart.reset();
        m_strokeErase = false;
        clearPreviewPixels();
        if (hit && hit->region == start->region &&
            start->resolution == hit->resolution && m_strokeFinishedHandler) {
            m_strokeFinishedHandler(*start, *hit, erase);
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

bool FramePreviewWidget::showsRuntimeView() const
{
    return m_runtimeView && m_runtimeView->isEnabled() && m_document && m_hasFrame &&
           m_frameIndex < m_document->frameCount();
}

render::CanvasLayout FramePreviewWidget::currentLayout() const
{
    const bool hasHd = m_document && m_hasFrame && m_frameIndex < m_document->frameCount() &&
                       m_document->hasHdFrame(m_frameIndex);
    // An HD-only project has no SD plane to fall back to.
    const bool hdOnly = m_document && m_document->rendersHdPlaneOnly();
    return render::CanvasLayout::buildFrameLayout({
        // libserum's 64-row plane exists for a frame without HD too.
        showsRuntimeView() ? m_runtimeView->layoutPlane(m_document->geometry())
        : hdOnly           ? render::Resolution::Hd
        : hasHd            ? m_activeResolution
                           : render::Resolution::Sd,
        true,
        false,
        hasHd,
        8,
        m_document ? m_document->geometry() : render::ProjectGeometry(),
    });
}

bool FramePreviewWidget::isHdFrameMissing() const
{
    return m_document && m_hasFrame && m_frameIndex < m_document->frameCount() &&
           m_document->rendersHdPlaneOnly() && !m_document->hasHdFrame(m_frameIndex);
}

void FramePreviewWidget::paintEmpty(QPainter& painter)
{
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No frame selected"));
}

void FramePreviewWidget::paintFrame(QPainter& painter)
{
    const render::CanvasLayout layout = currentLayout();
    const bool runtimeView = showsRuntimeView();
    std::optional<QRectF> colorizedPicture;
    for (const RegionView& view : regionViews()) {
        // The frame in its half, at the zoom, the part outside the half cut off.
        const render::CanvasRegionLayout& region = view.region;
        const render::Rect& rect = region.displayRect;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing,
                              m_renderOptions.pixelShape == DmdPixelShape::Dot);
        painter.setClipRect(view.viewport);
        painter.translate(view.origin);
        painter.scale(view.cell, view.cell);
        painter.translate(-rect.x, -rect.y);
        paintRegion(painter, region);
        if (!runtimeView) {
            paintComparisonMaskOverlay(painter, region);
            paintDifferenceOverlay(painter, region);
            paintDynamicMaskOverlay(painter, region);
            paintBackgroundMaskOverlay(painter, region);
            paintSelectedSpriteBounds(painter, region);
            paintSelectionOverlay(painter, region);
            paintPreview(painter, region);
            paintPasteOverlay(painter, region);
        }
        painter.restore();
        if (region.region == render::CanvasRegion::ColorizedFrame) {
            colorizedPicture = QRectF(view.origin, view.contentSize).intersected(view.viewport);
        }
    }

    if (runtimeView) {
        if (colorizedPicture) {
            paintRuntimeViewTexts(painter, *colorizedPicture);
        }
        return;
    }
    if (colorizedPicture && isHdFrameMissing() && !m_showBackgroundImageEditMode) {
        painter.setPen(QColor(255, 120, 120));
        painter.drawText(*colorizedPicture,
                         Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("No %1 frame: the runtime shows nothing for this "
                                        "frame.\nUse Create %1 Frame.")
                             .arg(nativePlaneText(*m_document)));
    }
}

// Above the picture, never over it: what is shown and that it cannot be edited; and in place
// of the picture, why libserum has none.
void FramePreviewWidget::paintRuntimeViewTexts(QPainter& painter, const QRectF& pictureRect)
{
    const int rows =
        m_runtimeView->rowsFor(m_document->geometry()) == runtime::PlaneRequest::Rows64 ? 64 : 32;
    // Which frame a machine identifies the ROM frame as, where that is another one.
    const QString hint =
        QString::fromStdString(m_runtimeView->identificationHint(*m_document, m_frameIndex));
    painter.setPen(QColor(120, 200, 255));
    painter.drawText(QRectF(0, std::max(0.0, pictureRect.top() - 18.0), width(), 16.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("Runtime view: libserum output, %1 rows%2. Editing is off.%3")
                         .arg(rows)
                         .arg(m_runtimeView->isPlaying() ? QStringLiteral(", playing")
                                                         : QString())
                         .arg(hint.isEmpty() ? QString()
                                             : QStringLiteral(" Note: %1.").arg(hint)));
    const QString message =
        QString::fromStdString(m_runtimeView->message(*m_document, m_frameIndex));
    if (!message.isEmpty()) {
        painter.setPen(QColor(255, 120, 120));
        painter.drawText(pictureRect, Qt::AlignCenter | Qt::TextWordWrap, message);
    }
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
    std::optional<document::Rgb565Frame> composedColorized;
    const bool drawBackgroundImage =
        !drawOriginal && m_showBackgroundImageEditMode;

    if (drawOriginal) {
        original = &m_document->originalFrame(m_frameIndex);
    } else if (showsRuntimeView()) {
        colorized = m_runtimeView->image(*m_document, m_frameIndex);
        if (colorized && colorized->size != region.logicalSize) {
            colorized = nullptr;
        }
    } else {
        if (drawBackgroundImage &&
            m_selectedBackgroundIndex &&
            *m_selectedBackgroundIndex < m_document->backgroundCount()) {
            try {
                colorized = &m_document->backgroundFrame(*m_selectedBackgroundIndex,
                                                         region.resolution);
            } catch (const std::exception&) {
                colorized = nullptr;
            }
        } else {
            // Dynamic content and its shadows stand still while the rest rotates.
            std::vector<uint8_t> notRotating;
            composedColorized =
                composeResolvedFrame(*m_document,
                                     m_frameIndex,
                                     region.resolution,
                                     m_showBackgroundLayer,
                                     m_colorRotationPreviewElapsedMs ? &notRotating : nullptr);
            if (m_colorRotationPreviewElapsedMs) {
                applyColorRotations(*m_document,
                                    m_frameIndex,
                                    region.resolution,
                                    *m_colorRotationPreviewElapsedMs,
                                    *composedColorized,
                                    &notRotating);
            }
            colorized = &*composedColorized;
        }
    }

    const int scale = region.displayScale;
    for (int y = 0; y < region.logicalSize.height; ++y) {
        for (int x = 0; x < region.logicalSize.width; ++x) {
            QColor color;
            if (original) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(original->size.width) +
                    static_cast<std::size_t>(x);
                color = originalIndexToColor(original->pixels[index], m_document->colorCount());
            } else if (colorized) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(colorized->size.width) +
                    static_cast<std::size_t>(x);
                color = rgb565ToColor(colorized->pixels[index]);
            } else {
                color = QColor(0, 0, 0);
            }

            paintDmdPixel(painter,
                          region.displayRect.x + x * scale,
                          region.displayRect.y + y * scale,
                          scale,
                          color,
                          m_renderOptions);
        }
    }

}

void FramePreviewWidget::paintComparisonMaskOverlay(
    QPainter& painter,
    const render::CanvasRegionLayout& region)
{
    if (!m_showComparisonMaskOverlay ||
        region.region != render::CanvasRegion::OriginalFrame) {
        return;
    }

    const uint8_t maskId = m_document->comparisonMaskId(m_frameIndex);
    if (maskId >= m_document->comparisonMaskCount()) {
        return;
    }

    const document::IndexedFrame& mask = m_document->comparisonMask(maskId);
    const int scale = region.displayScale;
    const QColor overlayColor(255, 0, 0, 180);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
    for (int y = 0; y < mask.size.height; ++y) {
        for (int x = 0; x < mask.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.size.width) +
                static_cast<std::size_t>(x);
            if (mask.pixels[index] == 0) {
                continue;
            }
            painter.fillRect(QRect(region.displayRect.x + x * scale,
                                   region.displayRect.y + y * scale,
                                   scale,
                                   scale),
                             overlayColor);
        }
    }
    painter.restore();
}

void FramePreviewWidget::paintDifferenceOverlay(QPainter& painter,
                                                const render::CanvasRegionLayout& region)
{
    if (!m_differenceOverlay || region.region != render::CanvasRegion::OriginalFrame ||
        m_differenceOverlay->size != m_document->originalFrameSize()) {
        return;
    }
    const document::IndexedFrame& differing = *m_differenceOverlay;
    const int scale = region.displayScale;
    const QColor overlayColor(255, 210, 0, 190);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
    for (int y = 0; y < differing.size.height; ++y) {
        for (int x = 0; x < differing.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(differing.size.width) +
                static_cast<std::size_t>(x);
            if (differing.pixels[index] == 0) {
                continue;
            }
            painter.fillRect(QRect(region.displayRect.x + x * scale,
                                   region.displayRect.y + y * scale,
                                   scale,
                                   scale),
                             overlayColor);
        }
    }
    painter.restore();
}

void FramePreviewWidget::paintDynamicMaskOverlay(
    QPainter& painter,
    const render::CanvasRegionLayout& region)
{
    // The zones are those of one plane; they are not drawn over the other one.
    if (!m_showDynamicMaskOverlay ||
        region.region != render::CanvasRegion::ColorizedFrame ||
        region.resolution != m_document->dynamicPlane()) {
        return;
    }

    const std::optional<document::FrameDynamicData>& dynamicData =
        m_document->dynamicData(m_frameIndex);
    if (!dynamicData) {
        return;
    }

    // The pixels show the colors of their zone's set, as the runtime renders them; the zones
    // are outlined, also where two zones of different sets meet.
    const document::IndexedFrame& mask =
        region.resolution == render::Resolution::Hd ? dynamicData->maskHd : dynamicData->maskSd;
    const int scale = region.displayScale;
    const int width = mask.size.width;
    const int height = mask.size.height;
    const auto setAt = [&mask, width, height](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return 255;
        }
        return mask.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x)];
    };
    QPen pen(QColor(0, 200, 255), 2);
    pen.setCosmetic(true);
    painter.save();
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int set = setAt(x, y);
            if (set == 255) {
                continue;
            }
            const int left = region.displayRect.x + x * scale;
            const int top = region.displayRect.y + y * scale;
            if (setAt(x - 1, y) != set) {
                painter.drawLine(QPointF(left, top), QPointF(left, top + scale));
            }
            if (setAt(x + 1, y) != set) {
                painter.drawLine(QPointF(left + scale, top), QPointF(left + scale, top + scale));
            }
            if (setAt(x, y - 1) != set) {
                painter.drawLine(QPointF(left, top), QPointF(left + scale, top));
            }
            if (setAt(x, y + 1) != set) {
                painter.drawLine(QPointF(left, top + scale), QPointF(left + scale, top + scale));
            }
        }
    }
    painter.restore();
}

void FramePreviewWidget::paintBackgroundMaskOverlay(
    QPainter& painter,
    const render::CanvasRegionLayout& region)
{
    if (!m_showBackgroundMaskOverlay ||
        region.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }

    const std::optional<document::FrameBackgroundData>& backgroundData =
        m_document->backgroundData(m_frameIndex);
    if (!backgroundData || backgroundData->backgroundId >= m_document->backgroundCount()) {
        return;
    }

    const document::IndexedFrame& mask =
        region.resolution == render::Resolution::Hd ? backgroundData->maskHd : backgroundData->maskSd;
    const int scale = region.displayScale;
    const QColor overlayColor(255, 200, 0, 150);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
    for (int y = 0; y < mask.size.height; ++y) {
        for (int x = 0; x < mask.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.size.width) +
                static_cast<std::size_t>(x);
            if (mask.pixels[index] == 0) {
                continue;
            }
            painter.fillRect(QRect(region.displayRect.x + x * scale,
                                   region.displayRect.y + y * scale,
                                   scale,
                                   scale),
                             overlayColor);
        }
    }
    painter.restore();
}

void FramePreviewWidget::paintSelectedSpriteBounds(
    QPainter& painter,
    const render::CanvasRegionLayout& region)
{
    if (!m_showSelectedSpriteBoundsOverlay ||
        !m_selectedSpriteReference ||
        region.region != render::CanvasRegion::OriginalFrame) {
        return;
    }

    // Slot bounds are pixels of the original the region shows.
    const render::Size size = region.logicalSize;
    const document::FrameSpriteReference& reference = *m_selectedSpriteReference;
    const int minX = std::clamp(static_cast<int>(std::min(reference.minX, reference.maxX)),
                                0,
                                size.width - 1);
    const int maxX = std::clamp(static_cast<int>(std::max(reference.minX, reference.maxX)),
                                0,
                                size.width - 1);
    const int minY = std::clamp(static_cast<int>(std::min(reference.minY, reference.maxY)),
                                0,
                                size.height - 1);
    const int maxY = std::clamp(static_cast<int>(std::max(reference.minY, reference.maxY)),
                                0,
                                size.height - 1);

    const int scale = region.displayScale;
    const QRect bounds(region.displayRect.x + minX * scale,
                       region.displayRect.y + minY * scale,
                       (maxX - minX + 1) * scale,
                       (maxY - minY + 1) * scale);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor fillColor(0, 255, 255, 110);
    painter.fillRect(bounds, fillColor);
    painter.restore();
}

void FramePreviewWidget::paintSelectionOverlay(QPainter& painter,
                                               const render::CanvasRegionLayout& region)
{
    if (!m_selectionOverlayResolution ||
        m_selectionOverlayRegion != region.region ||
        *m_selectionOverlayResolution != region.resolution ||
        m_selectionOverlayPixels.empty()) {
        return;
    }

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_selectionOverlayColor);
    const int scale = region.displayScale;
    for (const render::Point& point : m_selectionOverlayPixels) {
        if (point.x < 0 || point.y < 0 ||
            point.x >= region.logicalSize.width ||
            point.y >= region.logicalSize.height) {
            continue;
        }
        painter.drawRect(QRect(region.displayRect.x + point.x * scale,
                               region.displayRect.y + point.y * scale,
                               scale,
                               scale));
    }
    painter.restore();
}

void FramePreviewWidget::setPasteOverlay(std::optional<CanvasPasteOverlay> overlay)
{
    m_pasteOverlay = std::move(overlay);
    update();
}

void FramePreviewWidget::setPressInterceptor(
    std::function<bool(render::CanvasHit, Qt::KeyboardModifiers)> handler)
{
    m_pressInterceptor = std::move(handler);
}

void FramePreviewWidget::setHoverHandler(std::function<void(render::CanvasHit)> handler)
{
    m_hoverHandler = std::move(handler);
    setMouseTracking(static_cast<bool>(m_hoverHandler));
}

Qt::KeyboardModifiers FramePreviewWidget::pressModifiers() const
{
    return m_pressModifiers;
}

void FramePreviewWidget::paintPasteOverlay(QPainter& painter,
                                           const render::CanvasRegionLayout& region)
{
    if (!m_pasteOverlay || region.region != render::CanvasRegion::ColorizedFrame ||
        region.resolution != m_pasteOverlay->resolution) {
        return;
    }
    const int scale = region.displayScale;
    painter.save();
    painter.setClipRect(QRectF(region.displayRect.x,
                               region.displayRect.y,
                               region.displayRect.width,
                               region.displayRect.height));
    for (const auto& [pixel, color] : m_pasteOverlay->pixels) {
        QColor shown = color;
        shown.setAlpha(200);
        paintDmdPixel(painter,
                      region.displayRect.x + pixel.x * scale,
                      region.displayRect.y + pixel.y * scale,
                      scale,
                      shown,
                      m_renderOptions);
    }
    const render::Rect& box = m_pasteOverlay->box;
    painter.setPen(QPen(QColor(255, 255, 255, 220), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(region.displayRect.x + box.x * scale,
                            region.displayRect.y + box.y * scale,
                            box.width * scale - 1,
                            box.height * scale - 1));
    painter.restore();
}

void FramePreviewWidget::paintPreview(QPainter& painter, const render::CanvasRegionLayout& region)
{
    if (!m_previewResolution || m_previewPixels.empty() ||
        region.region != m_previewRegion ||
        region.resolution != *m_previewResolution) {
        return;
    }

    const int scale = region.displayScale;
    painter.save();
    QColor previewColor = m_previewColor;
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
    for (const render::Point& pixel : m_previewPixels) {
        if (pixel.x < 0 || pixel.y < 0 ||
            pixel.x >= region.logicalSize.width || pixel.y >= region.logicalSize.height) {
            continue;
        }
        paintDmdPixel(painter,
                      region.displayRect.x + pixel.x * scale,
                      region.displayRect.y + pixel.y * scale,
                      scale,
                      previewColor,
                      m_renderOptions);
    }
    painter.restore();
}

}  // namespace colorizing::app
