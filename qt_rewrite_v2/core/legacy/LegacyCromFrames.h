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
    std::size_t shapeComparisonModesOffset = 0;
    std::size_t comparisonMaskIdsOffset = 0;
    std::size_t comparisonMasksOffset = 0;
    std::size_t extraFrameFlagsOffset = 0;
    std::size_t colorizedSdFramesOffset = 0;
    std::size_t colorizedHdFramesOffset = 0;
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
