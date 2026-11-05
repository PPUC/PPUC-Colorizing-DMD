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

// Where a section starts whose first frame moves without all of its frames.
enum class SplitSectionStart {
    // At its first frame that stays.
    StaysWithFrames,
    // At its first frame, where that frame goes; the frames that stay belong to the section
    // before them then.
    GoesWithMovedFrames,
};

// The sections (by their index in the list) whose first frame moveFrames() with the same
// frames and beforeFrame would move without all of their frames: those for which
// SplitSectionStart makes a difference. Empty when the move would change nothing. Throws
// std::out_of_range as moveFrames() does.
[[nodiscard]] std::vector<std::size_t> sectionsSplitByMove(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices,
    std::size_t beforeFrame);

// Moves the frames (any set, in any order) before the frame `beforeFrame`, or behind the last
// frame when it is the frame count, and returns the command, already applied; nullptr when
// nothing would change. beforeFrame may be one of the moved frames: they then go before the
// first frame behind it that does not move.
//
// Sections, by their first frame:
// - a section whose frames all move goes with them;
// - a section whose first frame moves without all of its frames starts at its first frame
//   that stays, or with splitSections GoesWithMovedFrames at its first frame where that goes
//   (see sectionsSplitByMove());
// - with joinSection, frames moved right before the first frame of a section join that
//   section (it then starts at the first moved frame), unless they bring a section of their
//   own; without it (frames moved down behind the last frame of a section) they stay in the
//   section before;
// - otherwise the moved frames are in the section of the frame before them.
// Source frames of sprites follow their frames.
//
// Imported palettes go with their frames, see ProjectDocument::reorderFrames().
//
// The signing notice (ProjectDocument::signingNoticeFrame()), when it is the last frame, stays
// there: it is not moved, and frames moved behind the last frame go before it.
//
// Throws std::out_of_range for a frame that does not exist.
[[nodiscard]] std::unique_ptr<FrameMoveCommand> moveFrames(
    document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices,
    std::size_t beforeFrame,
    bool joinSection = true,
    SplitSectionStart splitSections = SplitSectionStart::StaysWithFrames);

}  // namespace colorizing::edit
