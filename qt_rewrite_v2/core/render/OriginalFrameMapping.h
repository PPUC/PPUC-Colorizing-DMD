#pragma once

#include "render/CanvasLayout.h"
#include "render/CanvasTypes.h"

namespace colorizing::render {

// How the colorized SD (128x32) and HD (256x64) frames relate to the ROM's original frame.
//
//   original 128x32: SD (x, y) shows original (x, y),   HD (x, y) shows original (x/2, y/2)
//   original 256x64: SD (x, y) shows original (2x, 2y), HD (x, y) shows original (x, y)
//
// The larger side is sampled at its top-left pixel, never averaged; that is what the Win32
// editor does and what the frame data is authored against.
//
// Sprites (their original pixels, detection areas and the bounds of a frame's sprite slots)
// always live on the SD grid, so for a 256x64 original they see it through toOriginal(Sd, ...).
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

    // The original pixel a pixel of the SD or HD frame shows.
    [[nodiscard]] Point toOriginal(Resolution resolution, Point framePixel) const;
    // The pixel of the SD or HD frame an original pixel belongs to. Where the frame is larger
    // than the original this is the top-left pixel of its block.
    [[nodiscard]] Point fromOriginal(Resolution resolution, Point originalPixel) const;
    // All original pixels a pixel of the SD or HD frame stands for: 2x2 for an SD pixel of a
    // 256x64 original, otherwise the one pixel toOriginal() returns.
    [[nodiscard]] Rect originalBlock(Resolution resolution, Point framePixel) const;
    // The smallest original rectangle that contains the blocks of an inclusive frame range.
    [[nodiscard]] Rect originalBlocks(Resolution resolution, Point minPixel, Point maxPixel) const;

private:
    // Original pixels per frame pixel along one axis when positive, frame pixels per original
    // pixel when negative.
    [[nodiscard]] int step(Resolution resolution) const;

    Size m_originalSize;
};

}  // namespace colorizing::render
