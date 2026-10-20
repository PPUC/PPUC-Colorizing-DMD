#include "ImageImport.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace colorizing::app {

namespace {

// OpenCV's BORDER_REFLECT_101: -1 is 1, n is n - 2.
int reflect101(int index, int size)
{
    if (size == 1) {
        return 0;
    }
    while (index < 0 || index >= size) {
        index = index < 0 ? -index : 2 * size - 2 - index;
    }
    return index;
}

// A box blur of k values along one direction (cv::blur, anchor in the middle).
void boxBlur(std::vector<float>& values, int width, int height, int k, bool horizontal)
{
    const std::vector<float> source = values;
    const int before = k / 2;
    const int length = horizontal ? width : height;
    for (int line = 0; line < (horizontal ? height : width); ++line) {
        for (int at = 0; at < length; ++at) {
            float sum = 0.0f;
            for (int offset = -before; offset < k - before; ++offset) {
                const int along = reflect101(at + offset, length);
                const int x = horizontal ? along : line;
                const int y = horizontal ? line : along;
                sum += source[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(x)];
            }
            const int x = horizontal ? at : line;
            const int y = horizontal ? line : at;
            values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] = sum / static_cast<float>(k);
        }
    }
}

uint8_t saturated(float value)
{
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

}  // namespace

QImage adjustedImage(const QImage& picture, const ImageAdjustments& adjustments)
{
    QImage source = picture.convertToFormat(QImage::Format_RGB32);
    if (adjustments.isNone() || source.isNull()) {
        return source;
    }
    double alpha = 1.0 + 0.05 * adjustments.contrast;
    if (alpha == 1.0) {
        alpha = 1.001;
    }
    const double beta = 2.5 * adjustments.brightness;
    const int width = source.width();
    const int height = source.height();
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::array<std::vector<float>, 3> channels;
    for (std::vector<float>& channel : channels) {
        channel.resize(count);
    }
    for (int y = 0; y < height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            channels[0][index] = static_cast<float>(alpha * qRed(line[x]) + beta);
            channels[1][index] = static_cast<float>(alpha * qGreen(line[x]) + beta);
            channels[2][index] = static_cast<float>(alpha * qBlue(line[x]) + beta);
        }
    }
    if (adjustments.blur > 1) {
        for (std::vector<float>& channel : channels) {
            boxBlur(channel, width, height, adjustments.blur, true);
            boxBlur(channel, width, height, adjustments.blur, false);
        }
    }
    QImage result(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            line[x] = qRgb(saturated(channels[0][index]), saturated(channels[1][index]),
                           saturated(channels[2][index]));
        }
    }
    return result;
}

QImage zonePicture(const QImage& picture, const QRect& zone, QSize size, ImageScaleFilter filter)
{
    const QRect clipped = zone.intersected(picture.rect());
    if (clipped.isEmpty() || size.isEmpty()) {
        return {};
    }
    const QImage part = picture.copy(clipped).convertToFormat(QImage::Format_RGB32);
    if (filter == ImageScaleFilter::Smooth) {
        return part.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    // OpenCV's nearest: the source pixel at floor(target * source / target size).
    QImage result(size, QImage::Format_RGB32);
    const double scaleX = static_cast<double>(part.width()) / size.width();
    const double scaleY = static_cast<double>(part.height()) / size.height();
    for (int y = 0; y < size.height(); ++y) {
        const int sourceY = std::min(static_cast<int>(std::floor(y * scaleY)), part.height() - 1);
        const auto* from = reinterpret_cast<const QRgb*>(part.constScanLine(sourceY));
        auto* to = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            to[x] = from[std::min(static_cast<int>(std::floor(x * scaleX)), part.width() - 1)];
        }
    }
    return result;
}

QRect zoneAt(QSize picture, double proportions, int width, QPoint centre)
{
    if (picture.isEmpty() || proportions <= 0.0) {
        return {};
    }
    // The widest zone of the proportions inside the picture.
    const int widest =
        static_cast<double>(picture.width()) / picture.height() > proportions
            ? std::max(1, static_cast<int>(std::lround(picture.height() * proportions)))
            : picture.width();
    width = std::clamp(width, std::min(8, widest), widest);
    const int height = std::clamp(static_cast<int>(std::lround(width / proportions)), 1,
                                  picture.height());
    const int x = std::clamp(centre.x() - width / 2, 0, picture.width() - width);
    const int y = std::clamp(centre.y() - height / 2, 0, picture.height() - height);
    return QRect(x, y, width, height);
}

QRect fittingZone(QSize picture, double proportions)
{
    return zoneAt(picture, proportions, picture.width(),
                  QPoint(picture.width() / 2, picture.height() / 2));
}

QRect zoneBetween(const QRect& start, const QRect& end, std::size_t index, std::size_t count)
{
    if (count < 2) {
        return start;
    }
    const double t = static_cast<double>(std::min(index, count - 1)) / (count - 1);
    const auto mix = [t](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * t));
    };
    return QRect(mix(start.x(), end.x()), mix(start.y(), end.y()),
                 std::max(1, mix(start.width(), end.width())),
                 std::max(1, mix(start.height(), end.height())));
}

uint16_t rgb565FromColor(QRgb color)
{
    const auto r = static_cast<uint16_t>((qRed(color) * 31) / 255);
    const auto g = static_cast<uint16_t>((qGreen(color) * 63) / 255);
    const auto b = static_cast<uint16_t>((qBlue(color) * 31) / 255);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

std::vector<edit::PixelChange> importedPixelChanges(const document::Rgb565Frame& plane,
                                                    const QImage& picture,
                                                    const render::Rect& rect,
                                                    const std::vector<uint8_t>& mask)
{
    std::vector<edit::PixelChange> changes;
    if (picture.isNull()) {
        return changes;
    }
    const QImage pixels = picture.convertToFormat(QImage::Format_RGB32);
    const uint16_t* colors = plane.pixels.data();
    const bool masked = mask.size() == plane.pixels.size();
    for (int y = std::max(0, rect.y); y < std::min(plane.size.height, rect.y + rect.height); ++y) {
        const int pictureY = y - rect.y;
        if (pictureY >= pixels.height()) {
            break;
        }
        const auto* line = reinterpret_cast<const QRgb*>(pixels.constScanLine(pictureY));
        for (int x = std::max(0, rect.x); x < std::min(plane.size.width, rect.x + rect.width);
             ++x) {
            const int pictureX = x - rect.x;
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(plane.size.width) +
                static_cast<std::size_t>(x);
            if (pictureX >= pixels.width() || (masked && mask[index] == 0)) {
                continue;
            }
            const uint16_t color = rgb565FromColor(line[pictureX]);
            if (colors[index] != color) {
                changes.push_back(edit::PixelChange{x, y, colors[index], color});
            }
        }
    }
    return changes;
}

PlaneArea areaOnPlane(const PlaneArea& area, render::Size from, render::Size to)
{
    if (from.width == to.width && from.height == to.height) {
        return area;
    }
    PlaneArea result;
    const bool larger = to.width > from.width;
    const int factor = larger ? to.width / std::max(1, from.width)
                              : from.width / std::max(1, to.width);
    if (factor < 1) {
        return area;
    }
    if (larger) {
        result.rect = render::Rect{area.rect.x * factor, area.rect.y * factor,
                                   area.rect.width * factor, area.rect.height * factor};
    } else {
        const int x0 = area.rect.x / factor;
        const int y0 = area.rect.y / factor;
        const int x1 = (area.rect.x + area.rect.width + factor - 1) / factor;
        const int y1 = (area.rect.y + area.rect.height + factor - 1) / factor;
        result.rect = render::Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
    }
    if (area.mask.empty()) {
        return result;
    }
    result.mask.assign(static_cast<std::size_t>(to.width) * static_cast<std::size_t>(to.height), 0);
    for (int y = 0; y < to.height; ++y) {
        for (int x = 0; x < to.width; ++x) {
            bool selected = false;
            if (larger) {
                selected = area.mask[static_cast<std::size_t>(y / factor) * from.width +
                                     static_cast<std::size_t>(x / factor)] != 0;
            } else {
                for (int dy = 0; dy < factor && !selected; ++dy) {
                    for (int dx = 0; dx < factor && !selected; ++dx) {
                        const int fx = x * factor + dx;
                        const int fy = y * factor + dy;
                        selected = fx < from.width && fy < from.height &&
                                   area.mask[static_cast<std::size_t>(fy) * from.width +
                                             static_cast<std::size_t>(fx)] != 0;
                    }
                }
            }
            result.mask[static_cast<std::size_t>(y) * to.width + static_cast<std::size_t>(x)] =
                selected ? 1 : 0;
        }
    }
    return result;
}

}  // namespace colorizing::app
