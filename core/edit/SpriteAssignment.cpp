#include "edit/SpriteAssignment.h"

#include <algorithm>

#include "edit/StateCommands.h"

namespace colorizing::edit {

namespace {

// The used slots of a frame: they come first, up to the first empty one.
std::size_t usedSlots(const std::vector<document::FrameSpriteReference>& references)
{
    std::size_t used = 0;
    while (used < references.size() && references[used].spriteId != document::kNoSpriteId) {
        ++used;
    }
    return used;
}

bool hasSprite(const std::vector<document::FrameSpriteReference>& references,
               std::size_t sprite)
{
    return std::any_of(references.begin(), references.end(),
                       [sprite](const document::FrameSpriteReference& reference) {
                           return reference.spriteId == sprite;
                       });
}

}  // namespace

SpritesAdded addSpritesToFrames(document::ProjectDocument& document,
                                const std::vector<std::size_t>& frames,
                                const std::vector<std::size_t>& sprites)
{
    SpritesAdded result;
    const document::Size size = document.originalFrameSize();
    for (const std::size_t frame : frames) {
        const std::vector<document::FrameSpriteReference> before =
            document.frameSpriteReferences(frame);
        bool changed = false;
        for (const std::size_t sprite : sprites) {
            const std::vector<document::FrameSpriteReference>& references =
                document.frameSpriteReferences(frame);
            if (hasSprite(references, sprite)) {
                ++result.kept;
                continue;
            }
            const std::size_t slot = usedSlots(references);
            if (slot >= document.spritesPerFrame()) {
                ++result.full;
                continue;
            }
            document::FrameSpriteReference reference;
            reference.spriteId = static_cast<uint16_t>(sprite);
            reference.maxX = static_cast<uint16_t>(size.width - 1);
            reference.maxY = static_cast<uint16_t>(size.height - 1);
            document.setFrameSpriteReference(frame, slot, reference);
            ++result.added;
            changed = true;
        }
        if (changed) {
            if (auto command = frameSpriteSlotsCommand(document, frame, before)) {
                result.commands.push_back(std::move(command));
            }
        }
    }
    return result;
}

std::size_t framesPassingSpriteWarning(const document::ProjectDocument& document,
                                       const std::vector<std::size_t>& frames,
                                       const std::vector<std::size_t>& sprites)
{
    std::size_t passing = 0;
    for (const std::size_t frame : frames) {
        const std::vector<document::FrameSpriteReference>& references =
            document.frameSpriteReferences(frame);
        const std::size_t used = usedSlots(references);
        std::size_t added = 0;
        for (const std::size_t sprite : sprites) {
            added += hasSprite(references, sprite) ? 0 : 1;
        }
        const std::size_t after = std::min(used + added, document.spritesPerFrame());
        passing += used <= document::kSpritesPerFrameWarning &&
                           after > document::kSpritesPerFrameWarning
                       ? 1
                       : 0;
    }
    return passing;
}

}  // namespace colorizing::edit
