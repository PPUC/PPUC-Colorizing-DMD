#pragma once

#include <optional>
#include <string>

#include "render/CanvasTypes.h"

namespace colorizing::render {

// The plane sizes of the ordinary layout. Other code asks the project's ProjectGeometry.
constexpr Size kSdFrameSize{128, 32};
constexpr Size kHdFrameSize{256, 64};

// How the two stored planes of a project relate to the frame the ROM delivers.
//
//   layout          ROM frame  original plane (Sd)   extra plane (Hd)       cROM header
//   Ordinary        128x32     128x32, native        256x64, twice          128x32 + 256x64
//   NativeExtra     256x64     128x32, carried       256x64, native         128x32 + 256x64, flag
//   NativeOriginal  192x64     192x64, native        96x32, carried         192x64 + 96x32
//
// Resolution names a stored plane, not a size: Sd is the original plane (cFrames, DynaMasks,
// ...), Hd the extra plane (the ...X arrays). The native plane has the ROM frame's size and
// lines up with it pixel for pixel. A carried plane is smaller than the ROM frame: its data
// is loaded and written back unchanged and never shown, edited, created or removed.
//
// Original frames, comparison masks, sprite originals, detection areas and the bounds of a
// frame's sprite slots are ROM frame pixels in every layout. A sprite found at a ROM frame
// position is drawn on a plane at planeScale() times that position and size.
enum class PlaneLayout {
    Ordinary,
    NativeExtra,
    NativeOriginal,
};

class ProjectGeometry {
public:
    // The ordinary layout.
    ProjectGeometry() = default;
    explicit ProjectGeometry(PlaneLayout layout);

    // The layout a cROM header describes: fWidth x fHeight, fWidthX x fHeightX and the
    // revision 20 "256x64 original" flag. Nothing for a combination that is not in the table
    // above; errorMessage then says which sizes are supported.
    [[nodiscard]] static std::optional<ProjectGeometry> fromStoredPlanes(
        Size originalPlane,
        Size extraPlane,
        bool original256x64Flag,
        std::string* errorMessage = nullptr);

    [[nodiscard]] PlaneLayout layout() const;
    // The header flag of the NativeExtra layout.
    [[nodiscard]] bool original256x64Flag() const;

    // Stored size of a plane, carried or not.
    [[nodiscard]] Size planeSize(Resolution plane) const;
    [[nodiscard]] Size romFrameSize() const;
    [[nodiscard]] Resolution nativePlane() const;
    // False for a carried plane.
    [[nodiscard]] bool isEditable(Resolution plane) const;
    // Plane pixels per ROM frame pixel along one axis: 1 or 2. Throws std::logic_error for a
    // carried plane, as toRomFrame() does.
    [[nodiscard]] int planeScale(Resolution plane) const;
    // The ROM frame pixel a pixel of the plane shows.
    [[nodiscard]] Point toRomFrame(Resolution plane, Point planePixel) const;

    // Display pixels per plane pixel at canvas zoom 1. A 64-row plane is shown one to one and
    // a 32-row plane doubled, so that both planes of the ordinary layout are equally high.
    [[nodiscard]] int planeDisplayScale(Resolution plane) const;
    [[nodiscard]] int romFrameDisplayScale() const;

private:
    PlaneLayout m_layout = PlaneLayout::Ordinary;
};

[[nodiscard]] bool operator==(const ProjectGeometry& lhs, const ProjectGeometry& rhs);
[[nodiscard]] bool operator!=(const ProjectGeometry& lhs, const ProjectGeometry& rhs);

}  // namespace colorizing::render
