#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::app {

struct GifExportOptions {
    // The plane the frames are taken from; every frame must have it.
    render::Resolution plane = render::Resolution::Sd;
    // Picture pixels per frame pixel; from 3 on, one row and column of each is a black gap,
    // as between the dots of a DMD.
    int pixelScale = 5;
    // With the frame's background, as the canvas's background layer shows it.
    bool includeBackground = true;
};

struct GifExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    // Pictures written: one per frame and colour rotation step, less the merged ones.
    std::size_t pictures = 0;
};

// The steps of a frame's colour rotations within its duration: 0 and every multiple of a
// running slot's delay below the duration, ascending.
[[nodiscard]] std::vector<uint32_t> colorRotationSteps(const document::ProjectDocument& document,
                                                       std::size_t frameIndex,
                                                       render::Resolution plane,
                                                       uint32_t durationMs);

// Writes the frames, in the given order, as an animated GIF of the pictures the editor
// composes (composeResolvedFrame) with their colour rotations stepping as the editor's preview
// steps them (applyColorRotations); rotations start again with every frame. A picture shows for
// its frame's duration, or until the next rotation step; GIF counts time in 1/100 s, so the
// times are rounded as they add up, and a picture that would show less than 2/100 s (which
// viewers stretch to about 1/10 s) is left out and its time given to the next one. progress
// counts frames (stage ExportingFrames); a set cancel stops writing.
[[nodiscard]] GifExportResult writeAnimatedGif(const document::ProjectDocument& document,
                                               const std::vector<std::size_t>& frameIndices,
                                               const GifExportOptions& options,
                                               std::ostream& stream,
                                               legacy::LegacyIoProgress* progress = nullptr);

}  // namespace colorizing::app
