#pragma once

#include <cstddef>
#include <cstdint>

#include "document/ProjectDocument.h"
#include "edit/DrawingTools.h"
#include "edit/FrameContentCommand.h"
#include "edit/PlaneSelection.h"

namespace colorizing::edit {

// Whether the plane pixel shows dynamic content: the dynamic mask of the ROM frame pixel it shows
// (on the dynamic plane, which is the native one) names a set.
[[nodiscard]] bool isDynamicPixel(const document::ProjectDocument& document,
                                  std::size_t frameIndex,
                                  render::Resolution plane,
                                  render::Point point);

// Alt+click with the fill tool (master:ColorizingDMD.cpp 23264): every pixel of the frame's
// colorized plane that has the color gets the source's color (the draw color, or in color set
// replace mode the edit color of its shade). Empty changes for a frame without the plane.
[[nodiscard]] FrameContentChanges makeRecolorChanges(const document::ProjectDocument& document,
                                                     std::size_t frameIndex,
                                                     render::Resolution plane,
                                                     uint16_t colorToReplace,
                                                     const PixelColorSource& newColor);

// Ctrl+O (21958): pixels whose ROM frame pixel has shade 0 become black (0). The ROM pixel is
// the one ProjectGeometry::toRomFrame() maps the plane pixel to, as libserum lays the planes
// out (Win32 takes the most frequent shade of a 2x2 block, ValuePlus2x2, when it edits 128x32
// over a 256x64 original, a plane this editor does not edit).
// area: the pixels to look at, all of the plane when null.
[[nodiscard]] FrameContentChanges makeBlackOriginalToBlackChanges(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    render::Resolution plane,
    const PlaneSelection* area);

// Ctrl+E / Alt+D (22056, 22111) on the dynamic plane: dynamic pixels of the area leave their
// zone (mask 255) and get the plain color of their original shade (plainOriginalColors, the
// Win32 MycRP.Palette). Empty changes for a frame without dynamic data.
[[nodiscard]] FrameContentChanges makeRemoveDynamicChanges(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    const PlaneSelection* area);

// Alt+A (22164) on the dynamic plane: the pixels of the area join the dynamic set.
[[nodiscard]] FrameContentChanges makeAssignDynamicSetChanges(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    const PlaneSelection* area,
    uint8_t dynamicSet);

}  // namespace colorizing::edit
