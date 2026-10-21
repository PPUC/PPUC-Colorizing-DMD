#include "FramePictureView.h"

#include <QPainter>

#include <algorithm>

namespace colorizing::app {

FramePictureView::FramePictureView(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(256, 64);
}

void FramePictureView::setFrame(render::Size frameSize, Renderer renderer)
{
    m_frameSize = frameSize;
    m_renderer = std::move(renderer);
    m_image = QImage();
    m_imageScale = 0;
    update();
}

void FramePictureView::clear()
{
    setFrame(render::Size{0, 0}, nullptr);
}

void FramePictureView::setLayoutSize(render::Size layoutSize)
{
    m_layoutSize = layoutSize;
    m_image = QImage();
    m_imageScale = 0;
    update();
}

int FramePictureView::pixelScale() const
{
    if (!m_renderer || m_frameSize.width <= 0 || m_frameSize.height <= 0) {
        return 0;
    }
    const bool onGrid = m_layoutSize.width >= m_frameSize.width &&
                        m_layoutSize.height >= m_frameSize.height &&
                        m_layoutSize.width % m_frameSize.width == 0 &&
                        m_layoutSize.height % m_frameSize.height == 0 &&
                        m_layoutSize.width / m_frameSize.width ==
                            m_layoutSize.height / m_frameSize.height;
    const render::Size grid = onGrid ? m_layoutSize : m_frameSize;
    const int cells = std::max(1, std::min(width() / grid.width, height() / grid.height));
    return cells * (grid.width / m_frameSize.width);
}

QSize FramePictureView::sizeHint() const
{
    // 128 columns at 4, 256 at 2: the size the pictures had before they grew.
    return QSize(512, 128);
}

void FramePictureView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    const int scale = pixelScale();
    if (scale == 0) {
        return;
    }
    if (scale != m_imageScale || m_image.isNull()) {
        m_image = m_renderer(scale);
        m_imageScale = scale;
    }
    painter.drawImage(QPoint((width() - m_image.width()) / 2, (height() - m_image.height()) / 2),
                      m_image);
}

}  // namespace colorizing::app
