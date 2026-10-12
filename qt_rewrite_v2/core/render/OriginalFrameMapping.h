#pragma once

#include "render/CanvasLayout.h"
#include "render/CanvasTypes.h"

namespace colorizing::render {

// How the rendered planes relate to the ROM's original frame.
//
//   original 128x32: SD (x, y) shows original (x, y), HD (x, y) shows original (x/2, y/2)
//   original 256x64: HD (x, y) shows original (x, y); nothing else is rendered
//
// A plane is never smaller than the original and nothing is downscaled, as in libserum: a
// 256x64 project has no SD plane. Its stored 128x32 data is carried, not mapped.
//
// Sprite originals, detection areas and the bounds of a frame's sprite slots are original
// pixels. A sprite found at an original position is drawn on a plane at planeScale() times
// that position and size.
class OriginalFrameMapping {
public:
    // Only the two sizes above are supported; see isSupportedOriginalSize().
    explicit OriginalFrameMapping(Size originalSize = kSdFrameSize);

    [[nodiscard]] static bool isSupportedOriginalSize(Size size);

    [[nodiscard]] Size originalSize() const;
    // Canvas hits, previews and overlays on the original region carry this as their
    // resolution: it names the grid of the original, Sd for 128x32 and Hd for 256x64.
    [[nodiscard]] Resolution originalResolution() const;
    // Display pixels per original pixel, so that the original is shown as large as an SD frame.
    [[nodiscard]] int originalDisplayScale() const;

    [[nodiscard]] bool hasPlane(Resolution resolution) const;
    // Plane pixels per original pixel along one axis. Throws std::logic_error for a plane the
    // project does not have, as toOriginal() does.
    [[nodiscard]] int planeScale(Resolution resolution) const;
    // The original pixel a pixel of the plane shows.
    [[nodiscard]] Point toOriginal(Resolution resolution, Point planePixel) const;

private:
    Size m_originalSize;
};

}  // namespace colorizing::render
