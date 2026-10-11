#include "document/EditorMetadata.h"

#include <algorithm>
#include <stdexcept>

namespace colorizing::document {

EditorMetadata makeDefaultEditorMetadata()
{
    EditorMetadata metadata;
    metadata.activeColorSets.assign(kMaxColorSets, 0);
    metadata.colorSets.assign(kMaxColorSets * kColorSetColorCount, 0);
    metadata.colorSetNames.assign(kMaxColorSets * kColorSetNameBytes, 0);
    metadata.maskNames.assign(kMaxComparisonMasks * kMaskNameBytes, 0);
    metadata.sectionFirstFrames.assign(kMaxSections, 0);
    metadata.sectionNames.assign(kMaxSections * kSectionNameBytes, 0);
    metadata.spriteNames.assign(kMaxSprites * kSpriteNameBytes, 0);
    metadata.spriteSourceFrames.assign(kMaxSprites, 0);
    metadata.spriteSourceRects.assign(kMaxSprites * 4, 0);
    metadata.spriteSourceMirrors.assign(kMaxSprites * 2, 0);
    metadata.editColors.assign(kEditColorCount, 0);
    metadata.imagePositionNames.assign(kMaxImagePositions * kImagePositionNameBytes, 0);
    metadata.imagePositions.assign(kMaxImagePositions * kImagePositionValueCount, 0);
    metadata.paletteNames.assign(256 * kPaletteNameBytes, 0);
    metadata.pupPackName.assign(kPupPackNameBytes, 0);
    return metadata;
}

bool isValidEditorMetadata(const EditorMetadata& metadata, std::string* errorMessage)
{
    const EditorMetadata expected = makeDefaultEditorMetadata();
    const bool ok =
        metadata.activeColorSets.size() == expected.activeColorSets.size() &&
        metadata.colorSets.size() == expected.colorSets.size() &&
        metadata.colorSetNames.size() == expected.colorSetNames.size() &&
        metadata.maskNames.size() == expected.maskNames.size() &&
        metadata.sectionFirstFrames.size() == expected.sectionFirstFrames.size() &&
        metadata.sectionNames.size() == expected.sectionNames.size() &&
        metadata.spriteNames.size() == expected.spriteNames.size() &&
        metadata.spriteSourceFrames.size() == expected.spriteSourceFrames.size() &&
        metadata.spriteSourceRects.size() == expected.spriteSourceRects.size() &&
        metadata.spriteSourceMirrors.size() == expected.spriteSourceMirrors.size() &&
        metadata.editColors.size() == expected.editColors.size() &&
        metadata.imagePositionNames.size() == expected.imagePositionNames.size() &&
        metadata.imagePositions.size() == expected.imagePositions.size() &&
        metadata.paletteNames.size() == expected.paletteNames.size() &&
        metadata.pupPackName.size() == expected.pupPackName.size() &&
        metadata.importedPalettes.size() % kImportedPaletteBytes == 0 &&
        metadata.sectionCount <= kMaxSections &&
        metadata.imagePositionCount <= kMaxImagePositions;
    if (!ok && errorMessage) {
        *errorMessage = "editor metadata has invalid table sizes";
    }
    return ok;
}

std::string fixedName(const std::vector<uint8_t>& table,
                      std::size_t entryIndex,
                      std::size_t entryBytes)
{
    if (entryBytes == 0 || (entryIndex + 1) * entryBytes > table.size()) {
        throw std::out_of_range("name table entry out of range");
    }
    const auto begin = table.begin() + static_cast<std::ptrdiff_t>(entryIndex * entryBytes);
    const auto end = begin + static_cast<std::ptrdiff_t>(entryBytes);
    return std::string(begin, std::find(begin, end, uint8_t{0}));
}

void setFixedName(std::vector<uint8_t>& table,
                  std::size_t entryIndex,
                  std::size_t entryBytes,
                  const std::string& name)
{
    const std::string truncated = name.substr(0, entryBytes == 0 ? 0 : entryBytes - 1);
    if (fixedName(table, entryIndex, entryBytes) == truncated) {
        return;
    }
    const auto begin = table.begin() + static_cast<std::ptrdiff_t>(entryIndex * entryBytes);
    std::fill(begin, begin + static_cast<std::ptrdiff_t>(entryBytes), uint8_t{0});
    std::copy(truncated.begin(), truncated.end(), begin);
}

}  // namespace colorizing::document
