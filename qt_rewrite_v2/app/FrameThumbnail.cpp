#include "FrameThumbnail.h"

#include <algorithm>

#include <QPainter>
#include <QPixmap>

#include "FrameImageUtils.h"
#include "PlaneLabels.h"

namespace colorizing::app {

namespace {

const QColor kBackdrop(18, 20, 22);

void drawBorder(QPainter& painter, const QSize& iconSize)
{
    painter.setPen(QPen(QColor(210, 210, 210), 1));
    painter.drawRect(QRect(0, 0, iconSize.width(), iconSize.height()).adjusted(0, 0, -1, -1));
}

}  // namespace

QIcon makeFrameThumbnailPlaceholder(const QSize& iconSize)
{
    QPixmap pixmap(iconSize);
    pixmap.fill(kBackdrop);
    QPainter painter(&pixmap);
    drawBorder(painter, iconSize);
    return QIcon(pixmap);
}

QIcon makeFrameThumbnail(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         const QSize& iconSize)
{
    QPixmap pixmap(iconSize);
    pixmap.fill(kBackdrop);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const int margin = 4;
    const int gap = 3;
    const int frameWidth = iconSize.width() - margin * 2;
    const int frameHeight = (iconSize.height() - margin * 2 - gap) / 2;

    const QImage colorized = renderResolvedFrameImage(document,
                                                      frameIndex,
                                                      document.geometry().nativePlane(),
                                                      1,
                                                      true);
    const QImage original = renderIndexedFrameImage(document.originalFrame(frameIndex), 1);

    // The two boxes are filled by a 4:1 frame; another shape keeps its proportions in them.
    const render::Size frameSize = document.originalFrameSize();
    const int shownWidth = std::min(frameWidth, frameWidth * frameSize.width /
                                                    (frameSize.height * 4));
    const int shownHeight = std::min(frameHeight, frameHeight * frameSize.height * 4 /
                                                      frameSize.width);
    const int left = margin + (frameWidth - shownWidth) / 2;
    const int inset = (frameHeight - shownHeight) / 2;
    painter.drawImage(QRect(left, margin + inset, shownWidth, shownHeight), colorized);
    painter.drawImage(QRect(left, margin + frameHeight + gap + inset, shownWidth, shownHeight),
                      original);

    drawBorder(painter, iconSize);

    if (document.frameHdGaps(frameIndex).any()) {
        // The runtime shows nothing for this frame, see ProjectDocument::frameHdGaps().
        const int badgeWidth = painter.fontMetrics().horizontalAdvance(kNoPictureText) + 10;
        QRect badge(iconSize.width() - 4 - badgeWidth, 4, badgeWidth, 14);
        painter.fillRect(badge, QColor(190, 45, 45));
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, kNoPictureText);
    } else if (hasHdAsExtraResolution(&document) && document.hasHdFrame(frameIndex)) {
        QRect badge(iconSize.width() - 28, 4, 24, 14);
        painter.fillRect(badge, QColor(35, 95, 160));
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, QStringLiteral("HD"));
    }

    return QIcon(pixmap);
}

}  // namespace colorizing::app
