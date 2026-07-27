#pragma once

#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::document {

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
};

struct ProjectSnapshot {
    std::string romName;
    std::vector<ProjectFrameSnapshot> frames;
    std::vector<IndexedFrame> comparisonMasks;
    std::vector<BackgroundRecord> backgrounds;
    std::vector<SpriteRecord> sprites;
    std::vector<UnsupportedLegacyPayload> unsupportedPayloads;
};

[[nodiscard]] bool isValidProjectSnapshot(const ProjectSnapshot& snapshot,
                                          std::string* errorMessage = nullptr);

}  // namespace colorizing::document
