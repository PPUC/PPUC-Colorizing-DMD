#include "edit/StateCommands.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace colorizing::edit {

namespace {

bool samePictures(const document::SpriteRecord& lhs, const document::SpriteRecord& rhs)
{
    return lhs.hasHd == rhs.hasHd && lhs.originalSd.pixels == rhs.originalSd.pixels &&
           lhs.colorizedSd.pixels == rhs.colorizedSd.pixels &&
           lhs.maskHd.pixels == rhs.maskHd.pixels &&
           lhs.colorizedHd.pixels == rhs.colorizedHd.pixels;
}

bool sameSlots(const std::vector<document::FrameSpriteReference>& lhs,
               const std::vector<document::FrameSpriteReference>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                      [](const document::FrameSpriteReference& a,
                         const document::FrameSpriteReference& b) {
                          return a.spriteId == b.spriteId && a.minX == b.minX &&
                                 a.minY == b.minY && a.maxX == b.maxX && a.maxY == b.maxY &&
                                 a.offsetX == b.offsetX && a.offsetY == b.offsetY;
                      });
}

bool sameAreas(const std::vector<document::SpriteDetectionArea>& lhs,
               const std::vector<document::SpriteDetectionArea>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                      [](const document::SpriteDetectionArea& a,
                         const document::SpriteDetectionArea& b) {
                          return a.x == b.x && a.y == b.y && a.width == b.width &&
                                 a.height == b.height;
                      });
}

bool sameSprite(const SpriteState& lhs, const SpriteState& rhs)
{
    const document::SpriteRecord& a = lhs.sprite;
    const document::SpriteRecord& b = rhs.sprite;
    return lhs.sourceFrame == rhs.sourceFrame && samePictures(a, b) &&
           a.detectionDwords == b.detectionDwords &&
           a.detectionDwordPositions == b.detectionDwordPositions &&
           sameAreas(a.detectionAreas, b.detectionAreas) && a.name == b.name &&
           a.dynamicMaskSd.pixels == b.dynamicMaskSd.pixels &&
           a.dynamicMaskHd.pixels == b.dynamicMaskHd.pixels &&
           a.dynamicColorsSd == b.dynamicColorsSd && a.dynamicColorsHd == b.dynamicColorsHd &&
           a.shapeMode == b.shapeMode;
}

void addSpriteReferences(std::vector<RecordReference>& references,
                         const std::vector<document::FrameSpriteReference>& slots)
{
    for (const document::FrameSpriteReference& slot : slots) {
        if (slot.spriteId != document::kNoSpriteId) {
            references.push_back({RecordKind::Sprite, slot.spriteId});
        }
    }
}

}  // namespace

SpriteState spriteState(const document::ProjectDocument& document, std::size_t spriteIndex)
{
    return SpriteState{document.sprite(spriteIndex), document.spriteSourceFrame(spriteIndex)};
}

SpriteStateCommand::SpriteStateCommand(std::size_t spriteIndex,
                                       SpriteState before,
                                       SpriteState after)
    : m_spriteIndex(spriteIndex),
      m_before(std::move(before)),
      m_after(std::move(after)),
      m_replacesPictures(!samePictures(m_before.sprite, m_after.sprite))
{
}

void SpriteStateCommand::apply(document::ProjectDocument& document) const
{
    document.restoreSprite(m_spriteIndex, m_after.sprite);
    document.setSpriteSourceFrame(m_spriteIndex, m_after.sourceFrame);
}

void SpriteStateCommand::revert(document::ProjectDocument& document) const
{
    document.restoreSprite(m_spriteIndex, m_before.sprite);
    document.setSpriteSourceFrame(m_spriteIndex, m_before.sourceFrame);
}

EditTarget SpriteStateCommand::target() const
{
    return EditTarget{EditKind::SpriteAttributes, m_spriteIndex, render::Resolution::Sd, {}};
}

std::vector<RecordReference> SpriteStateCommand::recordReferences() const
{
    // The source frames too: frame deletions renumber them, and the command would put an
    // outdated number back.
    return {{RecordKind::Sprite, m_spriteIndex},
            {RecordKind::Frame, m_before.sourceFrame},
            {RecordKind::Frame, m_after.sourceFrame}};
}

std::vector<ReplacedData> SpriteStateCommand::replacedData() const
{
    if (!m_replacesPictures) {
        return {};
    }
    return {{EditKind::SpriteImage, m_spriteIndex, render::Resolution::Sd},
            {EditKind::SpriteImage, m_spriteIndex, render::Resolution::Hd}};
}

FrameSpriteSlotsCommand::FrameSpriteSlotsCommand(
    std::size_t frameIndex,
    std::vector<document::FrameSpriteReference> before,
    std::vector<document::FrameSpriteReference> after)
    : m_frameIndex(frameIndex),
      m_before(std::move(before)),
      m_after(std::move(after))
{
}

void FrameSpriteSlotsCommand::apply(document::ProjectDocument& document) const
{
    document.restoreFrameSpriteReferences(m_frameIndex, m_after);
}

void FrameSpriteSlotsCommand::revert(document::ProjectDocument& document) const
{
    document.restoreFrameSpriteReferences(m_frameIndex, m_before);
}

EditTarget FrameSpriteSlotsCommand::target() const
{
    return EditTarget{EditKind::FrameAttributes,
                      m_frameIndex,
                      render::Resolution::Sd,
                      {m_frameIndex}};
}

std::vector<RecordReference> FrameSpriteSlotsCommand::recordReferences() const
{
    std::vector<RecordReference> references{{RecordKind::Frame, m_frameIndex}};
    addSpriteReferences(references, m_before);
    addSpriteReferences(references, m_after);
    return references;
}

ColorRotationsCommand::ColorRotationsCommand(std::size_t frameIndex,
                                             render::Resolution resolution,
                                             document::CompactArray<uint16_t> before,
                                             document::CompactArray<uint16_t> after)
    : m_frameIndex(frameIndex),
      m_resolution(resolution),
      m_before(std::move(before)),
      m_after(std::move(after))
{
}

void ColorRotationsCommand::apply(document::ProjectDocument& document) const
{
    document.restoreColorRotations(m_frameIndex, m_resolution, m_after);
}

void ColorRotationsCommand::revert(document::ProjectDocument& document) const
{
    document.restoreColorRotations(m_frameIndex, m_resolution, m_before);
}

EditTarget ColorRotationsCommand::target() const
{
    return EditTarget{EditKind::FrameAttributes,
                      m_frameIndex,
                      render::Resolution::Sd,
                      {m_frameIndex}};
}

std::vector<RecordReference> ColorRotationsCommand::recordReferences() const
{
    return {{RecordKind::Frame, m_frameIndex}};
}

CommandGroup::CommandGroup(std::vector<std::unique_ptr<EditCommand>> commands)
    : m_commands(std::move(commands))
{
    if (m_commands.empty()) {
        throw std::invalid_argument("a command group needs a command");
    }
    for (const std::unique_ptr<EditCommand>& command : m_commands) {
        if (!command) {
            throw std::invalid_argument("a command group needs a command");
        }
    }
}

void CommandGroup::apply(document::ProjectDocument& document) const
{
    std::size_t applied = 0;
    try {
        for (; applied < m_commands.size(); ++applied) {
            m_commands[applied]->apply(document);
        }
    } catch (...) {
        // Each command leaves the document as it was when it throws; take back the others.
        while (applied > 0) {
            m_commands[--applied]->revert(document);
        }
        throw;
    }
}

void CommandGroup::revert(document::ProjectDocument& document) const
{
    std::size_t remaining = m_commands.size();
    try {
        for (; remaining > 0; --remaining) {
            m_commands[remaining - 1]->revert(document);
        }
    } catch (...) {
        for (; remaining < m_commands.size(); ++remaining) {
            m_commands[remaining]->apply(document);
        }
        throw;
    }
}

EditTarget CommandGroup::target() const
{
    return m_commands.front()->target();
}

std::vector<ReplacedData> CommandGroup::replacedData() const
{
    std::vector<ReplacedData> replaced;
    for (const std::unique_ptr<EditCommand>& command : m_commands) {
        const std::vector<ReplacedData> more = command->replacedData();
        replaced.insert(replaced.end(), more.begin(), more.end());
    }
    return replaced;
}

std::vector<RecordReference> CommandGroup::recordReferences() const
{
    std::vector<RecordReference> references;
    for (const std::unique_ptr<EditCommand>& command : m_commands) {
        const std::vector<RecordReference> more = command->recordReferences();
        references.insert(references.end(), more.begin(), more.end());
    }
    return references;
}

std::vector<RenumberedRecords> CommandGroup::renumberedRecords() const
{
    std::vector<RenumberedRecords> renumbered;
    for (const std::unique_ptr<EditCommand>& command : m_commands) {
        const std::vector<RenumberedRecords> more = command->renumberedRecords();
        renumbered.insert(renumbered.end(), more.begin(), more.end());
    }
    return renumbered;
}

std::size_t CommandGroup::heldBytes() const
{
    std::size_t bytes = 0;
    for (const std::unique_ptr<EditCommand>& command : m_commands) {
        bytes += command->heldBytes();
    }
    return bytes;
}

const std::vector<std::unique_ptr<EditCommand>>& CommandGroup::commands() const
{
    return m_commands;
}

std::unique_ptr<EditCommand> spriteStateCommand(const document::ProjectDocument& document,
                                                std::size_t spriteIndex,
                                                SpriteState before)
{
    SpriteState after = spriteState(document, spriteIndex);
    if (sameSprite(before, after)) {
        return nullptr;
    }
    return std::make_unique<SpriteStateCommand>(spriteIndex, std::move(before), std::move(after));
}

std::unique_ptr<EditCommand> frameSpriteSlotsCommand(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    std::vector<document::FrameSpriteReference> before)
{
    std::vector<document::FrameSpriteReference> after =
        document.frameSpriteReferences(frameIndex);
    if (sameSlots(before, after)) {
        return nullptr;
    }
    return std::make_unique<FrameSpriteSlotsCommand>(frameIndex,
                                                     std::move(before),
                                                     std::move(after));
}

std::unique_ptr<EditCommand> colorRotationsCommand(const document::ProjectDocument& document,
                                                   std::size_t frameIndex,
                                                   render::Resolution resolution,
                                                   document::CompactArray<uint16_t> before)
{
    document::CompactArray<uint16_t> after = document.colorRotations(frameIndex, resolution);
    if (before == after) {
        return nullptr;
    }
    return std::make_unique<ColorRotationsCommand>(frameIndex,
                                                   resolution,
                                                   std::move(before),
                                                   std::move(after));
}

std::unique_ptr<EditCommand> groupCommands(std::vector<std::unique_ptr<EditCommand>> commands)
{
    commands.erase(std::remove(commands.begin(), commands.end(), nullptr), commands.end());
    if (commands.empty()) {
        return nullptr;
    }
    if (commands.size() == 1) {
        return std::move(commands.front());
    }
    return std::make_unique<CommandGroup>(std::move(commands));
}

}  // namespace colorizing::edit
