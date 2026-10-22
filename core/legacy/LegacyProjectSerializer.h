#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"

namespace colorizing::legacy {

// Frames shorter than this are ignored at runtime unless they are colorized.
constexpr uint32_t kSkipFrameDurationMs = 15;

// Name and start offset of each block written, in file order. Used to locate differences.
using LegacySectionOffsets = std::vector<std::pair<std::string, std::size_t>>;

// Whether a cROM block of that name holds data of the extra plane (the ...X arrays and the
// "has extra" flags).
[[nodiscard]] bool isLegacyExtraPlaneSection(const std::string& name);

// Frame identification hash exactly as libserum computes it: CRC32 over the original frame,
// skipping pixels covered by the comparison mask and folding colors above 1 in shape mode.
[[nodiscard]] uint32_t legacyFrameHash(const document::IndexedFrame& original,
                                       const document::IndexedFrame* comparisonMask,
                                       bool shapeMode);

// Colors the Win32 editor uses for an uncolorized frame: an orange ramp over the original colors.
[[nodiscard]] std::vector<uint16_t> legacyOriginalColors(uint32_t colorCount);

// A frame counts as colorized once it differs from the plain original rendering or has dynamic
// content. Uncolorized frames shorter than kSkipFrameDurationMs are flagged inactive. The frame
// is judged on the project's native plane (render::ProjectGeometry::nativePlane()), by that
// plane's colorized frame and dynamic mask.
[[nodiscard]] bool isLegacyFrameActive(const document::ProjectFrameSnapshot& frame,
                                       const std::vector<uint16_t>& originalColors,
                                       render::Resolution nativePlane);
// The same rule on the parts it reads: the native plane's colorized frame (nullptr if the
// frame has none) and dynamic mask (nullptr without dynamic data).
[[nodiscard]] bool isLegacyFrameActive(uint32_t durationMs,
                                       const document::IndexedFrame& original,
                                       const document::Rgb565Frame* nativeColorized,
                                       const document::IndexedFrame* nativeDynamicMask,
                                       const std::vector<uint16_t>& originalColors);

// Whether the runtime shows nothing new for the frame as a save writes it: it is flagged
// inactive (isLegacyFrameActive(): shorter than kSkipFrameDurationMs and not colorized) and
// has nothing libserum renders an inactive frame for either, an assigned background or a
// dynamic zone. libserum identifies such a frame and keeps the picture it shows.
[[nodiscard]] bool isFrameSkippedAtRuntime(const document::ProjectDocument& document,
                                           std::size_t frameIndex);

struct LegacyIoProgress;

// The blocks of a cROM after its header, in file order (libserum's Serum_Block). Each element
// of a block belongs to one frame, comparison mask, sprite or background.
enum class CromBlock {
    FrameHash,
    FrameShapeMode,
    FrameMaskId,
    ComparisonMask,
    FrameHasExtra,
    FrameColors,
    FrameColorsExtra,
    FrameDynamicMask,
    FrameDynamicMaskExtra,
    FrameDynamicColors,
    FrameDynamicColorsExtra,
    SpriteHasExtra,
    FrameSprites,
    SpriteOriginal,
    SpriteColors,
    SpriteMaskExtra,
    SpriteColorsExtra,
    FrameActive,
    FrameRotations,
    FrameRotationsExtra,
    SpriteDetectionDwords,
    SpriteDetectionPositions,
    SpriteDetectionAreas,
    FrameTrigger,
    FrameSpriteBounds,
    BackgroundHasExtra,
    BackgroundColors,
    BackgroundColorsExtra,
    FrameBackgroundId,
    FrameBackgroundMask,
    FrameBackgroundMaskExtra,
    FrameShadowDirections,
    FrameShadowColors,
    FrameShadowDirectionsExtra,
    FrameShadowColorsExtra,
    SpriteDynamicColors,
    SpriteDynamicColorsExtra,
    SpriteDynamicMask,
    SpriteDynamicMaskExtra,
    SpriteShapeMode,
};
inline constexpr std::size_t kCromBlockCount = 40;
// The block's name in LegacySectionOffsets.
[[nodiscard]] const char* cromBlockName(CromBlock block);
// How many elements the block has: one per frame, comparison mask, sprite or background.
[[nodiscard]] std::size_t cromBlockElementCount(const document::ProjectSnapshot& snapshot,
                                                CromBlock block);

// The bytes of single elements of the cROM blocks, as writeLegacyCrom() writes them (little
// endian), for handing a project to libserum without a cROM (Serum_LoadFromSource()). The
// snapshot must outlive the object. wideSpriteIds: 16-bit sprite IDs, as format version 1
// has them, also for a project for libserum 2.8.
class CromElements {
public:
    CromElements(const document::ProjectSnapshot& snapshot, bool wideSpriteIds);
    ~CromElements();
    CromElements(const CromElements&) = delete;
    CromElements& operator=(const CromElements&) = delete;

    // Replaces out with the element's bytes.
    void write(CromBlock block, std::size_t index, std::vector<uint8_t>& out) const;

private:
    struct Context;
    std::unique_ptr<Context> m_context;
};

// Write complete files from the snapshot alone, in the current legacy layout. With a progress,
// every byte written is added to its `done`, and a set `cancel` stops the write (it returns
// false with kLegacyIoCancelledMessage; what was written so far stays in the stream).
[[nodiscard]] bool writeLegacyCrom(std::ostream& stream,
                                   const document::ProjectSnapshot& snapshot,
                                   std::string& errorMessage,
                                   LegacySectionOffsets* sections = nullptr,
                                   LegacyIoProgress* progress = nullptr);
[[nodiscard]] bool writeLegacyCrp(std::ostream& stream,
                                  const document::ProjectSnapshot& snapshot,
                                  std::string& errorMessage,
                                  LegacySectionOffsets* sections = nullptr,
                                  LegacyIoProgress* progress = nullptr);

// The cRP part of a .cRPX (CrpxFile.h): writeLegacyCrp() with an empty PuP pack field, as the
// .cRPX keeps the pack in a chunk of its own. As many bytes as writeLegacyCrp().
[[nodiscard]] bool writeCrpxCrpPart(std::ostream& stream,
                                    const document::ProjectSnapshot& snapshot,
                                    std::string& errorMessage,
                                    LegacySectionOffsets* sections = nullptr,
                                    LegacyIoProgress* progress = nullptr);

// The settings block a cROM ends with (libserum's ReadCromSettings()): "SCX1", the payload
// size, then the contents (scenes, scaling), the scenes as a "<rom>.pup.csv" line has them
// and the scaling settings. libserum then takes the scenes and scaling settings from the cROM
// and ignores a "<rom>.pup.csv" and "scaling.txt" beside it; a libserum without it stops
// reading before it. Only a project with a ROM frame of 128x32 has scenes there (libserum
// plays scenes in no other). Empty for a snapshot without colorizationSettings, as one of the
// Win32 editor: its cROM stays as that editor writes it. The speaker lights (effects,
// defaults, the frames that change channels; libserum's kCromSettingsLights) follow when the
// project has any, also without colorizationSettings; libserum before 2.9 ignores them. The
// dynamic sets that color the SD picture only (libserum's kCromSettingsOriginalOnlySets: frame
// IDs and set masks) follow for a project for libserum 2.9 whose frames have any.
inline constexpr uint32_t kCromSettingsMagic = 0x31584353;  // "SCX1"
inline constexpr uint32_t kCromSettingsScenes = 1;
inline constexpr uint32_t kCromSettingsScaling = 2;
inline constexpr uint32_t kCromSettingsLights = 4;
inline constexpr uint32_t kCromSettingsOriginalOnlySets = 8;
[[nodiscard]] std::vector<uint8_t> cromSettingsBlock(const document::ProjectSnapshot& snapshot);

// How many bytes writeLegacyCrom() and writeLegacyCrp() write for a valid snapshot, without
// writing them (for a progress display).
[[nodiscard]] uint64_t legacyCromByteCount(const document::ProjectSnapshot& snapshot);
[[nodiscard]] uint64_t legacyCrpByteCount(const document::ProjectSnapshot& snapshot);

}  // namespace colorizing::legacy
