#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Commands that hold the state of some data before and after an edit and put one or the other
// back. The edit itself runs through the document's own operations; the command is made from
// the states around it. Pixel planes are shared copy-on-write, so a state costs little.

// A sprite as a whole (pictures, detection areas, name, dynamic content) with the frame the
// cRP names as its source.
struct SpriteState {
    document::SpriteRecord sprite;
    uint32_t sourceFrame = 0;
};

[[nodiscard]] SpriteState spriteState(const document::ProjectDocument& document,
                                      std::size_t spriteIndex);

// In the sprite's attribute history. When the pictures differ, the sprite's pixel histories
// are dropped (replacedData).
class SpriteStateCommand final : public EditCommand {
public:
    SpriteStateCommand(std::size_t spriteIndex, SpriteState before, SpriteState after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;

private:
    std::size_t m_spriteIndex;
    SpriteState m_before;
    SpriteState m_after;
    bool m_replacesPictures;
};

// The sprite slots of a frame (sprite and bounds of each), in the frame's attribute history.
class FrameSpriteSlotsCommand final : public EditCommand {
public:
    FrameSpriteSlotsCommand(std::size_t frameIndex,
                            std::vector<document::FrameSpriteReference> before,
                            std::vector<document::FrameSpriteReference> after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_frameIndex;
    std::vector<document::FrameSpriteReference> m_before;
    std::vector<document::FrameSpriteReference> m_after;
};

// The color rotations of one plane of a frame, in the frame's attribute history.
class ColorRotationsCommand final : public EditCommand {
public:
    ColorRotationsCommand(std::size_t frameIndex,
                          render::Resolution resolution,
                          document::CompactArray<uint16_t> before,
                          document::CompactArray<uint16_t> after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_frameIndex;
    render::Resolution m_resolution;
    document::CompactArray<uint16_t> m_before;
    document::CompactArray<uint16_t> m_after;
};

// Several commands that are one step: applied in order, reverted in reverse order. It belongs
// to the history of the first command and names everything its commands name.
class CommandGroup final : public EditCommand {
public:
    // Throws std::invalid_argument for an empty list.
    explicit CommandGroup(std::vector<std::unique_ptr<EditCommand>> commands);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<RenumberedRecords> renumberedRecords() const override;
    [[nodiscard]] std::size_t heldBytes() const override;
    [[nodiscard]] const std::vector<std::unique_ptr<EditCommand>>& commands() const;

private:
    std::vector<std::unique_ptr<EditCommand>> m_commands;
};

// Each makes the command from the state before an edit that has run and the document's state
// now, or returns nullptr when the edit changed nothing.
[[nodiscard]] std::unique_ptr<EditCommand> spriteStateCommand(
    const document::ProjectDocument& document,
    std::size_t spriteIndex,
    SpriteState before);
[[nodiscard]] std::unique_ptr<EditCommand> frameSpriteSlotsCommand(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    std::vector<document::FrameSpriteReference> before);
[[nodiscard]] std::unique_ptr<EditCommand> colorRotationsCommand(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    render::Resolution resolution,
    document::CompactArray<uint16_t> before);
// The commands that are not nullptr as one step; nullptr when all are, the command itself
// when only one is.
[[nodiscard]] std::unique_ptr<EditCommand> groupCommands(
    std::vector<std::unique_ptr<EditCommand>> commands);

}  // namespace colorizing::edit
