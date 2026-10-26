#include "edit/AssetUse.h"

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
        const uint32_t trigger = document.triggerId(frame);
        if (settings && trigger <= 0xffff && !document.sceneFrame(frame) &&
            settings->scene(static_cast<uint16_t>(trigger))) {
            use.scenes.insert(static_cast<uint16_t>(trigger));
        }
    }
    return use;
}

}  // namespace colorizing::edit
