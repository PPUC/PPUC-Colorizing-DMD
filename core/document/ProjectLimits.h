#pragma once

#include <cstddef>
#include <cstdint>

namespace colorizing::document {

// What a project may hold: what libserum 2.9 and later read (cROM format version 1, header
// revision 21, 16-bit sprite and background IDs). Projects are made for that libserum only;
// projects of libserum 2.8 and the Win32 editor are read and get these limits.

// Sprite slots of a frame.
inline constexpr std::size_t kSpritesPerFrame = 128;
inline constexpr std::size_t kMaxSprites = 65535;
inline constexpr std::size_t kMaxBackgrounds = 65535;
// The cROM format version written.
inline constexpr uint32_t kCromFormatVersion = 1;

// The sprite slots of a frame in a cROM before format version 1 (libserum 2.8, the Win32
// editor), read and filled up to kSpritesPerFrame with empty slots.
inline constexpr std::size_t kLegacySpritesPerFrame = 32;
// More sprites than this on one frame can make libserum's sprite detection noticeably slow:
// the editor warns about it. Sprites work the same past it; the limit stays the recommendation,
// and going past it is for a single frame that cannot be done otherwise, never the rule.
inline constexpr std::size_t kSpritesPerFrameWarning = 32;

}  // namespace colorizing::document
