#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/FrameContentCommand.h"
#include "edit/PlaneSelection.h"

namespace colorizing::edit {

// CRC32 over the pixels of the original frame where romMask is not 0, in row-major order (the
// Win32 editor's crc32_fast_mask). romMask has the ROM frame's size.
[[nodiscard]] uint32_t maskedOriginalHash(const document::IndexedFrame& original,
                                          const std::vector<uint8_t>& romMask);

struct AutoCopyScan {
    // Frames whose original equals the source's inside the selection, pixel for pixel,
    // ascending, without the source frame.
    std::vector<std::size_t> matches;
    // Frames whose masked hash equals the source's; matches are taken from these only.
    std::size_t hashCandidates = 0;
    // Hash candidates whose pixels differ after all.
    std::size_t hashCollisions = 0;
    // Matches that have no colorized frame on the selection's plane and are left alone.
    std::size_t withoutPlane = 0;
};

// IDC_AUTOCOPY (AutoCopy): the frames whose original frame shows exactly what the source
// frame's shows inside the selection (on ROM frame pixels, see selectionRomMask()). The
// candidates are found by the masked hash, as the Win32 editor does, and then compared pixel
// for pixel. candidates: the frames to look at, ascending; all frames when empty.
[[nodiscard]] AutoCopyScan scanAutoCopy(const document::ProjectDocument& document,
                                        std::size_t sourceFrame,
                                        const PlaneSelection& selection,
                                        const std::vector<std::size_t>& candidates = {});

// The source frame's colorized pixels of the selection, and on the dynamic plane its dynamic
// mask there (AutoCopy copies cFrames and DynaMasks, not the dynamic colors), applied to the
// target frame. Empty changes for a target without a colorized frame on the plane.
[[nodiscard]] FrameContentChanges makeAutoCopyChanges(const document::ProjectDocument& document,
                                                      std::size_t sourceFrame,
                                                      std::size_t targetFrame,
                                                      const PlaneSelection& selection);

}  // namespace colorizing::edit
