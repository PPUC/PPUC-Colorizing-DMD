#include "edit/AutoCopy.h"

#include <array>
#include <stdexcept>

namespace colorizing::edit {

namespace {

std::array<uint32_t, 256> buildCrc32Table()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1) ? (value >> 1) ^ 0xEDB88320u : value >> 1;
        }
        table[i] = value;
    }
    return table;
}

bool sameInsideMask(const document::IndexedFrame& left,
                    const document::IndexedFrame& right,
                    const std::vector<uint8_t>& romMask)
{
    for (std::size_t i = 0; i < romMask.size(); ++i) {
        if (romMask[i] && left.pixels[i] != right.pixels[i]) {
            return false;
        }
    }
    return true;
}

bool hasPlane(const document::ProjectDocument& document,
              std::size_t frameIndex,
              render::Resolution plane)
{
    return plane != render::Resolution::Hd || document.hasHdFrame(frameIndex);
}

}  // namespace

uint32_t maskedOriginalHash(const document::IndexedFrame& original,
                            const std::vector<uint8_t>& romMask)
{
    static const std::array<uint32_t, 256> table = buildCrc32Table();
    if (romMask.size() != original.pixels.size()) {
        throw std::invalid_argument("the mask does not have the original frame's size");
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < original.pixels.size(); ++i) {
        if (romMask[i]) {
            crc = (crc >> 8) ^ table[(original.pixels[i] ^ crc) & 0xFF];
        }
    }
    return ~crc;
}

AutoCopyScan scanAutoCopy(const document::ProjectDocument& document,
                          std::size_t sourceFrame,
                          const PlaneSelection& selection,
                          const std::vector<std::size_t>& candidates)
{
    AutoCopyScan scan;
    if (selection.isEmpty() || sourceFrame >= document.frameCount()) {
        return scan;
    }
    const std::vector<uint8_t> romMask = selectionRomMask(document.geometry(), selection);
    const document::IndexedFrame& source = document.originalFrame(sourceFrame);
    const uint32_t sourceHash = maskedOriginalHash(source, romMask);
    const auto visit = [&](std::size_t frameIndex) {
        if (frameIndex == sourceFrame || frameIndex >= document.frameCount()) {
            return;
        }
        const document::IndexedFrame& original = document.originalFrame(frameIndex);
        if (maskedOriginalHash(original, romMask) != sourceHash) {
            return;
        }
        ++scan.hashCandidates;
        if (!sameInsideMask(source, original, romMask)) {
            ++scan.hashCollisions;
            return;
        }
        if (!hasPlane(document, frameIndex, selection.plane())) {
            ++scan.withoutPlane;
            return;
        }
        scan.matches.push_back(frameIndex);
    };
    if (candidates.empty()) {
        for (std::size_t frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
            visit(frameIndex);
        }
    } else {
        for (std::size_t frameIndex : candidates) {
            visit(frameIndex);
        }
    }
    return scan;
}

FrameContentChanges makeAutoCopyChanges(const document::ProjectDocument& document,
                                        std::size_t sourceFrame,
                                        std::size_t targetFrame,
                                        const PlaneSelection& selection)
{
    const render::Resolution plane = selection.plane();
    FrameContentChanges empty;
    empty.plane = plane;
    if (!hasPlane(document, targetFrame, plane) || !hasPlane(document, sourceFrame, plane)) {
        return empty;
    }
    const document::Rgb565Frame& source = document.colorizedFrame(sourceFrame, plane);
    const std::optional<document::FrameDynamicData>& sourceDynamic =
        document.dynamicData(sourceFrame);
    const bool withDynamic = plane == document.dynamicPlane() && sourceDynamic &&
                             document.dynamicData(targetFrame);
    const document::IndexedFrame* sourceMask =
        withDynamic ? (plane == render::Resolution::Hd ? &sourceDynamic->maskHd
                                                       : &sourceDynamic->maskSd)
                    : nullptr;
    FrameContentBuilder builder(document, targetFrame, plane);
    const render::Size size = selection.size();
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(size.width) +
                                      static_cast<std::size_t>(x);
            if (!selection.mask()[index]) {
                continue;
            }
            builder.setPixel(render::Point{x, y}, source.pixels[index]);
            if (sourceMask) {
                builder.setDynamicMask(render::Point{x, y}, sourceMask->pixels[index]);
            }
        }
    }
    return builder.changes();
}

}  // namespace colorizing::edit
