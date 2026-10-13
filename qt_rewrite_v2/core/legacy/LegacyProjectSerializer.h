#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "document/ProjectSnapshot.h"

namespace colorizing::legacy {

// Frames shorter than this are ignored at runtime unless they are colorized.
constexpr uint32_t kSkipFrameDurationMs = 15;

// Name and start offset of each block written, in file order. Used to locate differences.
using LegacySectionOffsets = std::vector<std::pair<std::string, std::size_t>>;

// Frame identification hash exactly as libserum computes it: CRC32 over the original frame,
// skipping pixels covered by the comparison mask and folding colors above 1 in shape mode.
[[nodiscard]] uint32_t legacyFrameHash(const document::IndexedFrame& original,
                                       const document::IndexedFrame* comparisonMask,
                                       bool shapeMode);

// Colors the Win32 editor uses for an uncolorized frame: an orange ramp over the original colors.
[[nodiscard]] std::vector<uint16_t> legacyOriginalColors(uint32_t colorCount);

// A frame counts as colorized once it differs from the plain original rendering or has dynamic
// content. Uncolorized frames shorter than kSkipFrameDurationMs are flagged inactive. The frame
// is judged on the project's native plane (render::ProjectGeometry::nativePlane()), by that
// plane's colorized frame and dynamic mask.
[[nodiscard]] bool isLegacyFrameActive(const document::ProjectFrameSnapshot& frame,
                                       const std::vector<uint16_t>& originalColors,
                                       render::Resolution nativePlane);

// Write complete files from the snapshot alone, in the current legacy layout.
[[nodiscard]] bool writeLegacyCrom(std::ostream& stream,
                                   const document::ProjectSnapshot& snapshot,
                                   std::string& errorMessage,
                                   LegacySectionOffsets* sections = nullptr);
[[nodiscard]] bool writeLegacyCrp(std::ostream& stream,
                                  const document::ProjectSnapshot& snapshot,
                                  std::string& errorMessage,
                                  LegacySectionOffsets* sections = nullptr);

}  // namespace colorizing::legacy
