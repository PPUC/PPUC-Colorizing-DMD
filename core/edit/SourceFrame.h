#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "document/ProjectDocument.h"

namespace colorizing::edit {

// The Win32 editor's "To Source Frame" (IDC_CFTOOF, ConvertCFrameToNewOFrame): a frame's
// colorized picture on the original's plane, painted in the plain original colors only
// (plainOriginalColors()), read back as an original frame, each pixel the shade of its color.
// Authors paint such "fake frames" to add to the project: a frame no dump has, for sprites,
// detection areas, dynamic sequences or backgrounds. Nothing, with the reason in problem, when
// a pixel has another color or the frame has no picture on that plane.
[[nodiscard]] std::optional<document::IndexedFrame> sourceFrameFromColors(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    std::string* problem = nullptr);

// The duration the Win32 editor gives such a frame.
inline constexpr uint32_t kSourceFrameDurationMs = 100;

}  // namespace colorizing::edit
