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
};

struct ProjectSnapshot {
    std::string romName;
    std::vector<ProjectFrameSnapshot> frames;
    std::vector<UnsupportedLegacyPayload> unsupportedPayloads;
};

[[nodiscard]] bool isValidProjectSnapshot(const ProjectSnapshot& snapshot,
                                          std::string* errorMessage = nullptr);

}  // namespace colorizing::document

