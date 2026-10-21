#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace colorizing::legacy {

struct LegacyCromMetadata {
    std::string romName;
    uint32_t headerLengthBytes = 0;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;
    uint32_t hdFrameWidth = 0;
    uint32_t hdFrameHeight = 0;
    uint32_t frameCount = 0;
    uint32_t colorCount = 0;
    uint32_t comparisonMaskCount = 0;
    uint32_t spriteCount = 0;
    uint16_t backgroundCount = 0;
    bool modernRgb565Format = false;
    // Revision 20 projects can keep 256x64 original frames next to 128x32 colorized ones.
    bool originalIs256x64 = false;
    // Revision 21 on: the cROM format version (0 without it), and from version 1 on the
    // sprite slots of a frame, whose sprite IDs are then 16 bit (8 bit before).
    uint32_t formatVersion = 0;
    uint32_t spritesPerFrame = 32;
};

// The newest cROM format version the editor reads and writes.
constexpr uint32_t kNewestCromFormatVersion = 1;

struct LegacyCromParseResult {
    LegacyCromMetadata metadata;
    std::string errorMessage;
    std::size_t nextOffset = 0;
    bool ok = false;
};

[[nodiscard]] LegacyCromParseResult parseLegacyCromMetadata(const std::vector<uint8_t>& bytes);
[[nodiscard]] LegacyCromParseResult parseLegacyCromMetadata(const uint8_t* bytes,
                                                            std::size_t size);

}  // namespace colorizing::legacy

