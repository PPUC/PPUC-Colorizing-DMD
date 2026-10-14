#include "edit/RecolorHelpers.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace colorizing::edit {

namespace {

std::size_t pixelIndex(render::Size size, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
           static_cast<std::size_t>(x);
}

bool inArea(const PlaneSelection* area, int x, int y)
{
    return !area || area->contains(render::Point{x, y});
}

const document::IndexedFrame* dynamicMaskOf(const document::ProjectDocument& document,
                                            std::size_t frameIndex)
{
    const std::optional<document::FrameDynamicData>& data = document.dynamicData(frameIndex);
    if (!data) {
        return nullptr;
    }
    return document.dynamicPlane() == render::Resolution::Hd ? &data->maskHd : &data->maskSd;
}

void requireAreaOnPlane(const PlaneSelection* area, render::Resolution plane)
{
    if (area && area->plane() != plane) {
        throw std::invalid_argument("the selection is on another plane");
    }
}

}  // namespace

bool isDynamicPixel(const document::ProjectDocument& document,
                    std::size_t frameIndex,
                    render::Resolution plane,
                    render::Point point)
{
    const document::IndexedFrame* mask = dynamicMaskOf(document, frameIndex);
    if (!mask) {
        return false;
    }
    const render::Point rom = document.geometry().toRomFrame(plane, point);
    if (rom.x < 0 || rom.y < 0 || rom.x >= mask->size.width || rom.y >= mask->size.height) {
        return false;
    }
    return mask->pixels[pixelIndex(mask->size, rom.x, rom.y)] < document::kMaxDynamicSetsPerFrame;
}

FrameContentChanges makeRecolorChanges(const document::ProjectDocument& document,
                                       std::size_t frameIndex,
                                       render::Resolution plane,
                                       uint16_t colorToReplace,
                                       const PixelColorSource& newColor)
{
    FrameContentChanges empty;
    empty.plane = plane;
    if (plane == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
        return empty;
    }
    const document::Rgb565Frame& frame = document.colorizedFrame(frameIndex, plane);
    FrameContentBuilder builder(document, frameIndex, plane);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            if (frame.pixels[pixelIndex(frame.size, x, y)] == colorToReplace) {
                builder.setPixel(render::Point{x, y}, newColor(render::Point{x, y}));
            }
        }
    }
    return builder.changes();
}

FrameContentChanges makeBlackOriginalToBlackChanges(const document::ProjectDocument& document,
                                                    std::size_t frameIndex,
                                                    render::Resolution plane,
                                                    const PlaneSelection* area)
{
    requireAreaOnPlane(area, plane);
    FrameContentChanges empty;
    empty.plane = plane;
    if (plane == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
        return empty;
    }
    const render::ProjectGeometry& geometry = document.geometry();
    const document::Rgb565Frame& frame = document.colorizedFrame(frameIndex, plane);
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    FrameContentBuilder builder(document, frameIndex, plane);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            if (!inArea(area, x, y)) {
                continue;
            }
            const render::Point rom = geometry.toRomFrame(plane, render::Point{x, y});
            if (original.pixels[pixelIndex(original.size, rom.x, rom.y)] == 0) {
                builder.setPixel(render::Point{x, y}, 0);
            }
        }
    }
    return builder.changes();
}

FrameContentChanges makeRemoveDynamicChanges(const document::ProjectDocument& document,
                                             std::size_t frameIndex,
                                             const PlaneSelection* area)
{
    const render::Resolution plane = document.dynamicPlane();
    requireAreaOnPlane(area, plane);
    FrameContentChanges empty;
    empty.plane = plane;
    const document::IndexedFrame* mask = dynamicMaskOf(document, frameIndex);
    if (!mask) {
        return empty;
    }
    const std::vector<uint16_t> plainColors = document::plainOriginalColors(document.colorCount());
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    FrameContentBuilder builder(document, frameIndex, plane);
    for (int y = 0; y < mask->size.height; ++y) {
        for (int x = 0; x < mask->size.width; ++x) {
            if (!inArea(area, x, y) || mask->pixels[pixelIndex(mask->size, x, y)] == 255) {
                continue;
            }
            // The dynamic plane is the native one: its pixels are the ROM frame's.
            const uint8_t shade = original.pixels[pixelIndex(original.size, x, y)];
            builder.setDynamicMask(render::Point{x, y}, 255);
            if (shade < plainColors.size()) {
                builder.setPixel(render::Point{x, y}, plainColors[shade]);
            }
        }
    }
    return builder.changes();
}

FrameContentChanges makeAssignDynamicSetChanges(const document::ProjectDocument& document,
                                                std::size_t frameIndex,
                                                const PlaneSelection* area,
                                                uint8_t dynamicSet)
{
    if (dynamicSet >= document::kMaxDynamicSetsPerFrame) {
        throw std::out_of_range("dynamic set does not exist");
    }
    const render::Resolution plane = document.dynamicPlane();
    requireAreaOnPlane(area, plane);
    FrameContentChanges empty;
    empty.plane = plane;
    const document::IndexedFrame* mask = dynamicMaskOf(document, frameIndex);
    if (!mask) {
        return empty;
    }
    FrameContentBuilder builder(document, frameIndex, plane);
    for (int y = 0; y < mask->size.height; ++y) {
        for (int x = 0; x < mask->size.width; ++x) {
            if (inArea(area, x, y)) {
                builder.setDynamicMask(render::Point{x, y}, dynamicSet);
            }
        }
    }
    return builder.changes();
}

FrameContentChanges makeDynamicToStaticChanges(const document::ProjectDocument& document,
                                               std::size_t frameIndex,
                                               const PlaneSelection* area)
{
    const render::Resolution plane = document.dynamicPlane();
    requireAreaOnPlane(area, plane);
    FrameContentChanges empty;
    empty.plane = plane;
    const document::IndexedFrame* mask = dynamicMaskOf(document, frameIndex);
    if (!mask) {
        return empty;
    }
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    FrameContentBuilder builder(document, frameIndex, plane);
    for (int y = 0; y < mask->size.height; ++y) {
        for (int x = 0; x < mask->size.width; ++x) {
            const uint8_t set = mask->pixels[pixelIndex(mask->size, x, y)];
            if (!inArea(area, x, y) || set == 255) {
                continue;
            }
            // The dynamic plane is the native one: its pixels are the ROM frame's.
            const uint8_t shade = original.pixels[pixelIndex(original.size, x, y)];
            builder.setDynamicMask(render::Point{x, y}, 255);
            if (set < document::kMaxDynamicSetsPerFrame && shade < document.colorCount()) {
                builder.setPixel(render::Point{x, y},
                                 document.dynamicColor(frameIndex, plane, set, shade));
            }
        }
    }
    return builder.changes();
}

uint16_t brightenedColor(uint16_t color, double coefficient)
{
    const auto scaled = [coefficient](int in, int maximum) {
        int out = static_cast<int>(static_cast<float>(in) * static_cast<float>(coefficient));
        if (coefficient > 1) {
            if (out == in) {
                ++out;
            }
            out = std::min(out, maximum);
        } else if (coefficient < 1) {
            if (out == in) {
                --out;
            }
            out = std::max(out, 0);
        }
        return out;
    };
    const int red = scaled(color >> 11, 0x1f);
    const int green = scaled((color >> 5) & 0x3f, 0x3f);
    const int blue = scaled(color & 0x1f, 0x1f);
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

FrameContentChanges makeBrightnessChanges(const document::ProjectDocument& document,
                                          std::size_t frameIndex,
                                          render::Resolution plane,
                                          const PlaneSelection& selection,
                                          double coefficient)
{
    requireAreaOnPlane(&selection, plane);
    if (coefficient < 0 || coefficient > 20) {
        throw std::out_of_range("the brightness factor is 0 to 20 (0 to 2000 percent)");
    }
    FrameContentChanges empty;
    empty.plane = plane;
    if (plane == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
        return empty;
    }
    const document::Rgb565Frame& frame = document.colorizedFrame(frameIndex, plane);
    FrameContentBuilder builder(document, frameIndex, plane);
    for (const render::Point& point : selection.pixels()) {
        const uint16_t color = frame.pixels[pixelIndex(frame.size, point.x, point.y)];
        builder.setPixel(point, brightenedColor(color, coefficient));
    }
    return builder.changes();
}

namespace {

std::vector<render::Point> directionOffsets(SelectionDirection direction)
{
    switch (direction) {
        case SelectionDirection::AllAround:
            return {{-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}};
        case SelectionDirection::Left:
            return {{-1, 0}};
        case SelectionDirection::BottomLeft:
            return {{-1, 1}};
        case SelectionDirection::Bottom:
            return {{0, 1}};
        case SelectionDirection::BottomRight:
            return {{1, 1}};
        case SelectionDirection::Right:
            return {{1, 0}};
        case SelectionDirection::TopRight:
            return {{1, -1}};
        case SelectionDirection::Top:
            return {{0, -1}};
        case SelectionDirection::TopLeft:
            return {{-1, -1}};
    }
    return {};
}

// The pixels next to the selection in the direction that are not selected, each once.
std::vector<render::Point> neighbours(const PlaneSelection& selection,
                                      SelectionDirection direction)
{
    const render::Size size = selection.size();
    const std::vector<render::Point> offsets = directionOffsets(direction);
    std::vector<uint8_t> taken(static_cast<std::size_t>(size.width) * size.height, 0);
    std::vector<render::Point> result;
    for (const render::Point& point : selection.pixels()) {
        for (const render::Point& offset : offsets) {
            const render::Point next{point.x + offset.x, point.y + offset.y};
            if (next.x < 0 || next.y < 0 || next.x >= size.width || next.y >= size.height ||
                selection.contains(next)) {
                continue;
            }
            uint8_t& seen = taken[pixelIndex(size, next.x, next.y)];
            if (!seen) {
                seen = 1;
                result.push_back(next);
            }
        }
    }
    return result;
}

}  // namespace

FrameContentChanges makeSelectionShadowChanges(const document::ProjectDocument& document,
                                               std::size_t frameIndex,
                                               render::Resolution plane,
                                               const PlaneSelection& selection,
                                               SelectionDirection direction,
                                               uint16_t color)
{
    requireAreaOnPlane(&selection, plane);
    FrameContentChanges empty;
    empty.plane = plane;
    if (plane == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
        return empty;
    }
    FrameContentBuilder builder(document, frameIndex, plane);
    for (const render::Point& point : neighbours(selection, direction)) {
        if (!isDynamicPixel(document, frameIndex, plane, point)) {
            builder.setPixel(point, color);
        }
    }
    return builder.changes();
}

PlaneSelection grownSelection(const PlaneSelection& selection, SelectionDirection direction)
{
    PlaneSelection grown = selection;
    grown.add(neighbours(selection, direction));
    return grown;
}

}  // namespace colorizing::edit
