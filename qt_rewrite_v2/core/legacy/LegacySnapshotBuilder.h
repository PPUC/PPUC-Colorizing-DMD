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

// Moves the parsed blocks into the snapshot; what is left of the parsed data afterwards is
// empty.
[[nodiscard]] LegacySnapshotBuildResult buildProjectSnapshot(
    LegacyCromFrameData&& cromFrameData,
    LegacyCrpOriginalData&& crpOriginalData);
// The same on copies (which share the blocks, see document::CompactArray).
[[nodiscard]] LegacySnapshotBuildResult buildProjectSnapshot(
    const LegacyCromFrameData& cromFrameData,
    const LegacyCrpOriginalData& crpOriginalData);

}  // namespace colorizing::legacy

