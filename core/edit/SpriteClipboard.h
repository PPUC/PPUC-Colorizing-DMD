#pragma once

#include <cstddef>
#include <memory>

#include "document/ProjectDocument.h"
#include "edit/CopyPaste.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Copy and paste on the sprite canvas, as the Win32 editor's sprite window (IDC_COPY,
// IDC_PASTE). The clipboard is the one of the frame canvas (CopyBuffer), so a sprite can be
// pasted into frames and a frame selection into a sprite.
//
// The plane is the one the sprite canvas shows: Sd or Hd of a project with two planes (Hd
// needs the sprite's HD data), Hd in an HD-only project, Sd in a 192x64 project.

// The sprite's pixels that are not transparent, cut to their bounding box, with their colors
// and shades, without dynamic content. Throws std::invalid_argument for a sprite without a
// pixel on the plane, std::logic_error for a plane the sprite has no picture on.
[[nodiscard]] CopyBuffer copySprite(const document::ProjectDocument& document,
                                    std::size_t spriteIndex,
                                    render::Resolution plane);

// Why the buffer cannot be pasted into a sprite on that plane, or nothing: it has to come from
// the same plane of a project of the same layout (the Win32 editor's Copy_Content) and fit
// into a sprite.
[[nodiscard]] std::optional<std::string> spritePasteRefusal(
    const document::ProjectDocument& document,
    std::size_t spriteIndex,
    const CopyBuffer& buffer,
    render::Resolution plane);

// Replaces the sprite's picture on the plane with the buffer's selected pixels at the sprite's
// top left corner, mirrored as asked; the rest of the sprite becomes transparent. A buffer
// copied from a frame makes that frame the sprite's source frame. Returns the command (in the
// sprite's history), already applied, or nullptr when nothing changed. Throws
// std::invalid_argument when spritePasteRefusal() gives a reason.
[[nodiscard]] std::unique_ptr<EditCommand> pasteIntoSprite(document::ProjectDocument& document,
                                                           std::size_t spriteIndex,
                                                           const CopyBuffer& buffer,
                                                           render::Resolution plane,
                                                           bool mirrorHorizontally,
                                                           bool mirrorVertically);

}  // namespace colorizing::edit
