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
        metadata.importedPalettes.size() % kImportedPaletteBytes == 0 &&
        metadata.sectionCount <= kMaxSections &&
        metadata.imagePositionCount <= kMaxImagePositions;
    if (!ok && errorMessage) {
        *errorMessage = "editor metadata has invalid table sizes";
    }
    return ok;
}

std::string pupPackNameFromLegacyBytes(const uint8_t* bytes, std::size_t size)
{
    std::string name;
    const auto append = [&name](uint32_t code) {
        if (code < 0x80) {
            name.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            name.push_back(static_cast<char>(0xc0 | (code >> 6)));
            name.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code < 0x10000) {
            name.push_back(static_cast<char>(0xe0 | (code >> 12)));
            name.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            name.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            name.push_back(static_cast<char>(0xf0 | (code >> 18)));
            name.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            name.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            name.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    };
    for (std::size_t index = 0; index + 1 < size; index += 2) {
        uint32_t unit = bytes[index] | (bytes[index + 1] << 8);
        if (unit == 0) {
            break;
        }
        if (unit >= 0xd800 && unit < 0xdc00 && index + 3 < size) {
            const uint32_t low = bytes[index + 2] | (bytes[index + 3] << 8);
            if (low >= 0xdc00 && low < 0xe000) {
                unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
                index += 2;
            }
        }
        // A lone surrogate becomes U+FFFD.
        append(unit >= 0xd800 && unit < 0xe000 ? 0xfffd : unit);
    }
    return name;
}

std::vector<uint8_t> legacyPupPackNameBytes(const std::string& name)
{
    std::vector<uint16_t> units;
    for (std::size_t index = 0; index < name.size();) {
        const auto byte = static_cast<uint8_t>(name[index]);
        const std::size_t length = byte < 0x80 ? 1 : byte >= 0xf0 ? 4 : byte >= 0xe0 ? 3
                                 : byte >= 0xc0 ? 2 : 1;
        uint32_t code = length == 1 ? byte : byte & (0x7f >> length);
        bool valid = length == 1 ? byte < 0x80 : index + length <= name.size();
        for (std::size_t next = 1; valid && next < length; ++next) {
            const auto continuation = static_cast<uint8_t>(name[index + next]);
            valid = (continuation & 0xc0) == 0x80;
            code = (code << 6) | (continuation & 0x3f);
        }
        index += valid ? length : 1;
        if (!valid || code > 0x10ffff) {
            code = 0xfffd;
        }
        if (code >= 0x10000) {
            units.push_back(static_cast<uint16_t>(0xd800 + ((code - 0x10000) >> 10)));
            units.push_back(static_cast<uint16_t>(0xdc00 + ((code - 0x10000) & 0x3ff)));
        } else {
            units.push_back(static_cast<uint16_t>(code));
        }
    }
    // Room for the NUL, and no half of a surrogate pair at the end.
    std::size_t kept = std::min(units.size(), kPupPackNameBytes / 2 - 1);
    if (kept > 0 && kept < units.size() && units[kept - 1] >= 0xd800 && units[kept - 1] < 0xdc00) {
        --kept;
    }
    std::vector<uint8_t> bytes(kPupPackNameBytes, 0);
    for (std::size_t index = 0; index < kept; ++index) {
        bytes[2 * index] = static_cast<uint8_t>(units[index] & 0xff);
        bytes[2 * index + 1] = static_cast<uint8_t>(units[index] >> 8);
    }
    return bytes;
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
