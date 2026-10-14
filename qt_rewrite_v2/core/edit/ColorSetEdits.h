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

// How shapes, points and fills color the frame (DrawColMode of the Win32 editor). The palette
// gradient, the Win32 editor's third mode, is the Grad tool here and ignores this mode.
enum class DrawColorMode {
    SingleColor,
    // Each painted pixel takes the edit color of its original shade ("4 color mode").
    ColorSetReplace,
};

// The cRP stores 0 (single color), 1 (color set replace) or 2 (gradient). The Win32 loader
// turns 2 into 0, and so does this.
[[nodiscard]] DrawColorMode drawColorModeFromStored(uint32_t stored);
[[nodiscard]] uint32_t storedDrawColorMode(DrawColorMode mode);

// Replaces the edit colors and color sets as a whole: the before and after state of one user
// action. Names no record.
class EditorColorsCommand final : public EditCommand {
public:
    EditorColorsCommand(document::ColorSetTables before, document::ColorSetTables after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    document::ColorSetTables m_before;
    document::ColorSetTables m_after;
};

// A command from the document's tables to after, not applied, or nullptr when they are equal.
[[nodiscard]] std::unique_ptr<EditCommand> makeEditorColorsCommand(
    const document::ProjectDocument& document,
    document::ColorSetTables after);

// The changes the color set dialog of the Win32 editor (ColSet_Proc) and the toolbar make, as
// new tables. colorCount is the project's color count: only that many colors of a set are
// stored or loaded, the others stay as they are.
[[nodiscard]] document::ColorSetTables withEditColor(document::ColorSetTables tables,
                                                     std::size_t slot,
                                                     uint16_t color);
[[nodiscard]] document::ColorSetTables withEditColors(document::ColorSetTables tables,
                                                      const std::vector<uint16_t>& colors);
// "Set": the set takes the edit colors and becomes active.
[[nodiscard]] document::ColorSetTables withStoredColorSet(document::ColorSetTables tables,
                                                          std::size_t setIndex,
                                                          uint32_t colorCount);
// A click on an active set: the edit colors take its colors. An inactive set changes nothing.
[[nodiscard]] document::ColorSetTables withLoadedColorSet(document::ColorSetTables tables,
                                                          std::size_t setIndex,
                                                          uint32_t colorCount);
// The name is cut to 63 bytes. The entry is rewritten (zero filled behind the name) only when
// the name differs, so an untouched entry keeps the bytes a file left behind its name.
[[nodiscard]] document::ColorSetTables withColorSetName(document::ColorSetTables tables,
                                                        std::size_t setIndex,
                                                        const std::string& name);
// Deactivating keeps the colors and the name, as an inactive set of a file has them.
[[nodiscard]] document::ColorSetTables withColorSetActive(document::ColorSetTables tables,
                                                          std::size_t setIndex,
                                                          bool active);

[[nodiscard]] std::vector<uint16_t> colorSetColors(const document::ColorSetTables& tables,
                                                   std::size_t setIndex);
[[nodiscard]] std::string colorSetName(const document::ColorSetTables& tables,
                                       std::size_t setIndex);

// The pipette with Shift: the edit color slot among the first colorCount whose color is
// color; the last one when several are (the Win32 loop keeps the last match).
[[nodiscard]] std::optional<std::size_t> matchingEditColorSlot(
    const std::vector<uint16_t>& editColors,
    uint32_t colorCount,
    uint16_t color);

// "Edit -> Dyn" (IDC_COLTODYNA): one command per frame that has dynamic data and a color that
// changes, setting the first colorCount colors of the dynamic set on the plane to the edit
// colors. Not applied.
[[nodiscard]] std::vector<std::unique_ptr<EditCommand>> makeEditColorsToDynamicSetCommands(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices,
    render::Resolution plane,
    std::size_t setIndex);
// "Dyn -> Edit" (IDC_DYNATOCOL): the edit colors take the colors of the frame's dynamic set.
[[nodiscard]] document::ColorSetTables withEditColorsFromDynamicSet(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    render::Resolution plane,
    std::size_t setIndex);

}  // namespace colorizing::edit
