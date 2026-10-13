#pragma once

#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "render/ProjectGeometry.h"

namespace colorizing::document {

constexpr uint32_t kCurrentCromRevision = 20;
constexpr uint32_t kOldestWritableCromRevision = 19;

struct ProjectFrameSnapshot {
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    uint8_t shapeComparisonMode = 0;
    uint8_t comparisonMaskId = 0;
    std::optional<FrameDynamicData> dynamicData;
    std::optional<FrameBackgroundData> backgroundData;
    std::vector<FrameSpriteReference> spriteReferences;
    uint32_t triggerId = 0;
    uint32_t durationMs = 0;
    std::vector<uint16_t> colorRotationsSd;
    std::vector<uint16_t> colorRotationsHd;
    std::optional<Rgb565Frame> inactiveHd;
};

struct ProjectSnapshot {
    std::string romName;
    std::vector<ProjectFrameSnapshot> frames;
    std::vector<IndexedFrame> comparisonMasks;
    std::vector<BackgroundRecord> backgrounds;
    std::vector<SpriteRecord> sprites;
    std::vector<PaletteRecord> palettes;
    std::vector<UnsupportedLegacyPayload> unsupportedPayloads;
    // Number of colors of the original ROM frames (4 or 16).
    uint32_t colorCount = 4;
    // cROM header revision (header length in 32-bit words) the project was loaded with, so a
    // save does not silently move it to a revision an older libserum cannot read.
    uint32_t cromRevision = kCurrentCromRevision;
    // Sizes of the stored planes and which of them is the ROM frame's. Everything named ...Sd
    // has the size of the original plane and everything named ...Hd that of the extra plane;
    // original frames and comparison masks have the size of the ROM frame. A carried plane
    // is written back unchanged; a discarded one holds "no extra data", see
    // discardExtraPlane().
    render::ProjectGeometry geometry;
    EditorMetadata editorMetadata = makeDefaultEditorMetadata();
};

[[nodiscard]] Size originalFrameSize(const ProjectSnapshot& snapshot);

// Fills in the optional per-sprite dynamic data with its "no dynamic content" defaults.
void normalizeSpriteRecord(SpriteRecord& sprite, uint32_t colorCount);

// Makes the extra plane "no extra data": no frame, background or sprite has it, and every
// extra-plane array holds what the Win32 editor gives an item without extra resolution
// (colors, dynamic colors, shadows and rotations 0, dynamic and sprite masks 255, background
// masks 1). Sizes and the header stay as the geometry says.
void discardExtraPlane(ProjectSnapshot& snapshot);

// A frame nobody has worked on, as the Win32 editor makes it from a frame of a dump
// (CopyTXTFrames2Frame): its picture on the native plane is the original in the plain
// original colors, it has no comparison mask, no shape mode, no dynamic zone, no background
// (with the background masks set), no sprite (each slot's bounds are the whole frame), no
// trigger and no color rotation. An ordinary project gets no extra-plane picture; a
// project with 256x64 originals gets the 256x64 picture the runtime needs and, on its
// unused 128x32 plane, every second pixel of it. Throws std::invalid_argument for an
// original that has not the size of the ROM frame.
[[nodiscard]] ProjectFrameSnapshot makeUncolorizedFrameSnapshot(
    const render::ProjectGeometry& geometry,
    uint32_t colorCount,
    IndexedFrame original,
    uint32_t durationMs);

[[nodiscard]] bool isValidProjectSnapshot(const ProjectSnapshot& snapshot,
                                          std::string* errorMessage = nullptr);

}  // namespace colorizing::document
