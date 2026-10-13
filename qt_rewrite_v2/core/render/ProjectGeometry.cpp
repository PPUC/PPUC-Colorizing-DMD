#include "render/ProjectGeometry.h"

#include <stdexcept>

namespace colorizing::render {

namespace {

struct LayoutData {
    PlaneLayout layout;
    Size originalPlane;
    Size extraPlane;
    Resolution nativePlane;
    bool originalEditable;
    bool extraEditable;
    bool original256x64Flag;
};

constexpr LayoutData kLayouts[] = {
    {PlaneLayout::Ordinary, {128, 32}, {256, 64}, Resolution::Sd, true, true, false},
    {PlaneLayout::NativeExtra, {128, 32}, {256, 64}, Resolution::Hd, false, true, true},
    {PlaneLayout::NativeOriginal, {192, 64}, {96, 32}, Resolution::Sd, true, false, false},
};

const LayoutData& dataFor(PlaneLayout layout)
{
    for (const LayoutData& data : kLayouts) {
        if (data.layout == layout) {
            return data;
        }
    }
    throw std::invalid_argument("unknown plane layout");
}

std::string sizeText(Size size)
{
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

// Rounds down, so that a coordinate left of or above the frame stays outside.
int floorDivide(int value, int divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

}  // namespace

ProjectGeometry::ProjectGeometry(PlaneLayout layout)
    : m_layout(dataFor(layout).layout)
{
}

std::optional<ProjectGeometry> ProjectGeometry::fromStoredPlanes(Size originalPlane,
                                                                 Size extraPlane,
                                                                 bool original256x64Flag,
                                                                 std::string* errorMessage)
{
    for (const LayoutData& data : kLayouts) {
        if (data.originalPlane == originalPlane && data.extraPlane == extraPlane &&
            data.original256x64Flag == original256x64Flag) {
            return ProjectGeometry(data.layout);
        }
    }
    if (errorMessage) {
        *errorMessage = "unsupported frame geometry " + sizeText(originalPlane) +
                        " with an extra plane of " + sizeText(extraPlane) +
                        (original256x64Flag ? " and 256x64 original frames" : "") +
                        ": supported are 128x32 with 256x64 (also with 256x64 original "
                        "frames) and 192x64 with 96x32";
    }
    return std::nullopt;
}

PlaneLayout ProjectGeometry::layout() const
{
    return m_layout;
}

bool ProjectGeometry::original256x64Flag() const
{
    return dataFor(m_layout).original256x64Flag;
}

Size ProjectGeometry::planeSize(Resolution plane) const
{
    const LayoutData& data = dataFor(m_layout);
    return plane == Resolution::Hd ? data.extraPlane : data.originalPlane;
}

Size ProjectGeometry::romFrameSize() const
{
    return planeSize(nativePlane());
}

Resolution ProjectGeometry::nativePlane() const
{
    return dataFor(m_layout).nativePlane;
}

bool ProjectGeometry::isEditable(Resolution plane) const
{
    const LayoutData& data = dataFor(m_layout);
    return plane == Resolution::Hd ? data.extraEditable : data.originalEditable;
}

int ProjectGeometry::planeScale(Resolution plane) const
{
    if (!isEditable(plane)) {
        throw std::logic_error("a plane smaller than the ROM frame is carried, not rendered");
    }
    return planeSize(plane).width / romFrameSize().width;
}

Point ProjectGeometry::toRomFrame(Resolution plane, Point planePixel) const
{
    const int scale = planeScale(plane);
    return Point{floorDivide(planePixel.x, scale), floorDivide(planePixel.y, scale)};
}

int ProjectGeometry::planeDisplayScale(Resolution plane) const
{
    return kHdFrameSize.height / planeSize(plane).height;
}

int ProjectGeometry::romFrameDisplayScale() const
{
    return planeDisplayScale(nativePlane());
}

bool operator==(const ProjectGeometry& lhs, const ProjectGeometry& rhs)
{
    return lhs.layout() == rhs.layout();
}

bool operator!=(const ProjectGeometry& lhs, const ProjectGeometry& rhs)
{
    return !(lhs == rhs);
}

}  // namespace colorizing::render
