#include "legacy/LegacySnapshotBuilder.h"

#include <algorithm>
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
    return buildProjectSnapshot(LegacyCromFrameData(cromFrameData),
                                LegacyCrpOriginalData(crpOriginalData));
}

LegacySnapshotBuildResult buildProjectSnapshot(LegacyCromFrameData&& cromFrameData,
                                               LegacyCrpOriginalData&& crpOriginalData)
{
    if (cromFrameData.metadata.romName.empty()) {
        return fail("cROM frame data is missing ROM name");
    }
    if (cromFrameData.metadata.romName != crpOriginalData.romName) {
        return fail("cROM and cRP ROM names do not match");
    }

    const LegacyCromMetadata& metadata = cromFrameData.metadata;
    std::string geometryError;
    const std::optional<render::ProjectGeometry> geometry =
        render::ProjectGeometry::fromStoredPlanes(
            render::Size{static_cast<int>(metadata.frameWidth),
                         static_cast<int>(metadata.frameHeight)},
            render::Size{static_cast<int>(metadata.hdFrameWidth),
                         static_cast<int>(metadata.hdFrameHeight)},
            metadata.originalIs256x64,
            &geometryError);
    if (!geometry) {
        return fail(geometryError);
    }

    const std::size_t frameCount = metadata.frameCount;
    if (crpOriginalData.originalSdFrames.size() != frameCount ||
        cromFrameData.colorizedSdFrames.size() != frameCount ||
        cromFrameData.shapeComparisonModes.size() != frameCount ||
        cromFrameData.comparisonMaskIds.size() != frameCount ||
        cromFrameData.extraFrameFlags.size() != frameCount ||
        cromFrameData.dynamicFrames.size() != frameCount ||
        cromFrameData.frameBackgrounds.size() != frameCount ||
        cromFrameData.frameSpriteReferences.size() != frameCount ||
        cromFrameData.colorRotationsSd.size() != frameCount ||
        cromFrameData.colorRotationsHd.size() != frameCount ||
        cromFrameData.triggerIds.size() != frameCount) {
        return fail("cROM and cRP frame counts do not match");
    }

    document::ProjectSnapshot snapshot;
    snapshot.romName = cromFrameData.metadata.romName;
    snapshot.comparisonMasks = std::move(cromFrameData.comparisonMasks);
    snapshot.backgrounds = std::move(cromFrameData.backgrounds);
    snapshot.sprites = std::move(cromFrameData.sprites);
    snapshot.palettes = std::move(crpOriginalData.palettes);
    snapshot.colorCount = cromFrameData.metadata.colorCount;
    snapshot.cromRevision =
        cromFrameData.metadata.headerLengthBytes / static_cast<uint32_t>(sizeof(uint32_t));
    snapshot.editorMetadata = std::move(crpOriginalData.editorMetadata);
    snapshot.geometry = *geometry;
    for (std::size_t spriteIndex = 0;
         spriteIndex < snapshot.sprites.size() &&
             spriteIndex < crpOriginalData.spriteNames.size();
         ++spriteIndex) {
        snapshot.sprites[spriteIndex].name = crpOriginalData.spriteNames[spriteIndex];
    }
    snapshot.frames.reserve(frameCount);
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        document::ProjectFrameSnapshot frame;
        frame.originalSd = std::move(crpOriginalData.originalSdFrames[frameIndex]);
        frame.colorizedSd = std::move(cromFrameData.colorizedSdFrames[frameIndex]);
        // The extra-plane frame is the frame's HD picture when its flag is set, otherwise
        // pixels the Win32 editor left behind, kept only when they are not all 0.
        if (cromFrameData.extraFrameFlags[frameIndex] != 0) {
            frame.colorizedHd = std::move(cromFrameData.colorizedHdFrames[frameIndex]);
        } else if (std::any_of(cromFrameData.colorizedHdFrames[frameIndex].pixels.begin(),
                               cromFrameData.colorizedHdFrames[frameIndex].pixels.end(),
                               [](uint16_t pixel) { return pixel != 0; })) {
            frame.inactiveHd = std::move(cromFrameData.colorizedHdFrames[frameIndex]);
        }
        cromFrameData.colorizedHdFrames[frameIndex] = {};
        frame.shapeComparisonMode = cromFrameData.shapeComparisonModes[frameIndex];
        frame.comparisonMaskId = cromFrameData.comparisonMaskIds[frameIndex];
        frame.dynamicData = std::move(cromFrameData.dynamicFrames[frameIndex]);
        frame.backgroundData = std::move(cromFrameData.frameBackgrounds[frameIndex]);
        frame.spriteReferences = std::move(cromFrameData.frameSpriteReferences[frameIndex]);
        frame.triggerId = cromFrameData.triggerIds[frameIndex];
        frame.durationMs = frameIndex < crpOriginalData.frameDurationsMs.size()
                               ? crpOriginalData.frameDurationsMs[frameIndex]
                               : uint32_t{0};
        frame.colorRotationsSd = std::move(cromFrameData.colorRotationsSd[frameIndex]);
        frame.colorRotationsHd = std::move(cromFrameData.colorRotationsHd[frameIndex]);
        snapshot.frames.push_back(std::move(frame));
    }
    cromFrameData = LegacyCromFrameData{};
    crpOriginalData = LegacyCrpOriginalData{};

    std::string validationError;
    if (!document::isValidProjectSnapshot(snapshot, &validationError)) {
        return fail(validationError);
    }
    if (geometry->discardsExtraPlane()) {
        document::discardExtraPlane(snapshot);
    }

    LegacySnapshotBuildResult result;
    result.snapshot = std::move(snapshot);
    result.ok = true;
    return result;
}

}  // namespace colorizing::legacy
