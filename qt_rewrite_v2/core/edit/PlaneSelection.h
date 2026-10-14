#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "render/CanvasTypes.h"
#include "render/ProjectGeometry.h"

namespace colorizing::edit {

// Pixels marked on one plane of a frame (the Win32 editor's Copy_Mask): what copy, auto copy,
// the selection fills and the selection helpers work on. Editor state, not project data.
class PlaneSelection {
public:
    PlaneSelection() = default;
    PlaneSelection(render::Resolution plane, render::Size size);

    [[nodiscard]] render::Resolution plane() const;
    [[nodiscard]] render::Size size() const;
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] std::size_t count() const;
    // False outside the plane.
    [[nodiscard]] bool contains(render::Point point) const;
    // One byte per plane pixel, row-major, 1 for a selected pixel.
    [[nodiscard]] const std::vector<uint8_t>& mask() const;
    // Row-major.
    [[nodiscard]] std::vector<render::Point> pixels() const;
    // The smallest rectangle around the selected pixels; nothing for an empty selection.
    [[nodiscard]] std::optional<render::Rect> bounds() const;

    // Points outside the plane are ignored. Each returns whether a pixel changed.
    bool add(const std::vector<render::Point>& points);
    bool remove(const std::vector<render::Point>& points);
    bool paint(const std::vector<render::Point>& points, bool removePoints);
    void selectAll();
    void clear();
    void invert();
    // The selection's pixels move to the mirrored place on the plane (Win32 Ctrl+K, Ctrl+L).
    void mirror(bool horizontally, bool vertically);
    // A mask of size() pixels; any value but 0 selects. Throws std::invalid_argument for
    // another size.
    void assignMask(const std::vector<uint8_t>& mask);
    void assignMask(const document::CompactArray<uint8_t>& mask);

private:
    render::Resolution m_plane = render::Resolution::Sd;
    render::Size m_size;
    std::vector<uint8_t> m_mask;
    std::size_t m_count = 0;
};

[[nodiscard]] bool operator==(const PlaneSelection& lhs, const PlaneSelection& rhs);

// Rows or columns in stripes (IDC_DOAUTOSEL): from offset on, selected rows (columns) are
// selected, then skipped ones are not, and so on to the edge.
struct StripePattern {
    int offset = 0;
    int selected = 1;
    int skipped = 1;
    bool columns = false;
};

// The Win32 limits: offset 0 to 256, selected and skipped 1 to 256.
[[nodiscard]] bool isValidStripePattern(const StripePattern& pattern);
// Throws std::invalid_argument for a pattern that is not valid.
[[nodiscard]] std::vector<uint8_t> stripeMask(render::Size size, const StripePattern& pattern);

// The pixels of the frame that have the color, row-major.
[[nodiscard]] std::vector<render::Point> pixelsOfColor(const document::Rgb565Frame& frame,
                                                       uint16_t color);
// The points whose pixel in the frame is not 0 (black); the Win32 editor's "W held" filter.
[[nodiscard]] std::vector<render::Point> nonBlackPixels(const document::Rgb565Frame& frame,
                                                        const std::vector<render::Point>& points);

// The ROM frame pixels a selection covers: a ROM pixel is covered when any plane pixel showing
// it is selected (ConvertMaskToSourceReso: on the doubled plane a 2x2 block counts as one).
// One byte per ROM frame pixel.
[[nodiscard]] std::vector<uint8_t> selectionRomMask(const render::ProjectGeometry& geometry,
                                                    const PlaneSelection& selection);

}  // namespace colorizing::edit
