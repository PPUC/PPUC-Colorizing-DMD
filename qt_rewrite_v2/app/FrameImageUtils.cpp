#include "FrameImageUtils.h"

#include <algorithm>

#include <QPainter>

namespace colorizing::app {

QColor rgb565ToColor(uint16_t value)
{
    const int r5 = (value >> 11) & 0x1f;
    const int g6 = (value >> 5) & 0x3f;
    const int b5 = value & 0x1f;
    return QColor((r5 * 255) / 31, (g6 * 255) / 63, (b5 * 255) / 31);
}

QColor originalIndexToColor(uint8_t value)
{
    switch (value & 0x03) {
    case 0:
        return QColor(22, 18, 12);
    case 1:
        return QColor(110, 54, 8);
    case 2:
        return QColor(210, 112, 16);
    default:
        return QColor(255, 184, 48);
    }
}

QImage renderRgb565FrameImage(const document::Rgb565Frame& frame, int pixelScale)
{
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            painter.fillRect(QRect(x * scale, y * scale, scale, scale),
                             rgb565ToColor(frame.pixels[index]));
        }
    }
    return image;
}

QImage renderIndexedFrameImage(const document::IndexedFrame& frame, int pixelScale)
{
    const int scale = std::max(1, pixelScale);
    QImage image(frame.size.width * scale, frame.size.height * scale, QImage::Format_ARGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            painter.fillRect(QRect(x * scale, y * scale, scale, scale),
                             originalIndexToColor(frame.pixels[index]));
        }
    }
    return image;
}

}  // namespace colorizing::app

