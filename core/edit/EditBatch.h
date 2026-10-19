#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "edit/EditCommand.h"

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::edit {

// Edits of any number of frames and records, made one after the other and undone as one step
// under a name (the editor API's edit batch, for plugins). It lives in the history of the
// structure commands, as the steps may add or delete records, and so it is undone in strict
// order with them. Whenever it runs it drops the other histories of the data its steps edit:
// a later edit of such a frame no longer finds the state it was made for once the batch is
// undone.
class EditBatchCommand final : public EditCommand {
public:
    // Throws std::invalid_argument for an empty list or a null step.
    EditBatchCommand(std::string name, std::vector<std::unique_ptr<EditCommand>> steps);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<RenumberedRecords> renumberedRecords() const override;
    [[nodiscard]] std::size_t heldBytes() const override;

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] std::size_t stepCount() const;
    // Whether a step adds, deletes or renumbers records.
    [[nodiscard]] bool changesStructure() const;

private:
    std::string m_name;
    std::vector<std::unique_ptr<EditCommand>> m_steps;
};

// Collects the steps of an edit batch while they are made: each step is applied when it is
// added, so later steps and reads see it.
class EditBatch {
public:
    explicit EditBatch(std::string name);

    // A step that has been applied; nullptr (nothing changed) is left out.
    void add(std::unique_ptr<EditCommand> step);
    [[nodiscard]] bool isEmpty() const;
    // The batch as one command, or nullptr when no step changed anything.
    [[nodiscard]] std::unique_ptr<EditBatchCommand> finish();
    // Takes the steps back, the last first.
    void cancel(document::ProjectDocument& document);

private:
    std::string m_name;
    std::vector<std::unique_ptr<EditCommand>> m_steps;
};

// Steps for an edit batch. Each runs on the document and returns its command, already
// applied, or nullptr when nothing changed. They throw std::out_of_range for a frame or record
// that does not exist and std::invalid_argument for data of the wrong size, and leave the
// document as it was then.

// The frame's colorized pixels on every plane it has get the fill color, or without one the look
// of the original in its plain colors (as a frame fresh from a dump), except where keep (ROM
// frame size, row-major) is not 0. An HD pixel follows the ROM pixel under it.
[[nodiscard]] std::unique_ptr<EditCommand> resetPixels(document::ProjectDocument& document,
                                                       std::size_t frameIndex,
                                                       const std::vector<uint8_t>& keep,
                                                       std::optional<uint16_t> fill);
// The frame's dynamic zones on the document's dynamicPlane(): the set of each pixel (0 to 31),
// 255 for none.
[[nodiscard]] std::unique_ptr<EditCommand> setDynamicZones(document::ProjectDocument& document,
                                                           std::size_t frameIndex,
                                                           const std::vector<uint8_t>& sets);
// The frame's background, document::kNoBackgroundId for none. Throws std::logic_error for a
// frame without background data.
[[nodiscard]] std::unique_ptr<EditCommand> setFrameBackground(document::ProjectDocument& document,
                                                              std::size_t frameIndex,
                                                              uint16_t backgroundId);
// Removes every color rotation of the frame, on both planes.
[[nodiscard]] std::unique_ptr<EditCommand> clearColorRotations(
    document::ProjectDocument& document,
    std::size_t frameIndex);

}  // namespace colorizing::edit
