#include "edit/PlaneSelection.h"

#include <algorithm>
#include <stdexcept>

namespace colorizing::edit {

namespace {

std::size_t pixelIndex(render::Size size, render::Point point)
{
    return static_cast<std::size_t>(point.y) * static_cast<std::size_t>(size.width) +
           static_cast<std::size_t>(point.x);
}

bool isInside(render::Size size, render::Point point)
{
    return point.x >= 0 && point.y >= 0 && point.x < size.width && point.y < size.height;
}

}  // namespace

PlaneSelection::PlaneSelection(render::Resolution plane, render::Size size)
    : m_plane(plane),
      m_size(size),
      m_mask(static_cast<std::size_t>(std::max(size.width, 0)) *
                 static_cast<std::size_t>(std::max(size.height, 0)),
             0)
{
}

render::Resolution PlaneSelection::plane() const
{
    return m_plane;
}

render::Size PlaneSelection::size() const
{
    return m_size;
}

bool PlaneSelection::isEmpty() const
{
    return m_count == 0;
}

std::size_t PlaneSelection::count() const
{
    return m_count;
}

bool PlaneSelection::contains(render::Point point) const
{
    return isInside(m_size, point) && m_mask[pixelIndex(m_size, point)] != 0;
}

const std::vector<uint8_t>& PlaneSelection::mask() const
{
    return m_mask;
}

std::vector<render::Point> PlaneSelection::pixels() const
{
    std::vector<render::Point> points;
    points.reserve(m_count);
    for (int y = 0; y < m_size.height; ++y) {
        for (int x = 0; x < m_size.width; ++x) {
            if (m_mask[pixelIndex(m_size, render::Point{x, y})] != 0) {
                points.push_back(render::Point{x, y});
            }
        }
    }
    return points;
}

std::optional<render::Rect> PlaneSelection::bounds() const
{
    if (m_count == 0) {
        return std::nullopt;
    }
    int minX = m_size.width;
    int minY = m_size.height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < m_size.height; ++y) {
        for (int x = 0; x < m_size.width; ++x) {
            if (m_mask[pixelIndex(m_size, render::Point{x, y})] != 0) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    return render::Rect{minX, minY, maxX - minX + 1, maxY - minY + 1};
}

bool PlaneSelection::add(const std::vector<render::Point>& points)
{
    return paint(points, false);
}

bool PlaneSelection::remove(const std::vector<render::Point>& points)
{
    return paint(points, true);
}

bool PlaneSelection::paint(const std::vector<render::Point>& points, bool removePoints)
{
    const uint8_t value = removePoints ? uint8_t{0} : uint8_t{1};
    bool changed = false;
    for (const render::Point& point : points) {
        if (!isInside(m_size, point)) {
            continue;
        }
        uint8_t& pixel = m_mask[pixelIndex(m_size, point)];
        if (pixel != value) {
            pixel = value;
            m_count = removePoints ? m_count - 1 : m_count + 1;
            changed = true;
        }
    }
    return changed;
}

void PlaneSelection::selectAll()
{
    std::fill(m_mask.begin(), m_mask.end(), uint8_t{1});
    m_count = m_mask.size();
}

void PlaneSelection::clear()
{
    std::fill(m_mask.begin(), m_mask.end(), uint8_t{0});
    m_count = 0;
}

void PlaneSelection::invert()
{
    for (uint8_t& pixel : m_mask) {
        pixel = pixel != 0 ? uint8_t{0} : uint8_t{1};
    }
    m_count = m_mask.size() - m_count;
}

void PlaneSelection::mirror(bool horizontally, bool vertically)
{
    std::vector<uint8_t> mirrored(m_mask.size(), 0);
    for (int y = 0; y < m_size.height; ++y) {
        for (int x = 0; x < m_size.width; ++x) {
            const int sourceX = horizontally ? m_size.width - 1 - x : x;
            const int sourceY = vertically ? m_size.height - 1 - y : y;
            mirrored[pixelIndex(m_size, render::Point{x, y})] =
                m_mask[pixelIndex(m_size, render::Point{sourceX, sourceY})];
        }
    }
    m_mask = std::move(mirrored);
}

void PlaneSelection::assignMask(const std::vector<uint8_t>& mask)
{
    if (mask.size() != m_mask.size()) {
        throw std::invalid_argument("selection mask has the wrong size");
    }
    m_count = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        m_mask[i] = mask[i] != 0 ? uint8_t{1} : uint8_t{0};
        m_count += m_mask[i];
    }
}

bool operator==(const PlaneSelection& lhs, const PlaneSelection& rhs)
{
    return lhs.plane() == rhs.plane() && lhs.size() == rhs.size() && lhs.mask() == rhs.mask();
}

bool isValidStripePattern(const StripePattern& pattern)
{
    return pattern.offset >= 0 && pattern.offset <= 256 && pattern.selected >= 1 &&
           pattern.selected <= 256 && pattern.skipped >= 1 && pattern.skipped <= 256;
}

std::vector<uint8_t> stripeMask(render::Size size, const StripePattern& pattern)
{
    if (!isValidStripePattern(pattern)) {
        throw std::invalid_argument(
            "auto selection needs an offset of 0 to 256 and 1 to 256 lines selected and skipped");
    }
    std::vector<uint8_t> mask(static_cast<std::size_t>(size.width) *
                                  static_cast<std::size_t>(size.height),
                              0);
    const int lineCount = pattern.columns ? size.width : size.height;
    int line = pattern.offset;
    while (line < lineCount) {
        for (int run = 0; run < pattern.selected && line < lineCount; ++run, ++line) {
            if (pattern.columns) {
                for (int y = 0; y < size.height; ++y) {
                    mask[pixelIndex(size, render::Point{line, y})] = 1;
                }
            } else {
                for (int x = 0; x < size.width; ++x) {
                    mask[pixelIndex(size, render::Point{x, line})] = 1;
                }
            }
        }
        line += pattern.skipped;
    }
    return mask;
}

std::vector<render::Point> pixelsOfColor(const document::Rgb565Frame& frame, uint16_t color)
{
    std::vector<render::Point> points;
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            if (frame.pixels[pixelIndex(frame.size, render::Point{x, y})] == color) {
                points.push_back(render::Point{x, y});
            }
        }
    }
    return points;
}

std::vector<render::Point> nonBlackPixels(const document::Rgb565Frame& frame,
                                          const std::vector<render::Point>& points)
{
    std::vector<render::Point> result;
    for (const render::Point& point : points) {
        if (isInside(frame.size, point) && frame.pixels[pixelIndex(frame.size, point)] != 0) {
            result.push_back(point);
        }
    }
    return result;
}

std::vector<uint8_t> selectionRomMask(const render::ProjectGeometry& geometry,
                                      const PlaneSelection& selection)
{
    const render::Size romSize = geometry.romFrameSize();
    std::vector<uint8_t> romMask(static_cast<std::size_t>(romSize.width) *
                                     static_cast<std::size_t>(romSize.height),
                                 0);
    const render::Size size = selection.size();
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            if (!selection.mask()[pixelIndex(size, render::Point{x, y})]) {
                continue;
            }
            const render::Point rom = geometry.toRomFrame(selection.plane(), render::Point{x, y});
            if (isInside(romSize, rom)) {
                romMask[pixelIndex(romSize, rom)] = 1;
            }
        }
    }
    return romMask;
}

}  // namespace colorizing::edit
