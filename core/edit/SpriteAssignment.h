#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Several sprites onto several frames at once, as the Win32 editor's Add Sprite did with more
// than one sprite selected (IDC_ADDSPRITE2, issue #38). With `bounds` given (the slot the author
// set up for one of the sprites), the sprite goes to the frame's first empty slot with those
// bounds and that offset, unless a slot has the sprite with them already: a sprite can be in
// several slots, each with bounds of its own (issue #50). Without, a frame that has the sprite
// in any slot keeps it, and otherwise the new slot has the whole frame as its bounds. A frame
// whose slots are all used gets no more.
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
                                              const std::vector<std::size_t>& sprites,
                                              const std::optional<document::FrameSpriteReference>&
                                                  bounds = std::nullopt);

// How many of the frames addSpritesToFrames() would give more sprites than
// document::kSpritesPerFrameWarning, having had no more before.
[[nodiscard]] std::size_t framesPassingSpriteWarning(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& frames,
    const std::vector<std::size_t>& sprites,
    const std::optional<document::FrameSpriteReference>& bounds = std::nullopt);

}  // namespace colorizing::edit
