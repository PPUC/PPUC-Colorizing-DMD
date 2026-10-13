#pragma once

#include <optional>
#include <vector>

#include "render/CanvasTypes.h"
#include "render/ProjectGeometry.h"

namespace colorizing::render {

struct CanvasLayoutOptions {
    Resolution activeResolution = Resolution::Sd;
    bool showOriginal = true;
    bool showHdReference = false;
    bool hasHdFrame = false;
    int gap = 8;
    // activeResolution has to be an editable plane of it.
    ProjectGeometry geometry;
};

struct CanvasRegionLayout {
    CanvasRegion region = CanvasRegion::ColorizedFrame;
    // For the original region: the native plane, see ProjectGeometry::nativePlane().
    Resolution resolution = Resolution::Sd;
    Size logicalSize;
    Rect displayRect;
    int displayScale = 1;
};

class CanvasLayout {
public:
    [[nodiscard]] static CanvasLayout buildFrameLayout(const CanvasLayoutOptions& options);

    [[nodiscard]] Size displaySize() const;
    [[nodiscard]] const std::vector<CanvasRegionLayout>& regions() const;
    [[nodiscard]] std::optional<CanvasHit> hitTest(Point point) const;
    [[nodiscard]] std::optional<CanvasRegionLayout> region(CanvasRegion region) const;

private:
    explicit CanvasLayout(std::vector<CanvasRegionLayout> regions);

    std::vector<CanvasRegionLayout> m_regions;
    Size m_displaySize;
};

}  // namespace colorizing::render

