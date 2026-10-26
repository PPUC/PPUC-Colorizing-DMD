#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Several sprites onto several frames at once, as the Win32 editor's Add Sprite did with more
// than one sprite selected (IDC_ADDSPRITE2, issue #38): a frame that has a sprite already keeps
// it in its slot; otherwise the sprite goes to the frame's first empty slot, with the whole
// frame as its bounds. A frame whose slots are all used gets no more.
struct SpritesAdded {
    // Sprites put on a frame, sprites a frame had already, sprites left out for want of a slot.
    std::size_t added = 0;
    std::size_t kept = 0;
    std::size_t full = 0;
    // Frames of scenes, which take no sprites (ProjectDocument::stripSceneFrame()).
    std::size_t sceneFrames = 0;
    // One command per frame that changed, applied already.
    std::vector<std::unique_ptr<EditCommand>> commands;
};
[[nodiscard]] SpritesAdded addSpritesToFrames(document::ProjectDocument& document,
                                              const std::vector<std::size_t>& frames,
                                              const std::vector<std::size_t>& sprites);

// How many of the frames addSpritesToFrames() would give more sprites than
// document::kSpritesPerFrameWarning, having had no more before.
[[nodiscard]] std::size_t framesPassingSpriteWarning(const document::ProjectDocument& document,
                                                     const std::vector<std::size_t>& frames,
                                                     const std::vector<std::size_t>& sprites);

}  // namespace colorizing::edit
