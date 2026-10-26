#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "document/EditorMetadata.h"

namespace colorizing::legacy::crpx {

// The parts of the .cRPX layout (CrpxFile.h) its reader in the core and the deprecated writer of
// the tests (tests/legacy-writers) share. Not part of the API.

inline constexpr std::array<char, 8> kMagic = {'C', 'D', 'M', 'D', 'c', 'R', 'P', 'X'};

inline void putLittleEndian(uint8_t* out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i) {
        out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
}

inline void appendLittleEndian(std::string& out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

inline constexpr std::array<char, 4> kHelperLinesTag = {'H', 'L', 'I', 'N'};
inline constexpr std::array<char, 4> kPupPackTag = {'P', 'U', 'P', 'P'};
inline constexpr std::array<char, 4> kScenesTag = {'S', 'C', 'E', 'N'};
inline constexpr std::array<char, 4> kImageZonesTag = {'I', 'Z', 'O', 'N'};
inline constexpr std::array<char, 4> kFrameTagsTag = {'F', 'T', 'A', 'G'};
inline constexpr std::array<char, 4> kMoreSpritesTag = {'S', 'P', 'R', 'X'};
inline constexpr std::array<char, 4> kLightsTag = {'L', 'G', 'H', 'T'};
inline constexpr std::array<char, 4> kOriginalOnlySetsTag = {'D', 'S', 'S', 'D'};
inline constexpr std::array<char, 4> kLicenseTag = {'L', 'I', 'C', 'N'};
// Per effect of LGHT before its name, and per action.
inline constexpr std::size_t kLightEffectBytes = 28;
inline constexpr std::size_t kLightActionBytes = 8;
// Per sprite of SPRX: the name, the source frame, the rectangle, the mirrors.
inline constexpr std::size_t kMoreSpriteBytes = document::kSpriteNameBytes + 4 + 4 * 2 + 2 * 4;
inline constexpr std::size_t kSceneBytes = 16;
inline constexpr std::size_t kSceneFrameBytes = 12;
inline constexpr std::size_t kChunkHeaderBytes = 16;

}  // namespace colorizing::legacy::crpx
