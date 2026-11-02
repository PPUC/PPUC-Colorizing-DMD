#pragma once

#include <cstddef>
#include <optional>
#include <unordered_set>
#include <vector>

#include "document/ComponentId.h"
#include "document/ProjectDocument.h"

namespace colorizing::edit {

// Frames marked to be moved later, so that the place they go to can be looked for by any means,
// which changes the selection: a working aid of the open window, not saved and no undo step.
// They go by the frame's ID, so that a mark stays on its frame when others move or go; a deleted
// frame is no longer among the frames, and is again when an undo brings it back.
class FrameMoveMarks {
public:
    // The frames are the marked ones from now on.
    void mark(const document::ProjectDocument& document, const std::vector<std::size_t>& frames);
    void clear();
    [[nodiscard]] bool isEmpty() const { return m_ids.empty(); }
    // The marked frames the project has, ascending.
    [[nodiscard]] std::vector<std::size_t> frames(const document::ProjectDocument& document) const;

private:
    std::unordered_set<document::ComponentId, document::ComponentIdHash> m_ids;
};

// Where frames go that are moved before or behind a frame, as moveFrames() takes it. Behind the
// last frame of a section they stay in that section; before the first one they join it.
struct FrameMovePlace {
    std::size_t beforeFrame = 0;
    bool joinSection = true;
};
[[nodiscard]] FrameMovePlace frameMovePlace(std::size_t frame, bool behind);

// The frame to go on with after the frames (ascending) moved away: the first one behind the first
// of them that does not move, or else the last one before them. Its ID, as its number changes with
// the move; none when every frame moves or there are none.
[[nodiscard]] std::optional<document::ComponentId> frameLeftBehindByMove(
    const document::ProjectDocument& document, const std::vector<std::size_t>& frames);

}  // namespace colorizing::edit
