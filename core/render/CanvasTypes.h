#pragma once

#include <cstdint>

namespace colorizing::render {

// A stored plane of a project, not a size: Sd is the original plane, Hd the extra plane. What
// size each has and which one is the ROM frame's is in render::ProjectGeometry.
enum class Resolution {
    Sd,
    Hd
};

enum class CanvasRegion {
    ColorizedFrame,
    OriginalFrame,
    HdFrame,
    SpriteColorized,
    SpriteOriginal,
    Background
};

struct Size {
    int width = 0;
    int height = 0;
};

[[nodiscard]] constexpr bool operator==(Size lhs, Size rhs)
{
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

[[nodiscard]] constexpr bool operator!=(Size lhs, Size rhs)
{
    return !(lhs == rhs);
}

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool contains(Point point) const;
};

struct CanvasHit {
    CanvasRegion region = CanvasRegion::ColorizedFrame;
    // The plane whose grid x and y are on. On the original region that is the native plane
    // (ProjectGeometry::nativePlane()), not the plane being edited.
    Resolution resolution = Resolution::Sd;
    int x = 0;
    int y = 0;
};

}  // namespace colorizing::render

