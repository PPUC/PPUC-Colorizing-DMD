#pragma once

#include <utility>
#include <vector>

#include "render/CanvasTypes.h"

namespace colorizing::edit {

[[nodiscard]] std::vector<render::Point> makeLinePixels(render::Point start,
                                                        render::Point end);
[[nodiscard]] std::vector<render::Point> makeRectanglePixels(render::Point start,
                                                             render::Point end);
[[nodiscard]] std::vector<render::Point> makeFilledRectanglePixels(render::Point start,
                                                                   render::Point end);
[[nodiscard]] std::vector<render::Point> makeEllipsePixels(render::Point start,
                                                           render::Point end);
[[nodiscard]] std::vector<render::Point> makeFilledEllipsePixels(render::Point start,
                                                                 render::Point end);
[[nodiscard]] render::Point squareConstrainedEnd(render::Point start,
                                                 render::Point end);
[[nodiscard]] std::vector<render::Point> makeCirclePixels(render::Point start,
                                                          render::Point end);
[[nodiscard]] std::vector<render::Point> makeFilledCirclePixels(render::Point start,
                                                               render::Point end);

enum class DrawingTool {
    Point,
    Line,
    Rectangle,
    FilledRectangle,
    Circle,
    FilledCircle,
    Ellipse,
    FilledEllipse,
    PaletteGradient,
    Selection,
    ColorPicker,
    FloodFill,
};

// The filled area the PaletteGradient and Selection tools drag out.
enum class PaletteGradientShape {
    Rectangle,
    Circle,
    Ellipse,
};

// Whether the tool (with gradientShape for the PaletteGradient and Selection tools) draws a
// shape within the rectangle of the drag: rectangles, circles and ellipses.
[[nodiscard]] bool isBoundedShape(DrawingTool tool, PaletteGradientShape gradientShape);
// The drag's start and end for a shape drawn from its centre (where the drag started) out to
// end, as with Alt in Photoshop: the start mirrored through the centre; with square (circles)
// a square of the smaller extent, as squareConstrainedEnd() makes one.
[[nodiscard]] std::pair<render::Point, render::Point> centredShapeCorners(render::Point centre,
                                                                          render::Point end,
                                                                          bool square);
// Whether the bounded shape is a circle, which centredShapeCorners() makes square.
[[nodiscard]] bool isCircleShape(DrawingTool tool, PaletteGradientShape gradientShape);

// The pixels a drag from start to end covers with tool. Point, ColorPicker and FloodFill are
// click tools and give none.
[[nodiscard]] std::vector<render::Point> makeStrokePixels(DrawingTool tool,
                                                          PaletteGradientShape gradientShape,
                                                          render::Point start,
                                                          render::Point end);

}  // namespace colorizing::edit
