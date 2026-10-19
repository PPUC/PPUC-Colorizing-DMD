#include "edit/ColorSetEdits.h"

#include <algorithm>
#include <stdexcept>

namespace colorizing::edit {

namespace {

using document::ColorSetTables;
using document::kColorSetColorCount;
using document::kColorSetNameBytes;
using document::kEditColorCount;
using document::kMaxColorSets;

void requireSet(std::size_t setIndex)
{
    if (setIndex >= kMaxColorSets) {
        throw std::out_of_range("color set index out of range");
    }
}

std::size_t usedColorCount(uint32_t colorCount)
{
    return std::min<std::size_t>(colorCount, kColorSetColorCount);
}

}  // namespace

DrawColorMode drawColorModeFromStored(uint32_t stored)
{
    return stored == 1 ? DrawColorMode::ColorSetReplace : DrawColorMode::SingleColor;
}

uint32_t storedDrawColorMode(DrawColorMode mode)
{
    return mode == DrawColorMode::ColorSetReplace ? 1 : 0;
}

EditorColorsCommand::EditorColorsCommand(ColorSetTables before, ColorSetTables after)
    : m_before(std::move(before)),
      m_after(std::move(after))
{
}

void EditorColorsCommand::apply(document::ProjectDocument& document) const
{
    document.setColorSetTables(m_after);
}

void EditorColorsCommand::revert(document::ProjectDocument& document) const
{
    document.setColorSetTables(m_before);
}

EditTarget EditorColorsCommand::target() const
{
    return EditTarget{EditKind::EditorColors, 0, render::Resolution::Sd, {}};
}

std::vector<RecordReference> EditorColorsCommand::recordReferences() const
{
    return {};
}

std::unique_ptr<EditCommand> makeEditorColorsCommand(const document::ProjectDocument& document,
                                                     ColorSetTables after)
{
    ColorSetTables before = document.colorSetTables();
    if (before == after) {
        return nullptr;
    }
    return std::make_unique<EditorColorsCommand>(std::move(before), std::move(after));
}

ColorSetTables withEditColor(ColorSetTables tables, std::size_t slot, uint16_t color)
{
    if (slot >= tables.editColors.size()) {
        throw std::out_of_range("edit color slot out of range");
    }
    tables.editColors[slot] = color;
    return tables;
}

ColorSetTables withEditColors(ColorSetTables tables, const std::vector<uint16_t>& colors)
{
    if (colors.size() > tables.editColors.size()) {
        throw std::invalid_argument("too many edit colors");
    }
    std::copy(colors.begin(), colors.end(), tables.editColors.begin());
    return tables;
}

ColorSetTables withStoredColorSet(ColorSetTables tables, std::size_t setIndex, uint32_t colorCount)
{
    requireSet(setIndex);
    tables.activeColorSets[setIndex] = 1;
    const std::size_t count = usedColorCount(colorCount);
    std::copy_n(tables.editColors.begin(),
                count,
                tables.colorSets.begin() +
                    static_cast<std::ptrdiff_t>(setIndex * kColorSetColorCount));
    return tables;
}

ColorSetTables withLoadedColorSet(ColorSetTables tables, std::size_t setIndex, uint32_t colorCount)
{
    requireSet(setIndex);
    if (tables.activeColorSets[setIndex] == 0) {
        return tables;
    }
    const std::size_t count = usedColorCount(colorCount);
    std::copy_n(tables.colorSets.begin() +
                    static_cast<std::ptrdiff_t>(setIndex * kColorSetColorCount),
                count,
                tables.editColors.begin());
    return tables;
}

ColorSetTables withColorSetName(ColorSetTables tables,
                                std::size_t setIndex,
                                const std::string& name)
{
    requireSet(setIndex);
    document::setFixedName(tables.colorSetNames, setIndex, kColorSetNameBytes, name);
    return tables;
}

ColorSetTables withColorSetActive(ColorSetTables tables, std::size_t setIndex, bool active)
{
    requireSet(setIndex);
    // Any other non-zero value a file has counts as active and is left alone.
    if ((tables.activeColorSets[setIndex] != 0) != active) {
        tables.activeColorSets[setIndex] = active ? 1 : 0;
    }
    return tables;
}

std::vector<uint16_t> colorSetColors(const ColorSetTables& tables, std::size_t setIndex)
{
    requireSet(setIndex);
    const auto begin =
        tables.colorSets.begin() + static_cast<std::ptrdiff_t>(setIndex * kColorSetColorCount);
    return std::vector<uint16_t>(begin, begin + static_cast<std::ptrdiff_t>(kColorSetColorCount));
}

std::string colorSetName(const ColorSetTables& tables, std::size_t setIndex)
{
    requireSet(setIndex);
    return document::fixedName(tables.colorSetNames, setIndex, kColorSetNameBytes);
}

std::optional<std::size_t> matchingEditColorSlot(const std::vector<uint16_t>& editColors,
                                                 uint32_t colorCount,
                                                 uint16_t color)
{
    std::optional<std::size_t> slot;
    const std::size_t count = std::min<std::size_t>(colorCount, editColors.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (editColors[index] == color) {
            slot = index;
        }
    }
    return slot;
}

std::vector<std::unique_ptr<EditCommand>> makeEditColorsToDynamicSetCommands(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices,
    render::Resolution plane,
    std::size_t setIndex)
{
    const std::size_t count = usedColorCount(document.colorCount());
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (std::size_t frameIndex : frameIndices) {
        if (!document.dynamicData(frameIndex)) {
            continue;
        }
        std::vector<DynamicColorChange> changes;
        for (std::size_t colorIndex = 0; colorIndex < count; ++colorIndex) {
            const uint16_t oldColor =
                document.dynamicColor(frameIndex, plane, setIndex, colorIndex);
            const uint16_t newColor = document.editColor(colorIndex);
            if (oldColor != newColor) {
                changes.push_back(DynamicColorChange{colorIndex, oldColor, newColor});
            }
        }
        if (!changes.empty()) {
            commands.push_back(std::make_unique<DynamicColorsEditCommand>(
                frameIndex, plane, setIndex, std::move(changes)));
        }
    }
    return commands;
}

ColorSetTables withEditColorsFromDynamicSet(const document::ProjectDocument& document,
                                            std::size_t frameIndex,
                                            render::Resolution plane,
                                            std::size_t setIndex)
{
    ColorSetTables tables = document.colorSetTables();
    if (!document.dynamicData(frameIndex)) {
        return tables;
    }
    const std::size_t count = usedColorCount(document.colorCount());
    for (std::size_t colorIndex = 0; colorIndex < count && colorIndex < kEditColorCount;
         ++colorIndex) {
        tables.editColors[colorIndex] =
            document.dynamicColor(frameIndex, plane, setIndex, colorIndex);
    }
    return tables;
}

}  // namespace colorizing::edit
