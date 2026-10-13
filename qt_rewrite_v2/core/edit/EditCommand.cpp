#include "edit/EditCommand.h"

#include <stdexcept>
#include <utility>

namespace colorizing::edit {

std::vector<ReplacedData> EditCommand::replacedData() const
{
    return {};
}

std::vector<RenumberedRecords> EditCommand::renumberedRecords() const
{
    return {};
}

PixelEditCommand::PixelEditCommand(EditKind kind,
                                   std::size_t index,
                                   render::Resolution resolution,
                                   std::vector<PixelChange> changes)
    : m_kind(kind),
      m_index(index),
      m_resolution(resolution),
      m_changes(std::move(changes))
{
    if (kind != EditKind::FramePixels && kind != EditKind::BackgroundImage &&
        kind != EditKind::SpriteImage) {
        throw std::invalid_argument("edit kind has no RGB565 pixels");
    }
}

void PixelEditCommand::apply(document::ProjectDocument& document) const
{
    write(document, true);
}

void PixelEditCommand::revert(document::ProjectDocument& document) const
{
    write(document, false);
}

EditTarget PixelEditCommand::target() const
{
    EditTarget target{m_kind, m_index, m_resolution, {}};
    if (m_kind == EditKind::FramePixels) {
        target.frameIndices.push_back(m_index);
    }
    return target;
}

std::vector<RecordReference> PixelEditCommand::recordReferences() const
{
    const RecordKind kind = m_kind == EditKind::FramePixels
                                ? RecordKind::Frame
                                : m_kind == EditKind::BackgroundImage ? RecordKind::Background
                                                                      : RecordKind::Sprite;
    return {{kind, m_index}};
}

void PixelEditCommand::write(document::ProjectDocument& document, bool useNewColor) const
{
    for (const PixelChange& change : m_changes) {
        const uint16_t color = useNewColor ? change.newColor : change.oldColor;
        if (m_kind == EditKind::FramePixels) {
            document.setColorizedPixel(m_index, m_resolution, change.x, change.y, color);
        } else if (m_kind == EditKind::BackgroundImage) {
            document.setBackgroundPixel(m_index, m_resolution, change.x, change.y, color);
        } else {
            document.setSpriteColorizedPixel(m_index, m_resolution, change.x, change.y, color);
        }
    }
}

BackgroundHdCreateCommand::BackgroundHdCreateCommand(std::size_t backgroundIndex,
                                                     document::Rgb565Frame frame)
    : m_backgroundIndex(backgroundIndex),
      m_frame(std::move(frame))
{
}

void BackgroundHdCreateCommand::apply(document::ProjectDocument& document) const
{
    document.setBackgroundFrame(m_backgroundIndex, render::Resolution::Hd, m_frame);
}

void BackgroundHdCreateCommand::revert(document::ProjectDocument& document) const
{
    document.removeBackgroundHdFrame(m_backgroundIndex);
}

EditTarget BackgroundHdCreateCommand::target() const
{
    return EditTarget{EditKind::BackgroundImage, m_backgroundIndex, render::Resolution::Hd, {}};
}

std::vector<RecordReference> BackgroundHdCreateCommand::recordReferences() const
{
    return {{RecordKind::Background, m_backgroundIndex}};
}

MaskEditCommand::MaskEditCommand(EditKind kind,
                                 std::size_t index,
                                 render::Resolution resolution,
                                 std::vector<MaskPixelChange> changes)
    : m_kind(kind),
      m_index(index),
      m_resolution(kind == EditKind::ComparisonMask ? render::Resolution::Sd : resolution),
      m_changes(std::move(changes))
{
    if (kind != EditKind::ComparisonMask && kind != EditKind::DynamicMask &&
        kind != EditKind::BackgroundMask) {
        throw std::invalid_argument("edit kind has no mask pixels");
    }
}

void MaskEditCommand::apply(document::ProjectDocument& document) const
{
    write(document, true);
}

void MaskEditCommand::revert(document::ProjectDocument& document) const
{
    write(document, false);
}

EditTarget MaskEditCommand::target() const
{
    EditTarget target{m_kind, m_index, m_resolution, {}};
    if (m_kind != EditKind::ComparisonMask) {
        target.frameIndices.push_back(m_index);
    }
    return target;
}

std::vector<RecordReference> MaskEditCommand::recordReferences() const
{
    return {{m_kind == EditKind::ComparisonMask ? RecordKind::ComparisonMask : RecordKind::Frame,
             m_index}};
}

void MaskEditCommand::write(document::ProjectDocument& document, bool useNewValue) const
{
    for (const MaskPixelChange& change : m_changes) {
        const uint8_t value = useNewValue ? change.newValue : change.oldValue;
        if (m_kind == EditKind::ComparisonMask) {
            document.setComparisonMaskPixel(m_index, change.x, change.y, value);
        } else if (m_kind == EditKind::DynamicMask) {
            document.setDynamicMaskPixel(m_index, m_resolution, change.x, change.y, value);
        } else {
            document.setBackgroundMaskPixel(m_index, m_resolution, change.x, change.y, value);
        }
    }
}

DynamicColorEditCommand::DynamicColorEditCommand(std::size_t frameIndex,
                                                 render::Resolution resolution,
                                                 std::size_t setIndex,
                                                 std::size_t originalColorIndex,
                                                 uint16_t oldColor,
                                                 uint16_t newColor)
    : m_frameIndex(frameIndex),
      m_resolution(resolution),
      m_setIndex(setIndex),
      m_originalColorIndex(originalColorIndex),
      m_oldColor(oldColor),
      m_newColor(newColor)
{
}

void DynamicColorEditCommand::apply(document::ProjectDocument& document) const
{
    document.setDynamicColor(m_frameIndex,
                             m_resolution,
                             m_setIndex,
                             m_originalColorIndex,
                             m_newColor);
}

void DynamicColorEditCommand::revert(document::ProjectDocument& document) const
{
    document.setDynamicColor(m_frameIndex,
                             m_resolution,
                             m_setIndex,
                             m_originalColorIndex,
                             m_oldColor);
}

EditTarget DynamicColorEditCommand::target() const
{
    return EditTarget{EditKind::DynamicColor, m_frameIndex, m_resolution, {m_frameIndex}};
}

std::vector<RecordReference> DynamicColorEditCommand::recordReferences() const
{
    return {{RecordKind::Frame, m_frameIndex}};
}

DynamicShadowEditCommand::DynamicShadowEditCommand(std::size_t frameIndex,
                                                   render::Resolution resolution,
                                                   std::size_t setIndex,
                                                   bool editDirection,
                                                   bool editColor,
                                                   DynamicShadowChange change)
    : m_frameIndex(frameIndex),
      m_resolution(resolution),
      m_setIndex(setIndex),
      m_editDirection(editDirection),
      m_editColor(editColor),
      m_change(change)
{
}

void DynamicShadowEditCommand::apply(document::ProjectDocument& document) const
{
    write(document, true);
}

void DynamicShadowEditCommand::revert(document::ProjectDocument& document) const
{
    write(document, false);
}

EditTarget DynamicShadowEditCommand::target() const
{
    return EditTarget{EditKind::DynamicShadow, m_frameIndex, m_resolution, {m_frameIndex}};
}

std::vector<RecordReference> DynamicShadowEditCommand::recordReferences() const
{
    return {{RecordKind::Frame, m_frameIndex}};
}

void DynamicShadowEditCommand::write(document::ProjectDocument& document, bool useNewValue) const
{
    if (m_editDirection) {
        document.setDynamicShadowDirection(m_frameIndex,
                                           m_resolution,
                                           m_setIndex,
                                           useNewValue ? m_change.newDirection
                                                       : m_change.oldDirection);
    }
    if (m_editColor) {
        document.setDynamicShadowColor(m_frameIndex,
                                       m_resolution,
                                       m_setIndex,
                                       useNewValue ? m_change.newColor : m_change.oldColor);
    }
}

PaletteEditCommand::PaletteEditCommand(std::size_t paletteIndex,
                                       std::vector<PaletteColorChange> changes)
    : m_paletteIndex(paletteIndex),
      m_changes(std::move(changes))
{
}

void PaletteEditCommand::apply(document::ProjectDocument& document) const
{
    write(document, true);
}

void PaletteEditCommand::revert(document::ProjectDocument& document) const
{
    write(document, false);
}

EditTarget PaletteEditCommand::target() const
{
    return EditTarget{EditKind::Palette, m_paletteIndex, render::Resolution::Sd, {}};
}

std::vector<RecordReference> PaletteEditCommand::recordReferences() const
{
    return {};
}

void PaletteEditCommand::write(document::ProjectDocument& document, bool useNewColor) const
{
    for (const PaletteColorChange& change : m_changes) {
        document.setPaletteColor(m_paletteIndex,
                                 change.colorIndex,
                                 useNewColor ? change.newColor : change.oldColor);
    }
}

namespace {

void writeFrameAttribute(document::ProjectDocument& document,
                         std::size_t frameIndex,
                         FrameAttribute attribute,
                         uint32_t value,
                         bool restoring)
{
    switch (attribute) {
        case FrameAttribute::ComparisonMaskId:
            document.setComparisonMaskId(frameIndex, static_cast<uint8_t>(value));
            break;
        case FrameAttribute::ShapeComparisonMode:
            if (restoring) {
                document.restoreShapeComparisonMode(frameIndex, static_cast<uint8_t>(value));
            } else {
                document.setShapeComparisonMode(frameIndex, static_cast<uint8_t>(value));
            }
            break;
        case FrameAttribute::BackgroundId:
            document.setBackgroundId(frameIndex, static_cast<uint16_t>(value));
            break;
        case FrameAttribute::TriggerId:
            document.setTriggerId(frameIndex, value);
            break;
        case FrameAttribute::DurationMs:
            document.setFrameDurationMs(frameIndex, value);
            break;
    }
}

}  // namespace

uint32_t frameAttributeValue(const document::ProjectDocument& document,
                             std::size_t frameIndex,
                             FrameAttribute attribute)
{
    switch (attribute) {
        case FrameAttribute::ComparisonMaskId:
            return document.comparisonMaskId(frameIndex);
        case FrameAttribute::ShapeComparisonMode:
            return document.shapeComparisonMode(frameIndex);
        case FrameAttribute::BackgroundId:
            return document.backgroundId(frameIndex);
        case FrameAttribute::TriggerId:
            return document.triggerId(frameIndex);
        case FrameAttribute::DurationMs:
            return document.frameDurationMs(frameIndex);
    }
    throw std::invalid_argument("unknown frame attribute");
}

FrameAttributeEditCommand::FrameAttributeEditCommand(std::size_t frameIndex,
                                                     FrameAttribute attribute,
                                                     uint32_t oldValue,
                                                     uint32_t newValue)
    : m_frameIndex(frameIndex),
      m_attribute(attribute),
      m_oldValue(oldValue),
      m_newValue(newValue)
{
    const uint32_t maxValue = attribute == FrameAttribute::ComparisonMaskId ||
                                      attribute == FrameAttribute::ShapeComparisonMode
                                  ? 0xffu
                                  : attribute == FrameAttribute::BackgroundId ? 0xffffu
                                                                              : 0xffffffffu;
    if (oldValue > maxValue || newValue > maxValue) {
        throw std::invalid_argument("frame attribute value out of range");
    }
}

void FrameAttributeEditCommand::apply(document::ProjectDocument& document) const
{
    writeFrameAttribute(document, m_frameIndex, m_attribute, m_newValue, false);
}

void FrameAttributeEditCommand::revert(document::ProjectDocument& document) const
{
    writeFrameAttribute(document, m_frameIndex, m_attribute, m_oldValue, true);
}

EditTarget FrameAttributeEditCommand::target() const
{
    return EditTarget{EditKind::FrameAttributes,
                      m_frameIndex,
                      render::Resolution::Sd,
                      {m_frameIndex}};
}

std::vector<RecordReference> FrameAttributeEditCommand::recordReferences() const
{
    std::vector<RecordReference> references{{RecordKind::Frame, m_frameIndex}};
    for (uint32_t value : {m_oldValue, m_newValue}) {
        if (m_attribute == FrameAttribute::ComparisonMaskId &&
            value != document::kNoComparisonMaskId) {
            references.push_back({RecordKind::ComparisonMask, value});
        } else if (m_attribute == FrameAttribute::BackgroundId &&
                   value != document::kNoBackgroundId) {
            references.push_back({RecordKind::Background, value});
        }
    }
    return references;
}

FrameAttribute FrameAttributeEditCommand::attribute() const
{
    return m_attribute;
}

FrameHdCommand::FrameHdCommand(std::size_t frameIndex,
                               document::FrameHdState before,
                               document::FrameHdState after)
    : m_frameIndex(frameIndex),
      m_before(std::move(before)),
      m_after(std::move(after))
{
}

void FrameHdCommand::apply(document::ProjectDocument& document) const
{
    document.setFrameHdState(m_frameIndex, m_after);
}

void FrameHdCommand::revert(document::ProjectDocument& document) const
{
    document.setFrameHdState(m_frameIndex, m_before);
}

EditTarget FrameHdCommand::target() const
{
    return EditTarget{EditKind::FrameAttributes,
                      m_frameIndex,
                      render::Resolution::Sd,
                      {m_frameIndex}};
}

std::vector<RecordReference> FrameHdCommand::recordReferences() const
{
    std::vector<RecordReference> references{{RecordKind::Frame, m_frameIndex}};
    for (const document::FrameHdState* state : {&m_before, &m_after}) {
        if (state->background) {
            references.push_back({RecordKind::Background, state->background->backgroundIndex});
        }
    }
    return references;
}

std::vector<ReplacedData> FrameHdCommand::replacedData() const
{
    const render::Resolution hd = render::Resolution::Hd;
    std::vector<ReplacedData> replaced{{EditKind::FramePixels, m_frameIndex, hd}};
    if (m_before.backgroundMask || m_after.backgroundMask) {
        replaced.push_back({EditKind::BackgroundMask, m_frameIndex, hd});
    }
    for (const document::FrameHdState* state : {&m_before, &m_after}) {
        if (state->background) {
            replaced.push_back({EditKind::BackgroundImage, state->background->backgroundIndex, hd});
        }
    }
    return replaced;
}

SpriteHdCommand::SpriteHdCommand(std::size_t spriteIndex,
                                 document::SpriteHdState before,
                                 document::SpriteHdState after)
    : m_spriteIndex(spriteIndex),
      m_before(std::move(before)),
      m_after(std::move(after))
{
}

void SpriteHdCommand::apply(document::ProjectDocument& document) const
{
    document.setSpriteHdState(m_spriteIndex, m_after);
}

void SpriteHdCommand::revert(document::ProjectDocument& document) const
{
    document.setSpriteHdState(m_spriteIndex, m_before);
}

EditTarget SpriteHdCommand::target() const
{
    return EditTarget{EditKind::SpriteAttributes, m_spriteIndex, render::Resolution::Sd, {}};
}

std::vector<RecordReference> SpriteHdCommand::recordReferences() const
{
    return {{RecordKind::Sprite, m_spriteIndex}};
}

std::vector<ReplacedData> SpriteHdCommand::replacedData() const
{
    return {{EditKind::SpriteImage, m_spriteIndex, render::Resolution::Hd}};
}

std::unique_ptr<EditCommand> createFrameHd(document::ProjectDocument& document,
                                           std::size_t frameIndex,
                                           document::FrameHdParts parts)
{
    // An HD-only project can still lack the HD image of the frame's background.
    if (document.hasHdFrame(frameIndex) &&
        !(parts.backgroundImage && document.frameHdGaps(frameIndex).background)) {
        return nullptr;
    }
    document::FrameHdState before = document.frameHdState(frameIndex, parts);
    try {
        document.createHdFrame(frameIndex);
        if (parts.backgroundMask) {
            document.upscaleBackgroundMaskToHd(frameIndex);
        }
        if (parts.backgroundImage) {
            document.upscaleAssignedBackgroundToHd(frameIndex);
        }
    } catch (...) {
        document.setFrameHdState(frameIndex, before);
        throw;
    }
    return std::make_unique<FrameHdCommand>(frameIndex,
                                            std::move(before),
                                            document.frameHdState(frameIndex, parts));
}

std::unique_ptr<EditCommand> removeFrameHd(document::ProjectDocument& document,
                                           std::size_t frameIndex)
{
    if (!document.hasHdFrame(frameIndex)) {
        return nullptr;
    }
    document::FrameHdState before = document.frameHdState(frameIndex, {});
    document.removeHdFrame(frameIndex);
    return std::make_unique<FrameHdCommand>(frameIndex,
                                            std::move(before),
                                            document.frameHdState(frameIndex, {}));
}

std::unique_ptr<EditCommand> createSpriteHd(document::ProjectDocument& document,
                                            std::size_t spriteIndex)
{
    if (document.hasHdSprite(spriteIndex)) {
        return nullptr;
    }
    document::SpriteHdState before = document.spriteHdState(spriteIndex);
    document.createHdSprite(spriteIndex);
    return std::make_unique<SpriteHdCommand>(spriteIndex,
                                             std::move(before),
                                             document.spriteHdState(spriteIndex));
}

std::unique_ptr<EditCommand> removeSpriteHd(document::ProjectDocument& document,
                                            std::size_t spriteIndex)
{
    if (!document.hasHdSprite(spriteIndex)) {
        return nullptr;
    }
    document::SpriteHdState before = document.spriteHdState(spriteIndex);
    document.removeHdSprite(spriteIndex);
    return std::make_unique<SpriteHdCommand>(spriteIndex,
                                             std::move(before),
                                             document.spriteHdState(spriteIndex));
}

}  // namespace colorizing::edit
