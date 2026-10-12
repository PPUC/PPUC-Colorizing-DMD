#include "render/OriginalFrameMapping.h"

#include <stdexcept>

namespace colorizing::render {

namespace {

bool sameSize(Size lhs, Size rhs)
{
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

// Rounds down, so that a coordinate left of or above the frame stays outside.
int floorDivide(int value, int divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

}  // namespace

OriginalFrameMapping::OriginalFrameMapping(Size originalSize)
    : m_originalSize(originalSize)
{
    if (!isSupportedOriginalSize(originalSize)) {
        throw std::invalid_argument("unsupported original frame size");
    }
}

bool OriginalFrameMapping::isSupportedOriginalSize(Size size)
{
    return sameSize(size, kSdFrameSize) || sameSize(size, kHdFrameSize);
}

Size OriginalFrameMapping::originalSize() const
{
    return m_originalSize;
}

Resolution OriginalFrameMapping::originalResolution() const
{
    return sameSize(m_originalSize, kHdFrameSize) ? Resolution::Hd : Resolution::Sd;
}

int OriginalFrameMapping::originalDisplayScale() const
{
    return kSdFrameSize.width * displayScaleForResolution(Resolution::Sd) / m_originalSize.width;
}

int OriginalFrameMapping::step(Resolution resolution) const
{
    const int frameWidth = logicalSizeForResolution(resolution).width;
    return m_originalSize.width >= frameWidth ? m_originalSize.width / frameWidth
                                              : -(frameWidth / m_originalSize.width);
}

Point OriginalFrameMapping::toOriginal(Resolution resolution, Point framePixel) const
{
    const int ratio = step(resolution);
    return ratio > 0 ? Point{framePixel.x * ratio, framePixel.y * ratio}
                     : Point{floorDivide(framePixel.x, -ratio),
                             floorDivide(framePixel.y, -ratio)};
}

Point OriginalFrameMapping::fromOriginal(Resolution resolution, Point originalPixel) const
{
    const int ratio = step(resolution);
    return ratio > 0 ? Point{floorDivide(originalPixel.x, ratio),
                             floorDivide(originalPixel.y, ratio)}
                     : Point{originalPixel.x * -ratio, originalPixel.y * -ratio};
}

Rect OriginalFrameMapping::originalBlock(Resolution resolution, Point framePixel) const
{
    const Point origin = toOriginal(resolution, framePixel);
    const int size = step(resolution) > 0 ? step(resolution) : 1;
    return Rect{origin.x, origin.y, size, size};
}

Rect OriginalFrameMapping::originalBlocks(Resolution resolution,
                                          Point minPixel,
                                          Point maxPixel) const
{
    const Rect first = originalBlock(resolution, minPixel);
    const Rect last = originalBlock(resolution, maxPixel);
    return Rect{first.x,
                first.y,
                last.x + last.width - first.x,
                last.y + last.height - first.y};
}

}  // namespace colorizing::render
