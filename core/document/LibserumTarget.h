#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace colorizing::document {

// The libserum a project is made for, chosen by its author. It decides the limits of the
// project and the cROM format a save writes: a project for libserum 2.8 plays on every
// libserum since 2.8, one for 3.0 needs libserum 3.0 or later. Later targets add features.
enum class LibserumTarget : uint8_t {
    Libserum28 = 0,
    Libserum30 = 1,
};

// What a target allows.
struct LibserumLimits {
    // Sprite slots of a frame (the cROM stores that many per frame).
    std::size_t spritesPerFrame = 32;
    std::size_t maxSprites = 255;
    std::size_t maxBackgrounds = 255;
    // The cROM format version written: 0 is header revision 19/20 with 8-bit sprite IDs; from
    // 1 on header revision 21 with the format version and the slots per frame, 16-bit IDs.
    uint32_t cromFormatVersion = 0;
};

// The sprite slots of a frame of the cROM before format version 1 (libserum 2.8).
constexpr std::size_t kLegacySpritesPerFrame = 32;
// The most sprite slots any target gives a frame.
constexpr std::size_t kMaxSpritesPerFrame = 128;
// More sprites than this on one frame can make libserum's sprite detection noticeably slow:
// the editor warns about it. Sprites work the same past it; the limit stays the recommendation,
// and going past it is for a single frame that cannot be done otherwise, never the rule.
constexpr std::size_t kSpritesPerFrameWarning = 32;

[[nodiscard]] constexpr LibserumLimits libserumLimits(LibserumTarget target)
{
    switch (target) {
        case LibserumTarget::Libserum30:
            // 16-bit IDs, 0xffff for none.
            return LibserumLimits{kMaxSpritesPerFrame, 65535, 65535, 1};
        case LibserumTarget::Libserum28:
            break;
    }
    return LibserumLimits{kLegacySpritesPerFrame, 255, 255, 0};
}

// What keeps a project from being one for a target: more sprites or backgrounds than it
// allows, frames with a sprite in a slot it does not have (the first of them).
struct LibserumTargetProblems {
    std::size_t sprites = 0;
    std::size_t backgrounds = 0;
    std::size_t framesWithMoreSprites = 0;
    std::size_t firstFrameWithMoreSprites = 0;
    // The target's limits.
    LibserumLimits limits;

    [[nodiscard]] bool tooManySprites() const { return sprites > limits.maxSprites; }
    [[nodiscard]] bool tooManyBackgrounds() const { return backgrounds > limits.maxBackgrounds; }
    [[nodiscard]] bool empty() const
    {
        return !tooManySprites() && !tooManyBackgrounds() && framesWithMoreSprites == 0;
    }
};

// "libserum 2.8", "libserum 3.0".
[[nodiscard]] inline std::string libserumTargetName(LibserumTarget target)
{
    return target == LibserumTarget::Libserum30 ? "libserum 3.0" : "libserum 2.8";
}

}  // namespace colorizing::document
