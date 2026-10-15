#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace colorizing::document {

constexpr std::size_t kMaxColorSets = 64;
constexpr std::size_t kColorSetColorCount = 16;
constexpr std::size_t kColorSetNameBytes = 64;
constexpr std::size_t kMaxComparisonMasks = 64;
constexpr std::size_t kMaskNameBytes = 32;
constexpr std::size_t kMaxSections = 512;
constexpr std::size_t kSectionNameBytes = 32;
constexpr std::size_t kMaxSprites = 255;
constexpr std::size_t kSpriteNameBytes = 32;
constexpr std::size_t kEditColorCount = 16;
constexpr std::size_t kMaxImagePositions = 256;
constexpr std::size_t kImagePositionNameBytes = 64;
constexpr std::size_t kImagePositionValueCount = 16;
constexpr std::size_t kPaletteNameBytes = 64;
constexpr std::size_t kImportedPaletteBytes = 64 * 3;
// The Win32 editor's PuP pack name field: 256 UTF-16 code units (its wchar_t).
constexpr std::size_t kPupPackNameBytes = 256 * 2;

// Editor-only project state stored in the .cRP next to the original frames. The fixed-size name
// tables are kept as raw bytes so an unedited project serializes back to identical bytes.
struct EditorMetadata {
    std::vector<uint32_t> activeColorSets;
    std::vector<uint16_t> colorSets;
    uint8_t currentColorSet = 0;
    uint8_t firstDisplayedColorSet = 0;
    std::vector<uint8_t> colorSetNames;
    uint32_t drawColorMode = 0;
    uint8_t drawMode = 0;
    int32_t maskSelectionMode = 0;
    uint32_t fillMode = 0;
    std::vector<uint8_t> maskNames;
    uint32_t sectionCount = 0;
    std::vector<uint32_t> sectionFirstFrames;
    std::vector<uint8_t> sectionNames;
    std::vector<uint8_t> spriteNames;
    std::vector<uint32_t> spriteSourceFrames;
    std::vector<uint16_t> spriteSourceRects;
    std::vector<uint32_t> spriteSourceMirrors;
    std::vector<uint16_t> editColors;
    uint32_t imagePositionCount = 0;
    std::vector<uint8_t> imagePositionNames;
    std::vector<int32_t> imagePositions;
    std::vector<uint8_t> paletteNames;
    // 64 RGB888 colors per frame imported from a pre-RGB565 project.
    std::vector<uint8_t> importedPalettes;
    uint32_t timeElapsedMs = 0;
    uint32_t hasPupPack = 0;
    // UTF-8. A .cRP holds it in UTF-16 (legacyPupPackNameBytes()), a .cRPX in UTF-8.
    std::string pupPackName;
};

[[nodiscard]] EditorMetadata makeDefaultEditorMetadata();
[[nodiscard]] bool isValidEditorMetadata(const EditorMetadata& metadata,
                                         std::string* errorMessage = nullptr);

// The PuP pack name of the Win32 editor's field (UTF-16LE up to the first NUL) in UTF-8, and
// back: the field's kPupPackNameBytes, the name cut to 255 code units, then zeros.
[[nodiscard]] std::string pupPackNameFromLegacyBytes(const uint8_t* bytes, std::size_t size);
[[nodiscard]] std::vector<uint8_t> legacyPupPackNameBytes(const std::string& name);

// Reads a NUL-terminated name out of a table of fixed-size entries.
[[nodiscard]] std::string fixedName(const std::vector<uint8_t>& table,
                                    std::size_t entryIndex,
                                    std::size_t entryBytes);
// Stores a name into a table entry. The entry is left untouched when it already holds that name.
void setFixedName(std::vector<uint8_t>& table,
                  std::size_t entryIndex,
                  std::size_t entryBytes,
                  const std::string& name);

}  // namespace colorizing::document
