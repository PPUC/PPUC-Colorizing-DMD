#pragma once

#include "CanvasEditController.h"
#include "edit/DrawingGeometry.h"

namespace colorizing::app {

// How dynamic zones are painted on the frame canvas (Settings -> Appearance -> Dynamic Zones).
//
// The right button paints the zones of the selected dynamic set on the colorized picture
// whatever the setting, with Shift erasing, as in the Win32 editor 3.17.3. The left button
// follows the setting:
//
// - by default `Dynamic` is a mode: with it on, the left button paints zones too, on both
//   pictures, and colors are painted with `Dynamic` off; `Mask` and `Dynamic` are not on
//   together;
// - with leftPaintsColors (the Win32 editor's way), `Dynamic` only shows the zones: the left
//   button always paints colors, and `Mask` can be on with it, the comparison mask on the
//   original and the zones and colors on the colorized picture.
struct DynamicZoneOptions {
    bool leftPaintsColors = false;
    // The zones are drawn (and painted) on the original too, not only on the colorized
    // picture. While the comparison mask is shown, the original shows the mask alone.
    bool onOriginal = true;

    friend bool operator==(const DynamicZoneOptions&, const DynamicZoneOptions&) = default;
};

// Where the canvases put a zone's set number: one place per area, the pixels of a set that
// touch each other, also at a corner (a diagonal stroke is one area, issue #76), at the area's
// first pixel from the top left. sets holds a set per pixel, row by row, 255 for none.
struct DynamicSetLabel {
    render::Point at;
    int set = 0;
};
[[nodiscard]] std::vector<DynamicSetLabel> dynamicSetLabels(const std::vector<int>& sets,
                                                            int width,
                                                            int height);

[[nodiscard]] DynamicZoneOptions loadDynamicZoneOptions();
void saveDynamicZoneOptions(const DynamicZoneOptions& options);

// What the toolbar toggles and the paint target leave on, for a canvas event.
struct CanvasModes {
    bool comparisonMask = false;
    // The Dynamic toggle: the zones are shown.
    bool dynamicZones = false;
    bool backgroundMask = false;
    bool backgroundImage = false;
};

// The stroke under way on the canvas, if any.
enum class CanvasStroke {
    None,
    Left,
    Right,
};

// The layer a canvas event edits. onColorized: the stroke started on the colorized picture.
[[nodiscard]] CanvasEditLayer canvasEditLayer(const CanvasModes& modes,
                                              const DynamicZoneOptions& options,
                                              CanvasStroke stroke,
                                              bool onColorized);

// Whether the Mask and Dynamic toggles can be on together.
[[nodiscard]] bool maskWithDynamicZones(const DynamicZoneOptions& options);

// Whether the dynamic zones are drawn on the original, and painted there.
[[nodiscard]] bool dynamicZonesOnOriginal(const CanvasModes& modes,
                                          const DynamicZoneOptions& options);

// Whether the tool paints dynamic zones with the right button: the points, lines and shapes,
// the flood fill and the pipette, which picks the set. Not the selection or the gradient.
[[nodiscard]] bool paintsDynamicZonesWithRightButton(edit::DrawingTool tool);

}  // namespace colorizing::app
