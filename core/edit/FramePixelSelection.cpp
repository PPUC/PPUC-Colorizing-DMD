#include "edit/FramePixelSelection.h"

namespace colorizing::edit {

FramePixelSelection::FramePixelSelection(render::Size size)
    : m_size(size)
{
}

bool FramePixelSelection::paint(std::size_t frameIndex,
                                const std::vector<render::Point>& points,
                                bool erase)
{
    const std::size_t width = static_cast<std::size_t>(m_size.width);
    if (!m_frameIndex || *m_frameIndex != frameIndex) {
        m_frameIndex = frameIndex;
        m_mask.assign(width * static_cast<std::size_t>(m_size.height), 0);
    }

    const uint8_t newValue = erase ? uint8_t{0} : uint8_t{1};
    bool changed = false;
    for (const render::Point& point : points) {
        if (point.x < 0 || point.y < 0 || point.x >= m_size.width || point.y >= m_size.height) {
            continue;
        }
        uint8_t& value =
            m_mask[static_cast<std::size_t>(point.y) * width + static_cast<std::size_t>(point.x)];
        if (value != newValue) {
            value = newValue;
            changed = true;
        }
    }
    return changed;
}

void FramePixelSelection::clear()
{
    m_frameIndex.reset();
    m_mask.clear();
}

std::optional<std::size_t> FramePixelSelection::frameIndex() const
{
    return m_frameIndex;
}

std::vector<render::Point> FramePixelSelection::pixels(
    std::optional<std::size_t> frameIndex) const
{
    std::vector<render::Point> pixels;
    if (!frameIndex || !m_frameIndex || *m_frameIndex != *frameIndex) {
        return pixels;
    }
    const std::size_t width = static_cast<std::size_t>(m_size.width);
    for (int y = 0; y < m_size.height; ++y) {
        for (int x = 0; x < m_size.width; ++x) {
            if (m_mask[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)]) {
                pixels.push_back(render::Point{x, y});
            }
        }
    }
    return pixels;
}

}  // namespace colorizing::edit
