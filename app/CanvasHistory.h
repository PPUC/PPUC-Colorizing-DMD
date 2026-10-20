#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace colorizing::app {

// What the canvas shows: a frame, a background being edited or a sprite in the sprite editor.
struct CanvasPlace {
    enum class Kind { Frame, Background, Sprite };
    Kind kind = Kind::Frame;
    // The frame, the background or the sprite.
    std::size_t index = 0;
    // The current frame at the time (for a background or a sprite: the frame to come back to).
    std::size_t frame = 0;
    // The timeline at the time: the frame filter (its entry's data) and, for the frames of a
    // scene, the scene (-1: none).
    int filter = 0;
    int filterScene = -1;

    // The same canvas content: a background or a sprite is the same whatever the frame and the
    // timeline; a frame is the same frame in the same timeline.
    [[nodiscard]] bool sameContent(const CanvasPlace& other) const
    {
        return kind == other.kind && index == other.index &&
               (kind != Kind::Frame ||
                (filter == other.filter && filterScene == other.filterScene));
    }
};

// The toolbar's back and forward buttons, as in a web browser: the places the canvas showed.
// A place noted while going back or forward replaces nothing; a new place drops the ones ahead.
class CanvasHistory {
public:
    static constexpr std::size_t kMaxPlaces = 100;

    // A frame counts once it has rested (stepping through frames notes only where it stopped),
    // at once when the canvas comes back to the frames from a background or a sprite, or when
    // the caller says so (frameRested: another timeline).
    void note(const CanvasPlace& place, bool frameRested);
    void clear();

    [[nodiscard]] bool canGoBack() const { return m_position > 0 && !m_places.empty(); }
    [[nodiscard]] bool canGoForward() const { return m_position + 1 < m_places.size(); }
    // The place to show; the history moves to it. Places that no longer exist (exists() says
    // no: a deleted frame, background or sprite) are dropped on the way.
    using Exists = std::function<bool(const CanvasPlace&)>;
    std::optional<CanvasPlace> back(const Exists& exists);
    std::optional<CanvasPlace> forward(const Exists& exists);
    // Back to the latest place before the current one that matches (e.g. a timeline that is
    // not a scene's), past any number of others; they stay ahead for forward().
    std::optional<CanvasPlace> backTo(const Exists& exists, const Exists& matches);
    [[nodiscard]] std::optional<CanvasPlace> current() const;
    [[nodiscard]] std::size_t size() const { return m_places.size(); }

private:
    std::vector<CanvasPlace> m_places;
    std::size_t m_position = 0;
};

}  // namespace colorizing::app
