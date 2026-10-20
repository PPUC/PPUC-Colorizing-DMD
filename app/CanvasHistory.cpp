#include "CanvasHistory.h"

namespace colorizing::app {

void CanvasHistory::note(const CanvasPlace& place, bool frameRested)
{
    if (!m_places.empty()) {
        CanvasPlace& here = m_places[m_position];
        if (here.sameContent(place)) {
            // Still there; a background or a sprite keeps the frame to come back to up to date.
            here.frame = place.frame;
            return;
        }
        if (place.kind == CanvasPlace::Kind::Frame && here.kind == CanvasPlace::Kind::Frame &&
            !frameRested) {
            return;
        }
        m_places.resize(m_position + 1);
    }
    m_places.push_back(place);
    if (m_places.size() > kMaxPlaces) {
        m_places.erase(m_places.begin());
    }
    m_position = m_places.size() - 1;
}

void CanvasHistory::clear()
{
    m_places.clear();
    m_position = 0;
}

std::optional<CanvasPlace> CanvasHistory::back(const Exists& exists)
{
    while (canGoBack()) {
        --m_position;
        if (exists(m_places[m_position])) {
            return m_places[m_position];
        }
        // The one after it is where the search came from again.
        m_places.erase(m_places.begin() + static_cast<std::ptrdiff_t>(m_position));
    }
    return std::nullopt;
}

std::optional<CanvasPlace> CanvasHistory::forward(const Exists& exists)
{
    while (canGoForward()) {
        ++m_position;
        if (exists(m_places[m_position])) {
            return m_places[m_position];
        }
        m_places.erase(m_places.begin() + static_cast<std::ptrdiff_t>(m_position));
        --m_position;
    }
    return std::nullopt;
}

std::optional<CanvasPlace> CanvasHistory::current() const
{
    if (m_places.empty()) {
        return std::nullopt;
    }
    return m_places[m_position];
}

}  // namespace colorizing::app
