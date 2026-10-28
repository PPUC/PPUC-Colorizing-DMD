#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Adds or deletes records: frames, a sprite, a background or a comparison mask. The records
// are either in the document or held by the command, which moves them from one to the other
// and copies nothing. A deletion takes them out again each time it is applied, so a redo
// keeps what was edited in between.
//
// All structure commands share one history ({ProjectStructure, 0, Sd}) and are therefore
// undone strictly in reverse order: each one runs against the record numbering it was made
// for, which is why they name no record themselves (recordReferences() is empty). Every
// other history that names a record this command removes, adds or renumbers is dropped
// whenever the command runs, see renumberedRecords() and UndoHistories.
class StructureCommand : public EditCommand {
public:
    void apply(document::ProjectDocument& document) const final;
    void revert(document::ProjectDocument& document) const final;
    [[nodiscard]] EditTarget target() const final;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const final;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const final;
    [[nodiscard]] std::vector<RenumberedRecords> renumberedRecords() const final;

    [[nodiscard]] RecordKind recordKind() const;
    // False for a deletion.
    [[nodiscard]] bool addsRecords() const;
    // Ascending, as the records are numbered while they are in the document.
    [[nodiscard]] const std::vector<std::size_t>& recordIndices() const;
    // Whether the records are in the document now.
    [[nodiscard]] bool recordsPresent() const;
    // The frames whose assignment of the record was cleared when it was last taken out.
    [[nodiscard]] const std::vector<std::size_t>& unassignedFrames() const;

protected:
    // tablesWithout: the cRP tables of the state without the records, for a command that
    // adds them; a deletion works them out.
    StructureCommand(RecordKind kind,
                     std::vector<std::size_t> indices,
                     bool addsRecords,
                     std::optional<document::RecordTables> tablesWithout);

    // Move the records out of the document into the command and back. take() returns the
    // frames whose assignment it cleared.
    [[nodiscard]] virtual std::vector<std::size_t> take(
        document::ProjectDocument& document) const = 0;
    virtual void put(document::ProjectDocument& document) const = 0;

private:
    void takeRecords(document::ProjectDocument& document) const;
    void putRecords(document::ProjectDocument& document) const;

    RecordKind m_kind;
    std::vector<std::size_t> m_indices;
    bool m_addsRecords;
    std::optional<document::RecordTables> m_tablesWithout;
    mutable bool m_present;
    mutable std::vector<std::size_t> m_unassignedFrames;
};

// The frames among frameIndices that last less than durationMs, for "delete frames shorter
// than".
[[nodiscard]] std::vector<std::size_t> framesShorterThan(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices,
    uint32_t durationMs);

// Why the name cannot be given to a new sprite; empty if it can. A name has 1 to 31 bytes
// and is not used by another sprite.
[[nodiscard]] std::string spriteNameProblem(const document::ProjectDocument& document,
                                            const std::string& name);

// Each runs the operation on the document and returns the command for it, already applied.
// When the operation fails the document is left as it was and the exception is passed on:
// std::out_of_range for a record that does not exist, std::length_error at a limit (255
// sprites, 255 backgrounds, 64 comparison masks), std::logic_error when no frame would
// remain or the frame lacks the data a background needs, std::invalid_argument for a sprite
// name spriteNameProblem() objects to.
//
// deleteFrames() takes any set of frames, in any order, and returns nullptr for none.
[[nodiscard]] std::unique_ptr<StructureCommand> deleteFrames(
    document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices);
// The index a frame has after deleteFrames() deleted the frames given (any order); nothing for
// a frame among them.
[[nodiscard]] std::optional<std::size_t> frameIndexAfterDeletion(
    std::size_t frameIndex, const std::vector<std::size_t>& deletedFrames);
// Appends the frames behind the last one, e.g. those of
// ProjectDocument::makeUncolorizedFrame(); nullptr for none. Throws std::invalid_argument
// for a frame that does not fit the project's planes.
[[nodiscard]] std::unique_ptr<StructureCommand> addFrames(
    document::ProjectDocument& document,
    std::vector<document::FrameRecord> frames);
[[nodiscard]] std::unique_ptr<StructureCommand> addSprite(document::ProjectDocument& document,
                                                          const std::string& name,
                                                          std::size_t sourceFrameIndex);
[[nodiscard]] std::unique_ptr<StructureCommand> deleteSprite(document::ProjectDocument& document,
                                                             std::size_t spriteIndex);
[[nodiscard]] std::unique_ptr<StructureCommand> addBackgroundFromFrame(
    document::ProjectDocument& document,
    std::size_t frameIndex);
[[nodiscard]] std::unique_ptr<StructureCommand> deleteBackground(
    document::ProjectDocument& document,
    std::size_t backgroundIndex);
[[nodiscard]] std::unique_ptr<StructureCommand> addComparisonMask(
    document::ProjectDocument& document);
// Adds a comparison mask that covers the pixels where `mask` is not 0 and assigns it to the
// frames; undone, they get back the masks they had. Throws std::invalid_argument for a mask
// of another size than the ROM frame.
[[nodiscard]] std::unique_ptr<StructureCommand> addComparisonMaskForFrames(
    document::ProjectDocument& document,
    const document::IndexedFrame& mask,
    const std::vector<std::size_t>& frameIndices);
[[nodiscard]] std::unique_ptr<StructureCommand> deleteComparisonMask(
    document::ProjectDocument& document,
    std::size_t maskIndex);

}  // namespace colorizing::edit
