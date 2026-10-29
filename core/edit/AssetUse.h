#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"

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

// What dropUnusedAssets() left out.
struct DroppedAssets {
    std::size_t comparisonMasks = 0;
    std::size_t backgrounds = 0;
    std::size_t sprites = 0;

    [[nodiscard]] std::size_t total() const { return comparisonMasks + backgrounds + sprites; }
};

// Takes the comparison masks, backgrounds and sprites no frame names out of the snapshot (the
// rule of assetUse()) and renumbers the frames' references to those that stay, in their order.
// For the export: the runtime never looks at them, and the project keeps its own numbering. A
// reference to a record the snapshot does not have becomes none.
DroppedAssets dropUnusedAssets(document::ProjectSnapshot& snapshot);

}  // namespace colorizing::edit
