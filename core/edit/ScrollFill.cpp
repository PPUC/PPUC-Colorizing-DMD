#include "edit/ScrollFill.h"

#include <algorithm>

namespace colorizing::edit {

namespace {

uint8_t pixelAt(const document::IndexedFrame& frame, int x, int y)
{
    return frame.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                        static_cast<std::size_t>(x)];
}

bool rowsEqual(const document::IndexedFrame& a, int rowA, const document::IndexedFrame& b, int rowB)
{
    for (int x = 0; x < a.size.width; ++x) {
        if (pixelAt(a, x, rowA) != pixelAt(b, x, rowB)) {
            return false;
        }
    }
    return true;
}

bool columnsEqual(const document::IndexedFrame& a,
                  int columnA,
                  const document::IndexedFrame& b,
                  int columnB)
{
    for (int y = 0; y < a.size.height; ++y) {
        if (pixelAt(a, columnA, y) != pixelAt(b, columnB, y)) {
            return false;
        }
    }
    return true;
}

// frame's lines 0.. against reference's lines shift..: the frame shows the reference moved by
// shift towards line 0.
template <typename LinesEqual>
bool shiftedTowardsStart(int lineCount, int shift, const LinesEqual& equal)
{
    for (int line = shift; line < lineCount; ++line) {
        if (!equal(line - shift, line)) {
            return false;
        }
    }
    return true;
}

struct PlaneContent {
    render::Size size;
    std::vector<uint16_t> colors;
    // Empty unless the plane is the dynamic plane.
    std::vector<uint8_t> dynamicMask;
};

PlaneContent readContent(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         render::Resolution plane)
{
    PlaneContent content;
    const document::Rgb565Frame& frame = document.colorizedFrame(frameIndex, plane);
    content.size = frame.size;
    content.colors = frame.pixels.toVector();
    const std::optional<document::FrameDynamicData>& dynamicData = document.dynamicData(frameIndex);
    if (plane == document.dynamicPlane() && dynamicData) {
        content.dynamicMask =
            (plane == render::Resolution::Hd ? dynamicData->maskHd : dynamicData->maskSd)
                .pixels.toVector();
    }
    return content;
}

void copyPixel(const PlaneContent& source, int sx, int sy, PlaneContent& target, int tx, int ty)
{
    const std::size_t from = static_cast<std::size_t>(sy) *
                                 static_cast<std::size_t>(source.size.width) +
                             static_cast<std::size_t>(sx);
    const std::size_t to = static_cast<std::size_t>(ty) *
                               static_cast<std::size_t>(target.size.width) +
                           static_cast<std::size_t>(tx);
    target.colors[to] = source.colors[from];
    // CopyRows and CopyCols move the dynamic mask with the colors.
    if (!source.dynamicMask.empty() && !target.dynamicMask.empty()) {
        target.dynamicMask[to] = source.dynamicMask[from];
    }
}

// Where a reference pixel lands in the frame for the match, in the same units.
render::Point movedPoint(const ScrollMatch& match, int shift, int x, int y)
{
    switch (match.direction) {
        case ScrollDirection::CameraDown:
            return {x, y - shift};
        case ScrollDirection::CameraUp:
            return {x, y + shift};
        case ScrollDirection::CameraRight:
            return {x - shift, y};
        case ScrollDirection::CameraLeft:
            return {x + shift, y};
    }
    return {x, y};
}

bool matchesPart(const document::IndexedFrame& reference,
                 const document::IndexedFrame& frame,
                 const std::vector<uint8_t>& romMask,
                 const ScrollMatch& match)
{
    const int width = frame.size.width;
    const int height = frame.size.height;
    bool lit = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!romMask[static_cast<std::size_t>(y) * width + x]) {
                continue;
            }
            const render::Point to = movedPoint(match, match.shift, x, y);
            if (to.x < 0 || to.y < 0 || to.x >= width || to.y >= height) {
                continue;
            }
            const uint8_t value = pixelAt(reference, x, y);
            if (pixelAt(frame, to.x, to.y) != value) {
                return false;
            }
            lit = lit || value != 0;
        }
    }
    return lit;
}

// The selected part of the reference (romMask, scale plane pixels per ROM pixel) moved by
// the match into the target.
void applyPartScroll(const PlaneContent& reference,
                     const ScrollMatch& match,
                     int scale,
                     const std::vector<uint8_t>& romMask,
                     int romWidth,
                     PlaneContent& target)
{
    const int shift = match.shift * scale;
    for (int y = 0; y < reference.size.height; ++y) {
        for (int x = 0; x < reference.size.width; ++x) {
            if (!romMask[static_cast<std::size_t>(y / scale) * romWidth + x / scale]) {
                continue;
            }
            const render::Point to = movedPoint(match, shift, x, y);
            if (to.x >= 0 && to.y >= 0 && to.x < target.size.width && to.y < target.size.height) {
                copyPixel(reference, x, y, target, to.x, to.y);
            }
        }
    }
}

void applyScroll(const PlaneContent& reference,
                 const ScrollMatch& match,
                 int scale,
                 PlaneContent& target)
{
    const int width = target.size.width;
    const int height = target.size.height;
    const int shift = match.shift * scale;
    switch (match.direction) {
        case ScrollDirection::CameraDown:
            for (int y = 0; y + shift < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    copyPixel(reference, x, y + shift, target, x, y);
                }
            }
            break;
        case ScrollDirection::CameraUp:
            for (int y = 0; y + shift < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    copyPixel(reference, x, y, target, x, y + shift);
                }
            }
            break;
        case ScrollDirection::CameraRight:
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x + shift < width; ++x) {
                    copyPixel(reference, x + shift, y, target, x, y);
                }
            }
            break;
        case ScrollDirection::CameraLeft:
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x + shift < width; ++x) {
                    copyPixel(reference, x, y, target, x + shift, y);
                }
            }
            break;
    }
}

bool hasPlane(const document::ProjectDocument& document,
              std::size_t frameIndex,
              render::Resolution plane)
{
    return plane != render::Resolution::Hd || document.hasHdFrame(frameIndex);
}

}  // namespace

std::optional<ScrollMatch> findScroll(const document::IndexedFrame& reference,
                                      const document::IndexedFrame& frame,
                                      const std::vector<uint8_t>* romMask)
{
    if (reference.size != frame.size) {
        return std::nullopt;
    }
    const int width = frame.size.width;
    const int height = frame.size.height;
    if (romMask) {
        if (romMask->size() != frame.pixels.size()) {
            return std::nullopt;
        }
        // The same order as for whole frames: down, up, right, left, smallest shift first.
        for (const ScrollDirection direction : {ScrollDirection::CameraDown,
                                                ScrollDirection::CameraUp,
                                                ScrollDirection::CameraRight,
                                                ScrollDirection::CameraLeft}) {
            const bool vertical = direction == ScrollDirection::CameraDown ||
                                  direction == ScrollDirection::CameraUp;
            for (int shift = 1; shift < (vertical ? height : width); ++shift) {
                if (matchesPart(reference, frame, *romMask, ScrollMatch{direction, shift})) {
                    return ScrollMatch{direction, shift};
                }
            }
        }
        return std::nullopt;
    }
    const auto rowsFrameRef = [&](int frameRow, int referenceRow) {
        return rowsEqual(frame, frameRow, reference, referenceRow);
    };
    const auto rowsRefFrame = [&](int referenceRow, int frameRow) {
        return rowsEqual(reference, referenceRow, frame, frameRow);
    };
    const auto columnsFrameRef = [&](int frameColumn, int referenceColumn) {
        return columnsEqual(frame, frameColumn, reference, referenceColumn);
    };
    const auto columnsRefFrame = [&](int referenceColumn, int frameColumn) {
        return columnsEqual(reference, referenceColumn, frame, frameColumn);
    };
    for (int shift = 1; shift < height; ++shift) {
        if (shiftedTowardsStart(height, shift, rowsFrameRef)) {
            return ScrollMatch{ScrollDirection::CameraDown, shift};
        }
    }
    for (int shift = 1; shift < height; ++shift) {
        if (shiftedTowardsStart(height, shift, rowsRefFrame)) {
            return ScrollMatch{ScrollDirection::CameraUp, shift};
        }
    }
    for (int shift = 1; shift < width; ++shift) {
        if (shiftedTowardsStart(width, shift, columnsFrameRef)) {
            return ScrollMatch{ScrollDirection::CameraRight, shift};
        }
    }
    for (int shift = 1; shift < width; ++shift) {
        if (shiftedTowardsStart(width, shift, columnsRefFrame)) {
            return ScrollMatch{ScrollDirection::CameraLeft, shift};
        }
    }
    return std::nullopt;
}

std::pair<std::size_t, std::size_t> selectedRunAround(
    const std::vector<std::size_t>& selectedFrames,
    std::size_t currentFrame)
{
    const auto isSelected = [&selectedFrames](std::size_t frameIndex) {
        return std::binary_search(selectedFrames.begin(), selectedFrames.end(), frameIndex);
    };
    std::size_t first = currentFrame;
    std::size_t last = currentFrame;
    while (first > 0 && isSelected(first - 1)) {
        --first;
    }
    while (isSelected(last + 1)) {
        ++last;
    }
    return {first, last};
}

std::vector<ScrollFillFrame> makeScrollFill(const document::ProjectDocument& document,
                                            render::Resolution plane,
                                            std::size_t firstFrame,
                                            std::size_t lastFrame,
                                            const PlaneSelection* selection)
{
    const std::vector<uint8_t> romMask =
        selection ? selectionRomMask(document.geometry(), *selection) : std::vector<uint8_t>();
    const std::vector<uint8_t>* mask = selection ? &romMask : nullptr;
    const int romWidth = document.originalFrameSize().width;
    std::vector<ScrollFillFrame> result;
    if (lastFrame >= document.frameCount() || firstFrame + 1 >= lastFrame) {
        return result;
    }
    const int scale = document.geometry().planeScale(plane);
    std::optional<PlaneContent> first;
    std::optional<PlaneContent> last;
    if (hasPlane(document, firstFrame, plane)) {
        first = readContent(document, firstFrame, plane);
    }
    if (hasPlane(document, lastFrame, plane)) {
        last = readContent(document, lastFrame, plane);
    }
    const document::IndexedFrame& firstOriginal = document.originalFrame(firstFrame);
    const document::IndexedFrame& lastOriginal = document.originalFrame(lastFrame);
    for (std::size_t frameIndex = firstFrame + 1; frameIndex < lastFrame; ++frameIndex) {
        ScrollFillFrame entry;
        entry.frameIndex = frameIndex;
        entry.changes.plane = plane;
        const document::IndexedFrame& original = document.originalFrame(frameIndex);
        entry.fromFirst = findScroll(firstOriginal, original, mask);
        entry.fromLast = findScroll(lastOriginal, original, mask);
        if (hasPlane(document, frameIndex, plane) &&
            ((first && entry.fromFirst) || (last && entry.fromLast))) {
            PlaneContent content = readContent(document, frameIndex, plane);
            const auto apply = [&](const PlaneContent& reference, const ScrollMatch& match) {
                if (mask) {
                    applyPartScroll(reference, match, scale, romMask, romWidth, content);
                } else {
                    applyScroll(reference, match, scale, content);
                }
            };
            if (first && entry.fromFirst) {
                apply(*first, *entry.fromFirst);
            }
            if (last && entry.fromLast) {
                apply(*last, *entry.fromLast);
            }
            FrameContentBuilder builder(document, frameIndex, plane);
            for (int y = 0; y < content.size.height; ++y) {
                for (int x = 0; x < content.size.width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y) *
                                                  static_cast<std::size_t>(content.size.width) +
                                              static_cast<std::size_t>(x);
                    builder.setPixel(render::Point{x, y}, content.colors[index]);
                    if (!content.dynamicMask.empty()) {
                        builder.setDynamicMask(render::Point{x, y}, content.dynamicMask[index]);
                    }
                }
            }
            entry.changes = builder.changes();
        }
        result.push_back(std::move(entry));
    }
    return result;
}

}  // namespace colorizing::edit
