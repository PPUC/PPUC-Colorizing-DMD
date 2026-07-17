#include "legacy/LegacySnapshotBuilder.h"

#include <utility>

namespace colorizing::legacy {

namespace {

LegacySnapshotBuildResult fail(std::string message)
{
    LegacySnapshotBuildResult result;
    result.errorMessage = std::move(message);
    return result;
}

}  // namespace

LegacySnapshotBuildResult buildProjectSnapshot(const LegacyCromFrameData& cromFrameData,
                                               const LegacyCrpOriginalData& crpOriginalData)
{
    if (cromFrameData.metadata.romName.empty()) {
        return fail("cROM frame data is missing ROM name");
    }
    if (cromFrameData.metadata.romName != crpOriginalData.romName) {
        return fail("cROM and cRP ROM names do not match");
    }

    const std::size_t frameCount = cromFrameData.metadata.frameCount;
    if (crpOriginalData.originalSdFrames.size() != frameCount ||
        cromFrameData.colorizedSdFrames.size() != frameCount ||
        cromFrameData.shapeComparisonModes.size() != frameCount ||
        cromFrameData.comparisonMaskIds.size() != frameCount ||
        cromFrameData.extraFrameFlags.size() != frameCount) {
        return fail("cROM and cRP frame counts do not match");
    }

    document::ProjectSnapshot snapshot;
    snapshot.romName = cromFrameData.metadata.romName;
    snapshot.comparisonMasks = cromFrameData.comparisonMasks;
    snapshot.frames.reserve(frameCount);
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        snapshot.frames.push_back(document::ProjectFrameSnapshot{
            crpOriginalData.originalSdFrames[frameIndex],
            cromFrameData.colorizedSdFrames[frameIndex],
            hdFrameIfPresent(cromFrameData, frameIndex),
            cromFrameData.shapeComparisonModes[frameIndex],
            cromFrameData.comparisonMaskIds[frameIndex],
        });
    }

    std::string validationError;
    if (!document::isValidProjectSnapshot(snapshot, &validationError)) {
        return fail(validationError);
    }

    LegacySnapshotBuildResult result;
    result.snapshot = std::move(snapshot);
    result.ok = true;
    return result;
}

}  // namespace colorizing::legacy
