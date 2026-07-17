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
};

struct ProjectSnapshot {
    std::string romName;
    std::vector<ProjectFrameSnapshot> frames;
    std::vector<IndexedFrame> comparisonMasks;
    std::vector<UnsupportedLegacyPayload> unsupportedPayloads;
};

[[nodiscard]] bool isValidProjectSnapshot(const ProjectSnapshot& snapshot,
                                          std::string* errorMessage = nullptr);

}  // namespace colorizing::document
