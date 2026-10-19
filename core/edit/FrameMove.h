#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Moves frames to another place in the project, in one step: the frames keep their order
// among themselves and go as one block before a frame, or behind the last one. What a frame
// is moves with it; the frames are renumbered. In the structure history
// ({ProjectStructure, 0, Sd}) with the additions and deletions, so they undo in order; every
// history that names a renumbered frame is dropped when it runs.
class FrameMoveCommand final : public EditCommand {
public:
    // newOrder as ProjectDocument::reorderFrames() takes it; the tables with the sections and
    // sprite source frames before and after the move.
    FrameMoveCommand(std::vector<std::size_t> newOrder,
                     std::vector<std::size_t> movedFrames,
                     document::RecordTables tablesBefore,
                     document::RecordTables tablesAfter);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<RenumberedRecords> renumberedRecords() const override;

    // The moved frames, ascending, as they are numbered in the state the command last left:
    // after the move once applied, before it once reverted.
    [[nodiscard]] std::vector<std::size_t> movedFrames() const;
    [[nodiscard]] bool isApplied() const;

private:
    std::vector<std::size_t> m_newOrder;
    std::vector<std::size_t> m_inverseOrder;
    // As numbered before the move.
    std::vector<std::size_t> m_movedFrames;
    document::RecordTables m_tablesBefore;
    document::RecordTables m_tablesAfter;
    mutable bool m_applied = true;
};

// Moves the frames (any set, in any order) before the frame `beforeFrame`, or behind the last
// frame when it is the frame count, and returns the command, already applied; nullptr when
// nothing would change. beforeFrame may be one of the moved frames: they then go before the
// first frame behind it that does not move.
//
// Sections, by their first frame:
// - a section whose frames all move goes with them;
// - a section whose first frame moves without all of its frames starts at its first frame
//   that stays;
// - with joinSection, frames moved right before the first frame of a section join that
//   section (it then starts at the first moved frame), unless they bring a section of their
//   own; without it (frames moved down behind the last frame of a section) they stay in the
//   section before;
// - otherwise the moved frames are in the section of the frame before them.
// Source frames of sprites follow their frames.
//
// Throws std::out_of_range for a frame that does not exist and std::logic_error when frames
// with an imported palette would no longer be the first frames.
[[nodiscard]] std::unique_ptr<FrameMoveCommand> moveFrames(
    document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices,
    std::size_t beforeFrame,
    bool joinSection = true);

}  // namespace colorizing::edit
