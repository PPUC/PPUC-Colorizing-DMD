#include "edit/EditCommand.h"

#include <stdexcept>
#include <utility>

namespace colorizing::edit {

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

DynamicShadowEditCommand::DynamicShadowEditCommand(std::size_t ownerFrameIndex,
                                                   render::Resolution resolution,
                                                   std::size_t setIndex,
                                                   bool editDirection,
                                                   bool editColor,
                                                   std::vector<DynamicShadowChange> changes)
    : m_ownerFrameIndex(ownerFrameIndex),
      m_resolution(resolution),
      m_setIndex(setIndex),
      m_editDirection(editDirection),
      m_editColor(editColor),
      m_changes(std::move(changes))
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
    EditTarget target{EditKind::DynamicShadow, m_ownerFrameIndex, m_resolution, {}};
    target.frameIndices.reserve(m_changes.size());
    for (const DynamicShadowChange& change : m_changes) {
        target.frameIndices.push_back(change.frameIndex);
    }
    return target;
}

void DynamicShadowEditCommand::write(document::ProjectDocument& document, bool useNewValue) const
{
    for (const DynamicShadowChange& change : m_changes) {
        if (m_editDirection) {
            document.setDynamicShadowDirection(change.frameIndex,
                                               m_resolution,
                                               m_setIndex,
                                               useNewValue ? change.newDirection
                                                           : change.oldDirection);
        }
        if (m_editColor) {
            document.setDynamicShadowColor(change.frameIndex,
                                           m_resolution,
                                           m_setIndex,
                                           useNewValue ? change.newColor : change.oldColor);
        }
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

void PaletteEditCommand::write(document::ProjectDocument& document, bool useNewColor) const
{
    for (const PaletteColorChange& change : m_changes) {
        document.setPaletteColor(m_paletteIndex,
                                 change.colorIndex,
                                 useNewColor ? change.newColor : change.oldColor);
    }
}

}  // namespace colorizing::edit
