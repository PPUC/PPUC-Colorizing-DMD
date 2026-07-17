#pragma once

#include <optional>
#include <vector>

#include "render/CanvasTypes.h"

namespace colorizing::render {

constexpr Size kSdFrameSize{128, 32};
constexpr Size kHdFrameSize{256, 64};

struct CanvasLayoutOptions {
    Resolution activeResolution = Resolution::Sd;
    bool showOriginal = true;
    bool showHdReference = false;
    bool hasHdFrame = false;
    int gap = 8;
};

struct CanvasRegionLayout {
    CanvasRegion region = CanvasRegion::ColorizedFrame;
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

[[nodiscard]] Size logicalSizeForResolution(Resolution resolution);
[[nodiscard]] int displayScaleForResolution(Resolution resolution);

}  // namespace colorizing::render

