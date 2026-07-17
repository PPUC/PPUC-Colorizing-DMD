#include "FrameThumbnail.h"

#include <QPainter>
#include <QPixmap>

#include "FrameImageUtils.h"

namespace colorizing::app {

QIcon makeFrameThumbnail(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         const QSize& iconSize)
{
    QPixmap pixmap(iconSize);
    pixmap.fill(QColor(18, 20, 22));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const int margin = 4;
    const int gap = 3;
    const int frameWidth = iconSize.width() - margin * 2;
    const int frameHeight = (iconSize.height() - margin * 2 - gap) / 2;

    const QImage colorized = renderRgb565FrameImage(
        document.colorizedFrame(frameIndex, render::Resolution::Sd),
        1);
    const QImage original = renderIndexedFrameImage(document.originalFrame(frameIndex), 1);

    painter.drawImage(QRect(margin, margin, frameWidth, frameHeight), colorized);
    painter.drawImage(QRect(margin, margin + frameHeight + gap, frameWidth, frameHeight), original);

    painter.setPen(QPen(QColor(210, 210, 210), 1));
    painter.drawRect(QRect(0, 0, iconSize.width(), iconSize.height()).adjusted(0, 0, -1, -1));

    if (document.hasHdFrame(frameIndex)) {
        QRect badge(iconSize.width() - 28, 4, 24, 14);
        painter.fillRect(badge, QColor(35, 95, 160));
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, QStringLiteral("HD"));
    }

    return QIcon(pixmap);
}

}  // namespace colorizing::app

