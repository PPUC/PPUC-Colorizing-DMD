#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "render/CanvasTypes.h"

namespace colorizing::edit {

using RegionPredicate = std::function<bool(render::Point)>;

// The 4-connected pixels reachable from seed through pixels accepted by inRegion, in
// breadth-first order. inRegion is asked at most once per pixel and only for pixels inside
// size; a seed outside size or rejected by inRegion gives an empty result.
[[nodiscard]] std::vector<render::Point> floodFillRegion(render::Size size,
                                                         render::Point seed,
                                                         const RegionPredicate& inRegion);

// Flood fill over a row-major buffer of size.width * size.height values equal to target.
template <typename Value>
[[nodiscard]] std::vector<render::Point> floodFillMatching(const std::vector<Value>& values,
                                                           render::Size size,
                                                           render::Point seed,
                                                           Value target)
{
    return floodFillRegion(size, seed, [&values, size, target](render::Point point) {
        return values[static_cast<std::size_t>(point.y) * static_cast<std::size_t>(size.width) +
                      static_cast<std::size_t>(point.x)] == target;
    });
}

// The paintable sprite pixels connected to seed that share its colorized color. Transparent
// pixels (color 0) and pixels outside the sprite shape are never part of a fill.
[[nodiscard]] std::vector<render::Point> spriteFloodFillPixels(
    const document::ProjectDocument& document,
    std::size_t spriteIndex,
    render::Resolution resolution,
    render::Point seed);

enum class PaletteGradientDirection {
    Free,
    LeftToRight,
    RightToLeft,
    TopToBottom,
    BottomToTop,
    TopLeftToBottomRight,
    BottomRightToTopLeft,
    TopRightToBottomLeft,
    BottomLeftToTopRight,
    CenterToEdge,
    EdgeToCenter,
};

enum class PaletteGradientPattern {
    Linear,
    Circle,
    Ellipse,
};

// A run of palette colors spread over the bounding box of a drawn shape.
struct PaletteGradient {
    render::Point shapeStart;
    render::Point shapeEnd;
    // The line a linear gradient follows when the direction is Free; unused otherwise.
    render::Point directionStart;
    render::Point directionEnd;
    PaletteGradientDirection direction = PaletteGradientDirection::LeftToRight;
    PaletteGradientPattern pattern = PaletteGradientPattern::Linear;
    // Palette color indices at ratio 0 and 1; the start may be the larger one.
    int startColorIndex = 0;
    int endColorIndex = 0;
};

// Position of point within the gradient, clamped to 0..1. CenterToEdge and EdgeToCenter only
// affect the circle and ellipse patterns; a linear gradient treats them as LeftToRight.
[[nodiscard]] double paletteGradientRatio(const PaletteGradient& gradient, render::Point point);

// Splits 0..1 into equal bands, one per palette color between the two indices.
[[nodiscard]] std::size_t paletteGradientColorIndex(int startIndex, int endIndex, double ratio);
[[nodiscard]] std::size_t paletteGradientColorIndex(const PaletteGradient& gradient,
                                                    render::Point point);

// Blends in 8-bit RGB with truncating division, so the result matches colors picked in the UI.
[[nodiscard]] uint16_t interpolateRgb565(uint16_t startColor,
                                         uint16_t endColor,
                                         std::size_t step,
                                         std::size_t maxStep);

// Replaces the palette colors between the two indices by a blend from the start color to the
// end color. Colors that already have the blended value are left out.
[[nodiscard]] std::vector<PaletteColorChange> makePaletteRampChanges(
    const document::ProjectDocument& document,
    std::size_t paletteIndex,
    int startIndex,
    int endIndex);

// The pixels a PixelEditCommand or MaskEditCommand of the same kind, index and resolution edits.
struct EditLayer {
    EditKind kind = EditKind::FramePixels;
    std::size_t index = 0;
    render::Resolution resolution = render::Resolution::Sd;
};

using PixelColorSource = std::function<uint16_t(render::Point)>;

// One change per distinct point, in first-occurrence order, with the old value read from the
// document; points that already have the new value are skipped. Points outside the layer throw
// like the document accessors do. layer.kind must be FramePixels, BackgroundImage or
// SpriteImage.
[[nodiscard]] std::vector<PixelChange> makePixelChanges(const document::ProjectDocument& document,
                                                        const EditLayer& layer,
                                                        const std::vector<render::Point>& points,
                                                        const PixelColorSource& colorForPoint);
[[nodiscard]] std::vector<PixelChange> makePixelChanges(const document::ProjectDocument& document,
                                                        const EditLayer& layer,
                                                        const std::vector<render::Point>& points,
                                                        uint16_t color);

// layer.kind must be ComparisonMask, DynamicMask or BackgroundMask.
[[nodiscard]] std::vector<MaskPixelChange> makeMaskChanges(
    const document::ProjectDocument& document,
    const EditLayer& layer,
    const std::vector<render::Point>& points,
    uint8_t value);

// Sprite painting keeps the sprite's shape: points outside the sprite canvas or its shape are
// skipped, and only erasing may touch a transparent (color 0) pixel.
[[nodiscard]] std::vector<PixelChange> makeSpritePaintChanges(
    const document::ProjectDocument& document,
    std::size_t spriteIndex,
    render::Resolution resolution,
    const std::vector<render::Point>& points,
    uint16_t color,
    bool erase);

}  // namespace colorizing::edit
