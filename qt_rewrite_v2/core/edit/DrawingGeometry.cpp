#include "edit/DrawingGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace colorizing::edit {

std::vector<render::Point> makeLinePixels(render::Point start, render::Point end)
{
    std::vector<render::Point> pixels;
    int x0 = start.x;
    int y0 = start.y;
    const int x1 = end.x;
    const int y1 = end.y;
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        pixels.push_back(render::Point{x0, y0});
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
    return pixels;
}

std::vector<render::Point> makeRectanglePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    std::set<std::pair<int, int>> uniquePixels;

    for (int x = left; x <= right; ++x) {
        uniquePixels.insert({x, top});
        uniquePixels.insert({x, bottom});
    }
    for (int y = top; y <= bottom; ++y) {
        uniquePixels.insert({left, y});
        uniquePixels.insert({right, y});
    }

    std::vector<render::Point> pixels;
    pixels.reserve(uniquePixels.size());
    for (const auto& [x, y] : uniquePixels) {
        pixels.push_back(render::Point{x, y});
    }
    return pixels;
}

std::vector<render::Point> makeFilledRectanglePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    std::vector<render::Point> pixels;
    pixels.reserve(static_cast<std::size_t>(right - left + 1) *
                   static_cast<std::size_t>(bottom - top + 1));

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            pixels.push_back(render::Point{x, y});
        }
    }
    return pixels;
}

std::vector<render::Point> makeEllipsePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    const double centerX = (static_cast<double>(left) + static_cast<double>(right)) / 2.0;
    const double centerY = (static_cast<double>(top) + static_cast<double>(bottom)) / 2.0;
    const double radiusX = std::max(0.5, (static_cast<double>(right - left) + 1.0) / 2.0);
    const double radiusY = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) / 2.0);
    auto insideEllipse = [&](int x, int y) {
        const double normalizedX = (static_cast<double>(x) - centerX) / radiusX;
        const double normalizedY = (static_cast<double>(y) - centerY) / radiusY;
        return normalizedX * normalizedX + normalizedY * normalizedY <= 1.0;
    };
    std::set<std::pair<int, int>> uniquePixels;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (insideEllipse(x, y) &&
                (!insideEllipse(x - 1, y) || !insideEllipse(x + 1, y) ||
                 !insideEllipse(x, y - 1) || !insideEllipse(x, y + 1))) {
                uniquePixels.insert({x, y});
            }
        }
    }

    std::vector<render::Point> pixels;
    pixels.reserve(uniquePixels.size());
    for (const auto& [x, y] : uniquePixels) {
        pixels.push_back(render::Point{x, y});
    }
    return pixels;
}

std::vector<render::Point> makeFilledEllipsePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    const double centerX = (static_cast<double>(left) + static_cast<double>(right)) / 2.0;
    const double centerY = (static_cast<double>(top) + static_cast<double>(bottom)) / 2.0;
    const double radiusX = std::max(0.5, (static_cast<double>(right - left) + 1.0) / 2.0);
    const double radiusY = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) / 2.0);
    std::vector<render::Point> pixels;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const double normalizedX = (static_cast<double>(x) - centerX) / radiusX;
            const double normalizedY = (static_cast<double>(y) - centerY) / radiusY;
            if (normalizedX * normalizedX + normalizedY * normalizedY <= 1.0) {
                pixels.push_back(render::Point{x, y});
            }
        }
    }
    return pixels;
}

render::Point squareConstrainedEnd(render::Point start, render::Point end)
{
    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    const int side = std::min(std::abs(dx), std::abs(dy));
    const int xSign = dx < 0 ? -1 : 1;
    const int ySign = dy < 0 ? -1 : 1;
    return render::Point{start.x + xSign * side, start.y + ySign * side};
}

std::vector<render::Point> makeCirclePixels(render::Point start, render::Point end)
{
    return makeEllipsePixels(start, squareConstrainedEnd(start, end));
}

std::vector<render::Point> makeFilledCirclePixels(render::Point start, render::Point end)
{
    return makeFilledEllipsePixels(start, squareConstrainedEnd(start, end));
}

}  // namespace colorizing::edit
