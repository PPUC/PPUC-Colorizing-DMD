#pragma once

#include <cstdint>
#include <string>

#include "render/CanvasTypes.h"

namespace colorizing::document {

// A helper line of the editor: a named horizontal or vertical line over the canvas, to align
// and measure. It lies on the border before pixel `position` of `plane` (the top edge of a
// row, the left edge of a column), drawn on every frame, on the original and both planes
// scaled to theirs. Editor data only, kept in the .cRPX (not in a .cRP, nothing libserum
// reads).
struct HelperLine {
    std::string name;
    bool vertical = false;
    uint32_t position = 0;
    render::Resolution plane = render::Resolution::Sd;
    bool visible = true;

    friend bool operator==(const HelperLine& a, const HelperLine& b)
    {
        return a.name == b.name && a.vertical == b.vertical && a.position == b.position &&
               a.plane == b.plane && a.visible == b.visible;
    }
    friend bool operator!=(const HelperLine& a, const HelperLine& b) { return !(a == b); }
};

}  // namespace colorizing::document
