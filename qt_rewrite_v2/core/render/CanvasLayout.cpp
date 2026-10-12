#include "render/CanvasLayout.h"

#include <algorithm>

#include "render/OriginalFrameMapping.h"

namespace colorizing::render {

bool Rect::isEmpty() const
{
    return width <= 0 || height <= 0;
}

bool Rect::contains(Point point) const
{
    return !isEmpty() && point.x >= x && point.y >= y &&
           point.x < x + width && point.y < y + height;
}

Size logicalSizeForResolution(Resolution resolution)
{
    return resolution == Resolution::Hd ? kHdFrameSize : kSdFrameSize;
}

int displayScaleForResolution(Resolution resolution)
{
    return resolution == Resolution::Hd ? 1 : 2;
}

CanvasLayout::CanvasLayout(std::vector<CanvasRegionLayout> regions)
    : m_regions(std::move(regions))
{
    for (const auto& region : m_regions) {
        m_displaySize.width = std::max(m_displaySize.width,
                                       region.displayRect.x + region.displayRect.width);
        m_displaySize.height = std::max(m_displaySize.height,
                                        region.displayRect.y + region.displayRect.height);
    }
}

CanvasLayout CanvasLayout::buildFrameLayout(const CanvasLayoutOptions& options)
{
    std::vector<CanvasRegionLayout> regions;
    int y = 0;

    auto appendRegion = [&](CanvasRegion region,
                            Resolution resolution,
                            Size logicalSize,
                            int scale) {
        const Size displaySize{logicalSize.width * scale, logicalSize.height * scale};
        regions.push_back(CanvasRegionLayout{
            region,
            resolution,
            logicalSize,
            Rect{0, y, displaySize.width, displaySize.height},
            scale,
        });
        y += displaySize.height + options.gap;
    };

    auto appendFrameRegion = [&](CanvasRegion region, Resolution resolution) {
        appendRegion(region,
                     resolution,
                     logicalSizeForResolution(resolution),
                     displayScaleForResolution(resolution));
    };

    appendFrameRegion(CanvasRegion::ColorizedFrame, options.activeResolution);

    if (options.showOriginal) {
        const OriginalFrameMapping mapping(options.originalSize);
        appendRegion(CanvasRegion::OriginalFrame,
                     mapping.originalResolution(),
                     mapping.originalSize(),
                     mapping.originalDisplayScale());
    }

    if (options.showHdReference && options.hasHdFrame) {
        appendFrameRegion(CanvasRegion::HdFrame, Resolution::Hd);
    }

    return CanvasLayout(std::move(regions));
}

Size CanvasLayout::displaySize() const
{
    return m_displaySize;
}

const std::vector<CanvasRegionLayout>& CanvasLayout::regions() const
{
    return m_regions;
}

std::optional<CanvasHit> CanvasLayout::hitTest(Point point) const
{
    for (const auto& regionLayout : m_regions) {
        if (!regionLayout.displayRect.contains(point)) {
            continue;
        }

        const int localX = point.x - regionLayout.displayRect.x;
        const int localY = point.y - regionLayout.displayRect.y;
        return CanvasHit{
            regionLayout.region,
            regionLayout.resolution,
            localX / regionLayout.displayScale,
            localY / regionLayout.displayScale,
        };
    }
    return std::nullopt;
}

std::optional<CanvasRegionLayout> CanvasLayout::region(CanvasRegion region) const
{
    for (const auto& candidate : m_regions) {
        if (candidate.region == region) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace colorizing::render
