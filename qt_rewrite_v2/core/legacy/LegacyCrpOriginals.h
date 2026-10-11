#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "legacy/LegacyCromMetadata.h"

namespace colorizing::legacy {

struct LegacyCrpOriginalData {
    std::string romName;
    std::vector<document::IndexedFrame> originalSdFrames;
    std::vector<std::string> spriteNames;
    std::vector<uint32_t> frameDurationsMs;
    std::vector<document::PaletteRecord> palettes;
    document::EditorMetadata editorMetadata = document::makeDefaultEditorMetadata();
};

struct LegacyCrpOriginalParseResult {
    LegacyCrpOriginalData originalData;
    std::string errorMessage;
    std::size_t nextOffset = 0;
    bool ok = false;
};

[[nodiscard]] LegacyCrpOriginalParseResult parseLegacyCrpOriginalFrames(
    const std::vector<uint8_t>& bytes,
    const LegacyCromMetadata& cromMetadata);
[[nodiscard]] LegacyCrpOriginalParseResult parseLegacyCrpOriginalFrames(
    const uint8_t* bytes,
    std::size_t size,
    const LegacyCromMetadata& cromMetadata);

}  // namespace colorizing::legacy
