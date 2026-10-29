#include "edit/AssetUse.h"

#include <optional>

namespace colorizing::edit {

AssetUse assetUse(const document::ProjectDocument& document)
{
    AssetUse use;
    use.comparisonMasks.assign(document.comparisonMaskCount(), false);
    use.backgrounds.assign(document.backgroundCount(), false);
    use.sprites.assign(document.spriteCount(), false);
    const auto mark = [](std::vector<bool>& used, std::size_t index) {
        if (index < used.size()) {
            used[index] = true;
        }
    };
    const auto& settings = document.colorizationSettings();
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        mark(use.comparisonMasks, document.comparisonMaskId(frame));
        if (const auto& background = document.backgroundData(frame)) {
            mark(use.backgrounds, background->backgroundId);
        }
        for (const document::FrameSpriteReference& reference :
             document.frameSpriteReferences(frame)) {
            mark(use.sprites, reference.spriteId);
        }
        const uint32_t trigger = document.sceneTriggerId(frame);
        if (settings && trigger <= 0xffff && !document.sceneFrame(frame) &&
            settings->scene(static_cast<uint16_t>(trigger))) {
            // A variant uses its base's frames: the base is used too.
            use.scenes.insert(static_cast<uint16_t>(trigger));
            use.scenes.insert(settings->framesSceneId(static_cast<uint16_t>(trigger)));
        }
    }
    return use;
}

namespace {

// The new index of each record that stays; nothing for one that goes.
std::vector<std::optional<std::size_t>> renumbering(const std::vector<bool>& used)
{
    std::vector<std::optional<std::size_t>> numbers(used.size());
    std::size_t next = 0;
    for (std::size_t index = 0; index < used.size(); ++index) {
        if (used[index]) {
            numbers[index] = next++;
        }
    }
    return numbers;
}

// The records that stay, in their order; the number of those left out.
template <typename Record>
std::size_t keepUsed(std::vector<Record>& records, const std::vector<bool>& used)
{
    std::vector<Record> kept;
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (index < used.size() && used[index]) {
            kept.push_back(std::move(records[index]));
        }
    }
    const std::size_t dropped = records.size() - kept.size();
    records = std::move(kept);
    return dropped;
}

}  // namespace

DroppedAssets dropUnusedAssets(document::ProjectSnapshot& snapshot)
{
    std::vector<bool> masks(snapshot.comparisonMasks.size(), false);
    std::vector<bool> backgrounds(snapshot.backgrounds.size(), false);
    std::vector<bool> sprites(snapshot.sprites.size(), false);
    const auto mark = [](std::vector<bool>& used, std::size_t index) {
        if (index < used.size()) {
            used[index] = true;
        }
    };
    for (const document::ProjectFrameSnapshot& frame : snapshot.frames) {
        mark(masks, frame.comparisonMaskId);
        if (frame.backgroundData) {
            mark(backgrounds, frame.backgroundData->backgroundId);
        }
        for (const document::FrameSpriteReference& reference : frame.spriteReferences) {
            mark(sprites, reference.spriteId);
        }
    }

    const auto maskNumbers = renumbering(masks);
    const auto backgroundNumbers = renumbering(backgrounds);
    const auto spriteNumbers = renumbering(sprites);
    for (document::ProjectFrameSnapshot& frame : snapshot.frames) {
        if (frame.comparisonMaskId != document::kNoComparisonMaskId) {
            frame.comparisonMaskId =
                frame.comparisonMaskId < maskNumbers.size()
                    ? static_cast<uint8_t>(*maskNumbers[frame.comparisonMaskId])
                    : document::kNoComparisonMaskId;
        }
        if (frame.backgroundData &&
            frame.backgroundData->backgroundId != document::kNoBackgroundId) {
            uint16_t& id = frame.backgroundData->backgroundId;
            id = id < backgroundNumbers.size() ? static_cast<uint16_t>(*backgroundNumbers[id])
                                               : document::kNoBackgroundId;
        }
        for (document::FrameSpriteReference& reference : frame.spriteReferences) {
            if (reference.spriteId != document::kNoSpriteId) {
                reference.spriteId =
                    reference.spriteId < spriteNumbers.size()
                        ? static_cast<uint16_t>(*spriteNumbers[reference.spriteId])
                        : document::kNoSpriteId;
            }
        }
    }

    DroppedAssets dropped;
    if (snapshot.comparisonMaskMeta.size() == snapshot.comparisonMasks.size()) {
        (void)keepUsed(snapshot.comparisonMaskMeta, masks);
    } else {
        snapshot.comparisonMaskMeta.clear();
    }
    dropped.comparisonMasks = keepUsed(snapshot.comparisonMasks, masks);
    dropped.backgrounds = keepUsed(snapshot.backgrounds, backgrounds);
    dropped.sprites = keepUsed(snapshot.sprites, sprites);
    return dropped;
}

}  // namespace colorizing::edit
