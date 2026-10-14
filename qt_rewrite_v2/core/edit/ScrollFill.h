#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/FrameContentCommand.h"

namespace colorizing::edit {

// How a frame's picture lies against a reference frame's in a scrolling, named after the
// camera as in the Win32 editor (AutoFillScrolling, master:ColorizingDMD.cpp 8020).
enum class ScrollDirection {
    // The frame's rows 0.. are the reference's rows shift..: content moved up.
    CameraDown,
    // The frame's rows shift.. are the reference's rows 0..: content moved down.
    CameraUp,
    // The frame's columns 0.. are the reference's columns shift..: content moved left.
    CameraRight,
    // The frame's columns shift.. are the reference's columns 0..: content moved right.
    CameraLeft,
};

struct ScrollMatch {
    ScrollDirection direction = ScrollDirection::CameraDown;
    // In ROM frame pixels, at least 1.
    int shift = 1;
};

// The first match in the Win32 order: down, up, right, left, each with the smallest shift
// from 1 on. The overlapping rows (columns) are compared exactly, whole rows (columns) at a
// time; Win32 compares their CRC32s. Both originals have the ROM frame's size.
[[nodiscard]] std::optional<ScrollMatch> findScroll(const document::IndexedFrame& reference,
                                                    const document::IndexedFrame& frame);

// The run of selected frames without a gap that holds the current frame (firstfr .. lastfr).
// selectedFrames ascending; a current frame that is not selected gives a run of itself.
[[nodiscard]] std::pair<std::size_t, std::size_t> selectedRunAround(
    const std::vector<std::size_t>& selectedFrames,
    std::size_t currentFrame);

struct ScrollFillFrame {
    std::size_t frameIndex = 0;
    // What the first and the last frame of the run gave, if anything.
    std::optional<ScrollMatch> fromFirst;
    std::optional<ScrollMatch> fromLast;
    FrameContentChanges changes;
};

// The frames strictly between first and last get the colorized pixels (and on the dynamic plane
// the dynamic mask) of the part they share with the first frame, then of the part they share
// with the last, which wins where both apply. Whole rows or columns are copied, shifted by the
// match times the plane's scale. Frames without a colorized frame on the plane, and references
// without one, take no part. Returns one entry per frame between the two, changes empty where
// nothing changes.
[[nodiscard]] std::vector<ScrollFillFrame> makeScrollFill(const document::ProjectDocument& document,
                                                          render::Resolution plane,
                                                          std::size_t firstFrame,
                                                          std::size_t lastFrame);

}  // namespace colorizing::edit
