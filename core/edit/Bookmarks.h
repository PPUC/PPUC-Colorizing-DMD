#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::edit {

// Bookmarks mark frames to come back to: unnamed, a working aid of whoever has the project open.
// They go by the frame's ID, so that a bookmark stays on its frame when others move or go, and
// comes back when a deleted frame does. No undo step, as the Images window's saved zones.

[[nodiscard]] bool isBookmarked(const document::ProjectDocument& document, std::size_t frameIndex);
// Sets or removes the frame's bookmark; returns whether it has one now.
bool toggleBookmark(document::ProjectDocument& document, std::size_t frameIndex);
// The frames with a bookmark, in the order of the frames.
[[nodiscard]] std::vector<std::size_t> bookmarkedFrames(const document::ProjectDocument& document);
// Of the frames (ascending), the next one after `from` or the one before it, starting over at
// the other end; `from` itself only when it is the only one. None for no frames.
[[nodiscard]] std::optional<std::size_t> adjacentBookmark(const std::vector<std::size_t>& frames,
                                                          std::size_t from,
                                                          bool forward);

}  // namespace colorizing::edit
