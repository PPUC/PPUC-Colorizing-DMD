#include "edit/DrawingTools.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <set>
#include <stdexcept>
#include <utility>

namespace colorizing::edit {

namespace {

std::size_t spritePixelIndex(int x, int y)
{
    return static_cast<std::size_t>(y) * document::kMaxSpriteWidth +
           static_cast<std::size_t>(x);
}

// Sprites have no separate SD mask: the SD original marks pixels outside the shape with 255.
const document::IndexedFrame& spriteShapeMask(const document::SpriteRecord& sprite,
                                              render::Resolution resolution)
{
    return resolution == render::Resolution::Hd && sprite.hasHd ? sprite.maskHd
                                                                : sprite.originalSd;
}

double linearRatio(render::Point start,
                   render::Point end,
                   render::Point point,
                   PaletteGradientDirection direction)
{
    if (direction == PaletteGradientDirection::Free) {
        const double dx = static_cast<double>(end.x - start.x);
        const double dy = static_cast<double>(end.y - start.y);
        const double lengthSquared = dx * dx + dy * dy;
        if (lengthSquared <= 0.0) {
            return 0.0;
        }
        const double px = static_cast<double>(point.x - start.x);
        const double py = static_cast<double>(point.y - start.y);
        return std::clamp((px * dx + py * dy) / lengthSquared, 0.0, 1.0);
    }

    const double minX = static_cast<double>(std::min(start.x, end.x));
    const double maxX = static_cast<double>(std::max(start.x, end.x));
    const double minY = static_cast<double>(std::min(start.y, end.y));
    const double maxY = static_cast<double>(std::max(start.y, end.y));
    double fromX = minX;
    double fromY = minY;
    double toX = maxX;
    double toY = minY;
    switch (direction) {
        case PaletteGradientDirection::RightToLeft:
            fromX = maxX;
            fromY = minY;
            toX = minX;
            toY = minY;
            break;
        case PaletteGradientDirection::TopToBottom:
            fromX = minX;
            fromY = minY;
            toX = minX;
            toY = maxY;
            break;
        case PaletteGradientDirection::BottomToTop:
            fromX = minX;
            fromY = maxY;
            toX = minX;
            toY = minY;
            break;
        case PaletteGradientDirection::TopLeftToBottomRight:
            fromX = minX;
            fromY = minY;
            toX = maxX;
            toY = maxY;
            break;
        case PaletteGradientDirection::BottomRightToTopLeft:
            fromX = maxX;
            fromY = maxY;
            toX = minX;
            toY = minY;
            break;
        case PaletteGradientDirection::TopRightToBottomLeft:
            fromX = maxX;
            fromY = minY;
            toX = minX;
            toY = maxY;
            break;
        case PaletteGradientDirection::BottomLeftToTopRight:
            fromX = minX;
            fromY = maxY;
            toX = maxX;
            toY = minY;
            break;
        case PaletteGradientDirection::LeftToRight:
        case PaletteGradientDirection::Free:
        case PaletteGradientDirection::CenterToEdge:
        case PaletteGradientDirection::EdgeToCenter:
            break;
    }

    const double dx = toX - fromX;
    const double dy = toY - fromY;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0) {
        return 0.0;
    }
    const double px = static_cast<double>(point.x) - fromX;
    const double py = static_cast<double>(point.y) - fromY;
    return std::clamp((px * dx + py * dy) / lengthSquared, 0.0, 1.0);
}

double radialRatio(render::Point start,
                   render::Point end,
                   render::Point point,
                   PaletteGradientPattern pattern,
                   PaletteGradientDirection direction)
{
    const double minX = static_cast<double>(std::min(start.x, end.x));
    const double maxX = static_cast<double>(std::max(start.x, end.x));
    const double minY = static_cast<double>(std::min(start.y, end.y));
    const double maxY = static_cast<double>(std::max(start.y, end.y));
    const double centerX = (minX + maxX) / 2.0;
    const double centerY = (minY + maxY) / 2.0;
    const double radiusX = std::max(0.5, (maxX - minX) / 2.0);
    const double radiusY = std::max(0.5, (maxY - minY) / 2.0);
    const double dx = static_cast<double>(point.x) - centerX;
    const double dy = static_cast<double>(point.y) - centerY;
    const double ratio = pattern == PaletteGradientPattern::Circle
                             ? std::hypot(dx, dy) / std::max(radiusX, radiusY)
                             : std::hypot(dx / radiusX, dy / radiusY);
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    return direction == PaletteGradientDirection::EdgeToCenter ? 1.0 - clamped : clamped;
}

uint16_t readPixel(const document::ProjectDocument& document, const EditLayer& layer, int x, int y)
{
    switch (layer.kind) {
        case EditKind::FramePixels:
            return document.colorizedPixel(layer.index, layer.resolution, x, y);
        case EditKind::BackgroundImage:
            return document.backgroundPixel(layer.index, layer.resolution, x, y);
        case EditKind::SpriteImage:
            return document.spriteColorizedPixel(layer.index, layer.resolution, x, y);
        default:
            throw std::invalid_argument("edit kind has no RGB565 pixels");
    }
}

uint8_t readMaskPixel(const document::ProjectDocument& document,
                      const EditLayer& layer,
                      int x,
                      int y)
{
    switch (layer.kind) {
        case EditKind::ComparisonMask:
            return document.comparisonMaskPixel(layer.index, x, y);
        case EditKind::DynamicMask:
            return document.dynamicMaskPixel(layer.index, layer.resolution, x, y);
        case EditKind::BackgroundMask:
            return document.backgroundMaskPixel(layer.index, layer.resolution, x, y);
        default:
            throw std::invalid_argument("edit kind has no mask pixels");
    }
}

}  // namespace

std::vector<render::Point> floodFillRegion(render::Size size,
                                           render::Point seed,
                                           const RegionPredicate& inRegion)
{
    std::vector<render::Point> pixels;
    if (size.width <= 0 || size.height <= 0) {
        return pixels;
    }
    std::vector<uint8_t> visited(
        static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height), 0);
    std::deque<render::Point> queue;
    queue.push_back(seed);

    while (!queue.empty()) {
        const render::Point point = queue.front();
        queue.pop_front();
        if (point.x < 0 || point.y < 0 || point.x >= size.width || point.y >= size.height) {
            continue;
        }
        const std::size_t index =
            static_cast<std::size_t>(point.y) * static_cast<std::size_t>(size.width) +
            static_cast<std::size_t>(point.x);
        if (visited[index]) {
            continue;
        }
        visited[index] = 1;
        if (!inRegion(point)) {
            continue;
        }

        pixels.push_back(point);
        queue.push_back(render::Point{point.x - 1, point.y});
        queue.push_back(render::Point{point.x + 1, point.y});
        queue.push_back(render::Point{point.x, point.y - 1});
        queue.push_back(render::Point{point.x, point.y + 1});
    }
    return pixels;
}

std::vector<render::Point> spriteFloodFillPixels(const document::ProjectDocument& document,
                                                 std::size_t spriteIndex,
                                                 render::Resolution resolution,
                                                 render::Point seed)
{
    const document::SpriteRecord& sprite = document.sprite(spriteIndex);
    const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
    const document::IndexedFrame& mask = spriteShapeMask(sprite, resolution);
    const document::Rgb565Frame& frame = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    const uint16_t targetColor =
        document.spriteColorizedPixel(spriteIndex, resolution, seed.x, seed.y);
    if (targetColor == 0) {
        return {};
    }
    return floodFillRegion(frame.size, seed, [&mask, &frame, targetColor](render::Point point) {
        const std::size_t index = spritePixelIndex(point.x, point.y);
        return mask.pixels[index] != 255 && frame.pixels[index] == targetColor;
    });
}

double paletteGradientRatio(const PaletteGradient& gradient, render::Point point)
{
    if (gradient.pattern != PaletteGradientPattern::Linear) {
        return radialRatio(gradient.shapeStart,
                           gradient.shapeEnd,
                           point,
                           gradient.pattern,
                           gradient.direction);
    }
    if (gradient.direction == PaletteGradientDirection::Free) {
        return linearRatio(gradient.directionStart,
                           gradient.directionEnd,
                           point,
                           gradient.direction);
    }
    return linearRatio(gradient.shapeStart, gradient.shapeEnd, point, gradient.direction);
}

std::size_t paletteGradientColorIndex(int startIndex, int endIndex, double ratio)
{
    const int distance = std::abs(endIndex - startIndex);
    if (distance == 0) {
        return static_cast<std::size_t>(startIndex);
    }
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    const int step = std::min(distance,
                              static_cast<int>(std::floor(clamped * (distance + 1))));
    const int direction = startIndex <= endIndex ? 1 : -1;
    return static_cast<std::size_t>(startIndex + direction * step);
}

std::size_t paletteGradientColorIndex(const PaletteGradient& gradient, render::Point point)
{
    return paletteGradientColorIndex(gradient.startColorIndex,
                                     gradient.endColorIndex,
                                     paletteGradientRatio(gradient, point));
}

uint16_t interpolateRgb565(uint16_t startColor,
                           uint16_t endColor,
                           std::size_t step,
                           std::size_t maxStep)
{
    if (maxStep == 0) {
        return startColor;
    }
    const uint32_t denominator = static_cast<uint32_t>(maxStep);
    const uint32_t numerator = static_cast<uint32_t>(std::min(step, maxStep));
    const auto blend = [=](int shift, uint32_t mask) {
        const uint32_t start = (startColor >> shift) & mask;
        const uint32_t end = (endColor >> shift) & mask;
        const uint32_t value =
            (start * (denominator - numerator) + end * numerator + denominator / 2) / denominator;
        return static_cast<uint16_t>(value << shift);
    };
    return static_cast<uint16_t>(blend(11, 0x1f) | blend(5, 0x3f) | blend(0, 0x1f));
}

std::vector<PaletteColorChange> makePaletteRampChanges(const document::ProjectDocument& document,
                                                       std::size_t paletteIndex,
                                                       int startIndex,
                                                       int endIndex)
{
    const int first = std::min(startIndex, endIndex);
    const int last = std::max(startIndex, endIndex);
    const uint16_t startColor =
        document.paletteColor(paletteIndex, static_cast<std::size_t>(startIndex));
    const uint16_t endColor =
        document.paletteColor(paletteIndex, static_cast<std::size_t>(endIndex));
    const std::size_t maxStep = static_cast<std::size_t>(last - first);

    std::vector<PaletteColorChange> changes;
    for (int colorIndex = first; colorIndex <= last; ++colorIndex) {
        std::size_t step = static_cast<std::size_t>(colorIndex - first);
        if (startIndex > endIndex) {
            step = maxStep - step;
        }
        const std::size_t storedColorIndex = static_cast<std::size_t>(colorIndex);
        const uint16_t oldColor = document.paletteColor(paletteIndex, storedColorIndex);
        const uint16_t newColor = interpolateRgb565(startColor, endColor, step, maxStep);
        if (oldColor == newColor) {
            continue;
        }
        changes.push_back(PaletteColorChange{storedColorIndex, oldColor, newColor});
    }
    return changes;
}

PixelColorSource paletteGradientColorSource(const document::ProjectDocument& document,
                                            std::size_t paletteIndex,
                                            const PaletteGradient& gradient)
{
    return [&document, paletteIndex, gradient](render::Point point) {
        return document.paletteColor(paletteIndex, paletteGradientColorIndex(gradient, point));
    };
}

std::vector<PixelChange> makePixelChanges(const document::ProjectDocument& document,
                                          const EditLayer& layer,
                                          const std::vector<render::Point>& points,
                                          const PixelColorSource& colorForPoint)
{
    std::vector<PixelChange> changes;
    std::set<std::pair<int, int>> seen;
    for (const render::Point& point : points) {
        if (!seen.insert({point.x, point.y}).second) {
            continue;
        }
        const uint16_t newColor = colorForPoint(point);
        const uint16_t oldColor = readPixel(document, layer, point.x, point.y);
        if (oldColor == newColor) {
            continue;
        }
        changes.push_back(PixelChange{point.x, point.y, oldColor, newColor});
    }
    return changes;
}

std::vector<PixelChange> makePixelChanges(const document::ProjectDocument& document,
                                          const EditLayer& layer,
                                          const std::vector<render::Point>& points,
                                          uint16_t color)
{
    return makePixelChanges(document, layer, points, [color](render::Point) { return color; });
}

std::vector<MaskPixelChange> makeMaskChanges(const document::ProjectDocument& document,
                                             const EditLayer& layer,
                                             const std::vector<render::Point>& points,
                                             uint8_t value)
{
    std::vector<MaskPixelChange> changes;
    std::set<std::pair<int, int>> seen;
    for (const render::Point& point : points) {
        if (!seen.insert({point.x, point.y}).second) {
            continue;
        }
        const uint8_t oldValue = readMaskPixel(document, layer, point.x, point.y);
        if (oldValue == value) {
            continue;
        }
        changes.push_back(MaskPixelChange{point.x, point.y, oldValue, value});
    }
    return changes;
}

std::vector<PixelChange> makeSpritePaintChanges(const document::ProjectDocument& document,
                                                std::size_t spriteIndex,
                                                render::Resolution resolution,
                                                const std::vector<render::Point>& points,
                                                uint16_t color,
                                                bool erase)
{
    const document::IndexedFrame& mask = spriteShapeMask(document.sprite(spriteIndex), resolution);
    std::vector<PixelChange> changes;
    std::set<std::pair<int, int>> seen;
    for (const render::Point& point : points) {
        if (!seen.insert({point.x, point.y}).second) {
            continue;
        }
        if (point.x < 0 || point.y < 0 ||
            point.x >= static_cast<int>(document::kMaxSpriteWidth) ||
            point.y >= static_cast<int>(document::kMaxSpriteHeight)) {
            continue;
        }
        if (mask.pixels[spritePixelIndex(point.x, point.y)] == 255) {
            continue;
        }
        const uint16_t currentColor =
            document.spriteColorizedPixel(spriteIndex, resolution, point.x, point.y);
        if (!erase && currentColor == 0) {
            continue;
        }
        if (currentColor == color) {
            continue;
        }
        changes.push_back(PixelChange{point.x, point.y, currentColor, color});
    }
    return changes;
}

}  // namespace colorizing::edit
