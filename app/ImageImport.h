#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "render/CanvasTypes.h"

namespace colorizing::app {

// Importing a picture (an image file, the clipboard, a movie clip) into the selection of frames
// or into a background, as the Win32 editor's Images window does: a zone of the picture with
// the selection's proportions, adjusted (brightness, contrast, blur), scaled to the selection.

// The Win32 sliders: brightness -50..50 (adds 2.5 per step), contrast -15..20 (multiplies by
// 1 + 0.05 per step), blur 0..10 (a box blur of that many pixels; 0 and 1 none).
struct ImageAdjustments {
    int brightness = 0;
    int contrast = 0;
    int blur = 0;

    [[nodiscard]] bool isNone() const { return brightness == 0 && contrast == 0 && blur <= 1; }
};

// The picture with the adjustments, as the Win32 editor computes them (OpenCV: each channel
// times the contrast plus the brightness, then the box blur, rounded and clamped at the end).
[[nodiscard]] QImage adjustedImage(const QImage& picture, const ImageAdjustments& adjustments);

enum class ImageScaleFilter {
    // Each target pixel takes the source pixel it falls on (OpenCV INTER_NEAREST, the Win32
    // default).
    Nearest,
    // Filtered (Qt's smooth scaling): the colors of the source pixels it covers mixed.
    Smooth,
};

// The zone of the picture scaled to the size; the zone is clipped to the picture.
[[nodiscard]] QImage zonePicture(const QImage& picture,
                                 const QRect& zone,
                                 QSize size,
                                 ImageScaleFilter filter);

// The largest zone of the proportions (width to height) that fits the picture, in its middle:
// where the zone starts.
[[nodiscard]] QRect fittingZone(QSize picture, double proportions);
// A zone of the proportions, `width` picture pixels wide (kept between a few pixels and the
// largest that fits), centred at the point and moved inside the picture.
[[nodiscard]] QRect zoneAt(QSize picture, double proportions, int width, QPoint centre);
// Scroll copy: the zone of frame `index` of `count`, from the start zone to the end zone.
[[nodiscard]] QRect zoneBetween(const QRect& start, const QRect& end, std::size_t index,
                                std::size_t count);

// The changes that put the picture (the rect's size) into the plane at the rect, at the pixels
// the selection mask (the plane's size, row-major, 1 selected; empty: all) selects.
[[nodiscard]] std::vector<edit::PixelChange> importedPixelChanges(
    const document::Rgb565Frame& plane,
    const QImage& picture,
    const render::Rect& rect,
    const std::vector<uint8_t>& mask);

// A rect and a selection mask on one plane, carried over to a plane of another size (the HD
// plane is twice the SD plane): the rect scaled, and on the larger plane each pixel selected
// when the pixel under it is, on the smaller one when any of the pixels it covers is.
struct PlaneArea {
    render::Rect rect;
    std::vector<uint8_t> mask;
};
[[nodiscard]] PlaneArea areaOnPlane(const PlaneArea& area, render::Size from, render::Size to);

[[nodiscard]] uint16_t rgb565FromColor(QRgb color);

}  // namespace colorizing::app
