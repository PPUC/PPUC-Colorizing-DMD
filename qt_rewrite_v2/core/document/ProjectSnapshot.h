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
    // keeps its layout in the file and is written back unchanged.
    render::ProjectGeometry geometry;
    EditorMetadata editorMetadata = makeDefaultEditorMetadata();
};

[[nodiscard]] Size originalFrameSize(const ProjectSnapshot& snapshot);

// Fills in the optional per-sprite dynamic data with its "no dynamic content" defaults.
void normalizeSpriteRecord(SpriteRecord& sprite, uint32_t colorCount);

[[nodiscard]] bool isValidProjectSnapshot(const ProjectSnapshot& snapshot,
                                          std::string* errorMessage = nullptr);

}  // namespace colorizing::document
