#pragma once

#include <memory>

#include "document/ProjectDocument.h"
#include "edit/StructureCommand.h"

namespace colorizing::edit {

// The signing notice: a frame of the project that libserum shows, in place of the frames it
// identifies, when the exported cROMc was changed after its author signed it (for 10 seconds
// from the first frame, then for 5 seconds every 5 to 10 minutes). The author draws whatever
// message they like on it, with every feature of a frame: HD, color rotations, a background or
// a background scene. libserum never identifies it. A project without one shows the words
// "COLORIZATION MODIFIED" over its frames instead.

// A new notice: a black original, and the words "COLORIZATION MODIFIED" in white on black on
// every plane the project shows, centered, twice as large on a plane that has the room.
[[nodiscard]] document::FrameRecord makeSigningNoticeFrame(
    const document::ProjectDocument& document);

// Adds the notice behind the last frame and returns the command, already applied; nullptr when
// the project has one already.
[[nodiscard]] std::unique_ptr<StructureCommand> addSigningNotice(
    document::ProjectDocument& document);

}  // namespace colorizing::edit
