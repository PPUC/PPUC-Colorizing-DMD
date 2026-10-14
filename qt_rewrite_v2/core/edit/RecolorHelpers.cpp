#include "edit/RecolorHelpers.h"

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

}  // namespace colorizing::edit
