#pragma once

#include <vector>

#include "render/CanvasTypes.h"

namespace colorizing::app {

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

}  // namespace colorizing::app
