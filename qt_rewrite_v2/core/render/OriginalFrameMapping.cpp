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

bool OriginalFrameMapping::hasPlane(Resolution resolution) const
{
    return logicalSizeForResolution(resolution).width >= m_originalSize.width;
}

int OriginalFrameMapping::planeScale(Resolution resolution) const
{
    if (!hasPlane(resolution)) {
        throw std::logic_error("a project with 256x64 original frames has no SD plane");
    }
    return logicalSizeForResolution(resolution).width / m_originalSize.width;
}

Point OriginalFrameMapping::toOriginal(Resolution resolution, Point planePixel) const
{
    const int scale = planeScale(resolution);
    return Point{floorDivide(planePixel.x, scale), floorDivide(planePixel.y, scale)};
}

}  // namespace colorizing::render
