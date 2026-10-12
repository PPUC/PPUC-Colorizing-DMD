#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "render/CanvasTypes.h"

namespace colorizing::edit {

// Pixels marked by hand on one frame, e.g. the area a sprite is captured from. The selection
// belongs to the frame it was painted on and is not part of the document.
class FramePixelSelection {
public:
    explicit FramePixelSelection(render::Size size);

    // Marks or unmarks points on frameIndex; a selection on another frame is dropped first, even
    // if nothing gets marked. Points outside the size are ignored. Returns whether a pixel
    // changed.
    bool paint(std::size_t frameIndex, const std::vector<render::Point>& points, bool erase);
    void clear();

    // The frame painted on last, also when no pixel is marked any more.
    [[nodiscard]] std::optional<std::size_t> frameIndex() const;
    // The marked pixels in row-major order; none unless frameIndex is the selection's frame.
    [[nodiscard]] std::vector<render::Point> pixels(std::optional<std::size_t> frameIndex) const;

private:
    render::Size m_size;
    std::optional<std::size_t> m_frameIndex;
    std::vector<uint8_t> m_mask;
};

}  // namespace colorizing::edit
