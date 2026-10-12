#pragma once

#include <cstdint>

namespace colorizing::render {

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
    // The grid x and y are on. On the original region that is the grid of the original frame
    // (Hd for a 256x64 original), not the resolution being edited.
    Resolution resolution = Resolution::Sd;
    int x = 0;
    int y = 0;
};

}  // namespace colorizing::render

