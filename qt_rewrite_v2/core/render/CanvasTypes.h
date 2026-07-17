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
    Resolution resolution = Resolution::Sd;
    int x = 0;
    int y = 0;
};

}  // namespace colorizing::render

