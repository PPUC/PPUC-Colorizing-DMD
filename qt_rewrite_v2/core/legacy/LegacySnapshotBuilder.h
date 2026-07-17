#pragma once

#include <string>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacyCrpOriginals.h"

namespace colorizing::legacy {

struct LegacySnapshotBuildResult {
    document::ProjectSnapshot snapshot;
    std::string errorMessage;
    bool ok = false;
};

[[nodiscard]] LegacySnapshotBuildResult buildProjectSnapshot(
    const LegacyCromFrameData& cromFrameData,
    const LegacyCrpOriginalData& crpOriginalData);

}  // namespace colorizing::legacy

