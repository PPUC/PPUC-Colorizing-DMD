#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "legacy/LegacyCromMetadata.h"

namespace colorizing::legacy {

struct LegacyCromFrameData {
    LegacyCromMetadata metadata;
    std::vector<uint8_t> shapeComparisonModes;
    std::vector<uint8_t> comparisonMaskIds;
    std::vector<document::IndexedFrame> comparisonMasks;
    std::vector<uint8_t> extraFrameFlags;
    std::vector<document::Rgb565Frame> colorizedSdFrames;
    std::vector<document::Rgb565Frame> colorizedHdFrames;
    std::vector<document::FrameDynamicData> dynamicFrames;
    std::vector<document::SpriteRecord> sprites;
    std::vector<std::vector<document::FrameSpriteReference>> frameSpriteReferences;
    std::vector<uint32_t> triggerIds;
    std::vector<uint8_t> extraBackgroundFlags;
    std::vector<document::BackgroundRecord> backgrounds;
    std::vector<document::FrameBackgroundData> frameBackgrounds;
    std::size_t shapeComparisonModesOffset = 0;
    std::size_t comparisonMaskIdsOffset = 0;
    std::size_t comparisonMasksOffset = 0;
    std::size_t extraFrameFlagsOffset = 0;
    std::size_t colorizedSdFramesOffset = 0;
    std::size_t colorizedHdFramesOffset = 0;
    std::size_t dynamicMasksSdOffset = 0;
    std::size_t dynamicMasksHdOffset = 0;
    std::size_t dynamicColorsSdOffset = 0;
    std::size_t dynamicColorsHdOffset = 0;
    std::size_t extraSpriteFlagsOffset = 0;
    std::size_t frameSpritesOffset = 0;
    std::size_t spriteOriginalsOffset = 0;
    std::size_t spriteColorsSdOffset = 0;
    std::size_t spriteMasksHdOffset = 0;
    std::size_t spriteColorsHdOffset = 0;
    std::size_t spriteDetectionDwordsOffset = 0;
    std::size_t spriteDetectionDwordPositionsOffset = 0;
    std::size_t spriteDetectionAreasOffset = 0;
    std::size_t triggerIdsOffset = 0;
    std::size_t frameSpriteBoundingBoxesOffset = 0;
    std::size_t extraBackgroundFlagsOffset = 0;
    std::size_t backgroundFramesSdOffset = 0;
    std::size_t backgroundFramesHdOffset = 0;
    std::size_t backgroundIdsOffset = 0;
    std::size_t backgroundMasksSdOffset = 0;
    std::size_t backgroundMasksHdOffset = 0;
};

struct LegacyCromFrameParseResult {
    LegacyCromFrameData frameData;
    std::string errorMessage;
    std::size_t nextOffset = 0;
    bool ok = false;
};

[[nodiscard]] LegacyCromFrameParseResult parseModernLegacyCromFrames(
    const std::vector<uint8_t>& bytes);
[[nodiscard]] LegacyCromFrameParseResult parseModernLegacyCromFrames(const uint8_t* bytes,
                                                                     std::size_t size);
[[nodiscard]] std::optional<document::Rgb565Frame> hdFrameIfPresent(
    const LegacyCromFrameData& frameData,
    std::size_t frameIndex);

}  // namespace colorizing::legacy
