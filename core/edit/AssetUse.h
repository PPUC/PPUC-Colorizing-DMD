#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::edit {

// Which assets the frames of the project use, for the left sidebar's "Not used by any frame".
// Comparison masks, backgrounds and sprites by their index: one a frame names (its comparison
// mask, its background, a sprite in one of its slots). Scenes by ID: one a frame of the game
// starts (startsScene()); a scene's own frames do not count.
struct AssetUse {
    std::vector<bool> comparisonMasks;
    std::vector<bool> backgrounds;
    std::vector<bool> sprites;
    std::set<uint16_t> scenes;
};

[[nodiscard]] AssetUse assetUse(const document::ProjectDocument& document);

}  // namespace colorizing::edit
