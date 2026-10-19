#include "edit/CopyPaste.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace colorizing::edit {

namespace {

std::size_t planeIndex(render::Size size, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
           static_cast<std::size_t>(x);
}

std::string sizeText(render::Size size)
{
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

uint8_t romShade(const document::IndexedFrame& original,
                 const render::ProjectGeometry& geometry,
                 render::Resolution plane,
                 int x,
                 int y)
{
    const render::Point rom = geometry.toRomFrame(plane, render::Point{x, y});
    return original.pixels[planeIndex(original.size, rom.x, rom.y)];
}

}  // namespace

std::size_t CopyBuffer::selectedCount() const
{
    return static_cast<std::size_t>(std::count(mask.begin(), mask.end(), uint8_t{1}));
}

CopyBuffer copySelection(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         const PlaneSelection& selection)
{
    const std::optional<render::Rect> box = selection.bounds();
    if (!box) {
        throw std::invalid_argument("nothing is selected");
    }
    const render::Resolution plane = selection.plane();
    const render::ProjectGeometry& geometry = document.geometry();
    if (!geometry.isEditable(plane) ||
        (plane == render::Resolution::Hd && !document.hasHdFrame(frameIndex))) {
        throw std::logic_error("the frame has no picture on the selection's plane");
    }
    const document::Rgb565Frame& colorized = document.colorizedFrame(frameIndex, plane);
    if (colorized.size != selection.size()) {
        throw std::invalid_argument("the selection does not have the plane's size");
    }
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    const std::optional<document::FrameDynamicData>& dynamicData = document.dynamicData(frameIndex);
    const bool withDynamic = plane == document.dynamicPlane() && dynamicData.has_value();
    const document::IndexedFrame* dynamicMask = nullptr;
    if (withDynamic) {
        dynamicMask =
            plane == render::Resolution::Hd ? &dynamicData->maskHd : &dynamicData->maskSd;
    }

    CopyBuffer buffer;
    buffer.geometry = geometry;
    buffer.colorCount = document.colorCount();
    buffer.plane = plane;
    buffer.sourceFrame = frameIndex;
    buffer.box = *box;
    const std::size_t boxPixels =
        static_cast<std::size_t>(box->width) * static_cast<std::size_t>(box->height);
    buffer.mask.assign(boxPixels, 0);
    buffer.colors.assign(boxPixels, 0);
    buffer.originals.assign(boxPixels, 0);
    if (dynamicMask) {
        buffer.dynamicMask.assign(boxPixels, 255);
    }
    std::vector<bool> usedSets(document::kMaxDynamicSetsPerFrame, false);
    for (int y = 0; y < box->height; ++y) {
        for (int x = 0; x < box->width; ++x) {
            const int planeX = box->x + x;
            const int planeY = box->y + y;
            const std::size_t index = static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(box->width) +
                                      static_cast<std::size_t>(x);
            // Unselected box pixels are carried too, as the Win32 buffers do; only the
            // selected ones are ever pasted.
            buffer.mask[index] = selection.contains(render::Point{planeX, planeY}) ? 1 : 0;
            buffer.colors[index] = colorized.pixels[planeIndex(colorized.size, planeX, planeY)];
            buffer.originals[index] = romShade(original, geometry, plane, planeX, planeY);
            if (dynamicMask) {
                const uint8_t value =
                    dynamicMask->pixels[planeIndex(dynamicMask->size, planeX, planeY)];
                buffer.dynamicMask[index] = value;
                if (buffer.mask[index] && value < document::kMaxDynamicSetsPerFrame) {
                    usedSets[value] = true;
                }
            }
        }
    }
    for (std::size_t setIndex = 0; setIndex < usedSets.size(); ++setIndex) {
        if (!usedSets[setIndex]) {
            continue;
        }
        CopiedDynamicSet set;
        set.setIndex = setIndex;
        for (std::size_t color = 0; color < document.colorCount(); ++color) {
            set.colors.push_back(document.dynamicColor(frameIndex, plane, setIndex, color));
        }
        set.shadowDirection = document.dynamicShadowDirection(frameIndex, plane, setIndex);
        set.shadowColor = document.dynamicShadowColor(frameIndex, plane, setIndex);
        buffer.dynamicSets.push_back(std::move(set));
    }
    return buffer;
}

std::size_t pasteSourceIndex(const CopyBuffer& buffer,
                             const PastePlacement& placement,
                             int boxX,
                             int boxY)
{
    const int x = placement.mirrorHorizontally ? buffer.box.width - 1 - boxX : boxX;
    const int y = placement.mirrorVertically ? buffer.box.height - 1 - boxY : boxY;
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.box.width) +
           static_cast<std::size_t>(x);
}

std::optional<std::string> pasteRefusal(const document::ProjectDocument& document,
                                        const CopyBuffer& buffer,
                                        render::Resolution plane,
                                        PasteMode mode)
{
    const render::ProjectGeometry& geometry = document.geometry();
    if (buffer.geometry != geometry || buffer.colorCount != document.colorCount()) {
        return std::string("the copied content belongs to a project of another layout");
    }
    if (!geometry.isEditable(plane)) {
        return std::string("this plane cannot be edited");
    }
    if (buffer.plane != plane) {
        return "the copied content is of the " + sizeText(geometry.planeSize(buffer.plane)) +
               " plane and can only be pasted there, not on the " +
               sizeText(geometry.planeSize(plane)) + " plane";
    }
    if (mode == PasteMode::DynamicMaskOnly && plane != document.dynamicPlane()) {
        return "dynamic zones are defined on the " +
               sizeText(geometry.planeSize(document.dynamicPlane())) + " plane only";
    }
    if (mode == PasteMode::DynamicMaskOnly && !buffer.hasDynamicData()) {
        return std::string("the copied content has no dynamic mask");
    }
    return std::nullopt;
}

FrameContentChanges makePasteChanges(const document::ProjectDocument& document,
                                     const CopyBuffer& buffer,
                                     std::size_t targetFrame,
                                     render::Resolution plane,
                                     const PastePlacement& placement,
                                     PasteMode mode)
{
    if (const std::optional<std::string> refusal = pasteRefusal(document, buffer, plane, mode)) {
        throw std::invalid_argument(*refusal);
    }
    FrameContentChanges empty;
    empty.plane = plane;
    if (plane == render::Resolution::Hd && !document.hasHdFrame(targetFrame)) {
        return empty;
    }
    const render::ProjectGeometry& geometry = document.geometry();
    const render::Size planeSize = geometry.planeSize(plane);
    const bool withDynamic = buffer.hasDynamicData() && document.dynamicData(targetFrame);
    if (mode == PasteMode::DynamicMaskOnly && !withDynamic) {
        return empty;
    }
    const document::IndexedFrame& original = document.originalFrame(targetFrame);
    FrameContentBuilder builder(document, targetFrame, plane);
    if (mode != PasteMode::DynamicMaskOnly && withDynamic) {
        for (const CopiedDynamicSet& set : buffer.dynamicSets) {
            for (std::size_t color = 0; color < set.colors.size(); ++color) {
                builder.setDynamicColor(set.setIndex, color, set.colors[color]);
            }
            builder.setDynamicShadow(set.setIndex, set.shadowDirection, set.shadowColor);
        }
    }
    for (int boxY = 0; boxY < buffer.box.height; ++boxY) {
        for (int boxX = 0; boxX < buffer.box.width; ++boxX) {
            const std::size_t source = pasteSourceIndex(buffer, placement, boxX, boxY);
            if (!buffer.mask[source]) {
                continue;
            }
            const int x = placement.x + boxX;
            const int y = placement.y + boxY;
            if (x < 0 || y < 0 || x >= planeSize.width || y >= planeSize.height) {
                continue;
            }
            if (mode == PasteMode::WhereOriginalsMatch &&
                buffer.originals[source] != romShade(original, geometry, plane, x, y)) {
                continue;
            }
            if (withDynamic) {
                builder.setDynamicMask(render::Point{x, y}, buffer.dynamicMask[source]);
            }
            if (mode != PasteMode::DynamicMaskOnly) {
                builder.setPixel(render::Point{x, y}, buffer.colors[source]);
            }
        }
    }
    return builder.changes();
}

}  // namespace colorizing::edit
