#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "render/CanvasTypes.h"

namespace colorizing::edit {

enum class EditKind {
    FramePixels,
    ComparisonMask,
    DynamicMask,
    DynamicColor,
    DynamicShadow,
    BackgroundMask,
    BackgroundImage,
    SpriteImage,
    Palette,
    // Per-frame settings (comparison mask, shape mode, background, trigger, duration) and
    // creating or removing the frame's HD data. One history per frame for both resolutions.
    FrameAttributes,
    // Creating or removing a sprite's HD data. One history per sprite for both resolutions.
    SpriteAttributes,
    // Adding and deleting frames, sprites, backgrounds and comparison masks. One history for
    // the project (index 0), see StructureCommand.
    ProjectStructure,
    // The edit colors and color sets of the cRP. One history for the project (index 0), see
    // ColorSetEdits.
    EditorColors,
    // The sections of the cRP. One history for the project (index 0), see Sections.h.
    Sections,
    // The editor's helper lines (.cRPX). One history for the project (index 0), see
    // HelperLines.h.
    HelperLines,
};

// The records other data refers to by index.
enum class RecordKind {
    Frame,
    ComparisonMask,
    Background,
    Sprite,
};

struct RecordReference {
    RecordKind kind = RecordKind::Frame;
    std::size_t index = 0;
};

// The records of a kind from firstIndex on are removed, added or renumbered.
struct RenumberedRecords {
    RecordKind kind = RecordKind::Frame;
    std::size_t firstIndex = 0;
};

// What a command touches, so the UI knows what to refresh and which history owns the command.
struct EditTarget {
    EditKind kind = EditKind::FramePixels;
    // Frame, comparison mask, background, sprite or palette index, depending on the kind.
    std::size_t index = 0;
    // Always Sd for comparison masks, palettes and attributes, which have no resolution.
    render::Resolution resolution = render::Resolution::Sd;
    // Frames whose own data changed. Empty for masks shared between frames, backgrounds,
    // sprites and palettes.
    std::vector<std::size_t> frameIndices;
};

// Data a command replaces as a whole instead of editing it in place.
struct ReplacedData {
    EditKind kind = EditKind::FramePixels;
    std::size_t index = 0;
    render::Resolution resolution = render::Resolution::Sd;
};

class EditCommand {
public:
    virtual ~EditCommand() = default;

    virtual void apply(document::ProjectDocument& document) const = 0;
    virtual void revert(document::ProjectDocument& document) const = 0;
    [[nodiscard]] virtual EditTarget target() const = 0;
    // Commands recorded earlier for this data no longer describe it once this command ran in
    // either direction, so their histories have to be dropped.
    [[nodiscard]] virtual std::vector<ReplacedData> replacedData() const;
    // Every record the command names by index: the one it edits and each ID it writes into
    // another record, in either direction. A history is dropped when a later command
    // renumbers one of them, so a command is never run against another record than the one
    // it was made for. Pure, so that no command can forget to say.
    [[nodiscard]] virtual std::vector<RecordReference> recordReferences() const = 0;
    // What this command removes, adds or renumbers, whenever it runs in either direction.
    [[nodiscard]] virtual std::vector<RenumberedRecords> renumberedRecords() const;
    // The memory of record data the command holds while it is in a history (the records a
    // deletion took out), for UndoHistories' limit; 0 for the commands that hold only their
    // changes.
    [[nodiscard]] virtual std::size_t heldBytes() const;
};

struct PixelChange {
    int x = 0;
    int y = 0;
    uint16_t oldColor = 0;
    uint16_t newColor = 0;
};

// RGB565 pixels of a colorized frame, a background image or a sprite.
class PixelEditCommand final : public EditCommand {
public:
    // kind must be FramePixels, BackgroundImage or SpriteImage.
    PixelEditCommand(EditKind kind,
                     std::size_t index,
                     render::Resolution resolution,
                     std::vector<PixelChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    void write(document::ProjectDocument& document, bool useNewColor) const;

    EditKind m_kind;
    std::size_t m_index;
    render::Resolution m_resolution;
    std::vector<PixelChange> m_changes;
};

// Gives a background that has no HD image one; reverting removes the HD image again.
class BackgroundHdCreateCommand final : public EditCommand {
public:
    BackgroundHdCreateCommand(std::size_t backgroundIndex, document::Rgb565Frame frame);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_backgroundIndex;
    document::Rgb565Frame m_frame;
};

// Takes a background's HD image away; reverting puts it back.
class BackgroundHdRemoveCommand final : public EditCommand {
public:
    // frame: the HD image the background has.
    BackgroundHdRemoveCommand(std::size_t backgroundIndex, document::Rgb565Frame frame);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_backgroundIndex;
    document::Rgb565Frame m_frame;
};

struct MaskPixelChange {
    int x = 0;
    int y = 0;
    uint8_t oldValue = 0;
    uint8_t newValue = 0;
};

// Pixels of a comparison mask, or of a frame's dynamic or background mask.
class MaskEditCommand final : public EditCommand {
public:
    // kind must be ComparisonMask, DynamicMask or BackgroundMask. The resolution is ignored
    // for comparison masks.
    MaskEditCommand(EditKind kind,
                    std::size_t index,
                    render::Resolution resolution,
                    std::vector<MaskPixelChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    void write(document::ProjectDocument& document, bool useNewValue) const;

    EditKind m_kind;
    std::size_t m_index;
    render::Resolution m_resolution;
    std::vector<MaskPixelChange> m_changes;
};

class DynamicColorEditCommand final : public EditCommand {
public:
    DynamicColorEditCommand(std::size_t frameIndex,
                            render::Resolution resolution,
                            std::size_t setIndex,
                            std::size_t originalColorIndex,
                            uint16_t oldColor,
                            uint16_t newColor);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_frameIndex;
    render::Resolution m_resolution;
    std::size_t m_setIndex;
    std::size_t m_originalColorIndex;
    uint16_t m_oldColor;
    uint16_t m_newColor;
};

struct DynamicColorChange {
    std::size_t originalColorIndex = 0;
    uint16_t oldColor = 0;
    uint16_t newColor = 0;
};

// Several colors of one dynamic set of one frame in one step, e.g. the edit colors copied
// into the set. Same history as DynamicColorEditCommand.
class DynamicColorsEditCommand final : public EditCommand {
public:
    DynamicColorsEditCommand(std::size_t frameIndex,
                             render::Resolution resolution,
                             std::size_t setIndex,
                             std::vector<DynamicColorChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    void write(document::ProjectDocument& document, bool useNewColor) const;

    std::size_t m_frameIndex;
    render::Resolution m_resolution;
    std::size_t m_setIndex;
    std::vector<DynamicColorChange> m_changes;
};

struct DynamicShadowChange {
    uint8_t oldDirection = 0;
    uint8_t newDirection = 0;
    uint16_t oldColor = 0;
    uint16_t newColor = 0;
};

// One dynamic set's shadow direction and/or color on one frame.
class DynamicShadowEditCommand final : public EditCommand {
public:
    DynamicShadowEditCommand(std::size_t frameIndex,
                             render::Resolution resolution,
                             std::size_t setIndex,
                             bool editDirection,
                             bool editColor,
                             DynamicShadowChange change);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    void write(document::ProjectDocument& document, bool useNewValue) const;

    std::size_t m_frameIndex;
    render::Resolution m_resolution;
    std::size_t m_setIndex;
    bool m_editDirection;
    bool m_editColor;
    DynamicShadowChange m_change;
};

// Which dynamic sets of one frame color the SD picture only
// (ProjectDocument::originalOnlyDynamicSets()). In the frame's dynamic shadow history, beside
// the other settings of its sets.
class OriginalOnlyDynamicSetsCommand final : public EditCommand {
public:
    OriginalOnlyDynamicSetsCommand(std::size_t frameIndex, uint32_t before, uint32_t after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_frameIndex;
    uint32_t m_before;
    uint32_t m_after;
};

struct PaletteColorChange {
    std::size_t colorIndex = 0;
    uint16_t oldColor = 0;
    uint16_t newColor = 0;
};

class PaletteEditCommand final : public EditCommand {
public:
    PaletteEditCommand(std::size_t paletteIndex, std::vector<PaletteColorChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    void write(document::ProjectDocument& document, bool useNewColor) const;

    std::size_t m_paletteIndex;
    std::vector<PaletteColorChange> m_changes;
};

enum class FrameAttribute {
    ComparisonMaskId,
    ShapeComparisonMode,
    BackgroundId,
    PupTriggerId,
    SceneTriggerId,
    MonochromeTriggerId,
    DurationMs,
};

[[nodiscard]] uint32_t frameAttributeValue(const document::ProjectDocument& document,
                                           std::size_t frameIndex,
                                           FrameAttribute attribute);

// One per-frame setting of one frame.
class FrameAttributeEditCommand final : public EditCommand {
public:
    // Values are given as stored in the document. A background ID needs a frame with
    // background data.
    FrameAttributeEditCommand(std::size_t frameIndex,
                              FrameAttribute attribute,
                              uint32_t oldValue,
                              uint32_t newValue);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] FrameAttribute attribute() const;

private:
    std::size_t m_frameIndex;
    FrameAttribute m_attribute;
    uint32_t m_oldValue;
    uint32_t m_newValue;
};

// The tags of one frame (ProjectDocument::frameTags()), in the frame's settings history.
class FrameTagsCommand final : public EditCommand {
public:
    FrameTagsCommand(std::size_t frameIndex,
                     std::vector<std::string> before,
                     std::vector<std::string> after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_frameIndex;
    std::vector<std::string> m_before;
    std::vector<std::string> m_after;
};

// The DOF events of one frame (ProjectDocument::frameDofEvents()), in the frame's settings
// history.
class FrameDofEventsCommand final : public EditCommand {
public:
    FrameDofEventsCommand(std::size_t frameIndex,
                          document::DofEvents before,
                          document::DofEvents after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::size_t m_frameIndex;
    document::DofEvents m_before;
    document::DofEvents m_after;
};

// Create HD or Remove HD of a frame: swaps the frame's HD data, and the related HD data the
// states hold, between the state before and the state after.
class FrameHdCommand final : public EditCommand {
public:
    FrameHdCommand(std::size_t frameIndex,
                   document::FrameHdState before,
                   document::FrameHdState after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;

private:
    std::size_t m_frameIndex;
    document::FrameHdState m_before;
    document::FrameHdState m_after;
};

// Create HD or Remove HD of a sprite.
class SpriteHdCommand final : public EditCommand {
public:
    SpriteHdCommand(std::size_t spriteIndex,
                    document::SpriteHdState before,
                    document::SpriteHdState after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;

private:
    std::size_t m_spriteIndex;
    document::SpriteHdState m_before;
    document::SpriteHdState m_after;
};

// Each runs the operation on the document and returns the command for it, already applied,
// or nullptr when the operation changes nothing. When the operation fails the document is
// left as it was and the exception is passed on.
[[nodiscard]] std::unique_ptr<EditCommand> createFrameHd(document::ProjectDocument& document,
                                                         std::size_t frameIndex,
                                                         document::FrameHdParts parts);
[[nodiscard]] std::unique_ptr<EditCommand> removeFrameHd(document::ProjectDocument& document,
                                                         std::size_t frameIndex);
// The backgrounds that the frames show without an image of their own on the rendered plane
// of a 256x64 project (FrameHdGaps::background), ascending and unique: what
// FrameHdParts::backgroundImage gives a black image.
[[nodiscard]] std::vector<std::size_t> backgroundsWithoutHdImage(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices);
[[nodiscard]] std::unique_ptr<EditCommand> createSpriteHd(
    document::ProjectDocument& document,
    std::size_t spriteIndex,
    document::UpscaleFilter filter = document::UpscaleFilter::Nearest);
[[nodiscard]] std::unique_ptr<EditCommand> removeSpriteHd(document::ProjectDocument& document,
                                                          std::size_t spriteIndex);

}  // namespace colorizing::edit
