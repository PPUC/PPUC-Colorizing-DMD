#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "document/ProjectDocument.h"
#include "render/CanvasTypes.h"

namespace colorizing::edit {

enum class EditKind {
    FramePixels,
    ComparisonMask,
    DynamicMask,
    DynamicColor,
    DynamicShadow,
    BackgroundMask,
    BackgroundImage,
    SpriteImage,
    Palette,
};

// What a command touches, so the UI knows what to refresh and which history owns the command.
struct EditTarget {
    EditKind kind = EditKind::FramePixels;
    // Frame, comparison mask, background, sprite or palette index, depending on the kind.
    std::size_t index = 0;
    // Always Sd for comparison masks and palettes, which have no resolution.
    render::Resolution resolution = render::Resolution::Sd;
    // Frames whose own data changed. Empty for masks shared between frames, backgrounds,
    // sprites and palettes.
    std::vector<std::size_t> frameIndices;
};

class EditCommand {
public:
    virtual ~EditCommand() = default;

    virtual void apply(document::ProjectDocument& document) const = 0;
    virtual void revert(document::ProjectDocument& document) const = 0;
    [[nodiscard]] virtual EditTarget target() const = 0;
};

struct PixelChange {
    int x = 0;
    int y = 0;
    uint16_t oldColor = 0;
    uint16_t newColor = 0;
};

// RGB565 pixels of a colorized frame, a background image or a sprite.
class PixelEditCommand final : public EditCommand {
public:
    // kind must be FramePixels, BackgroundImage or SpriteImage.
    PixelEditCommand(EditKind kind,
                     std::size_t index,
                     render::Resolution resolution,
                     std::vector<PixelChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;

private:
    void write(document::ProjectDocument& document, bool useNewColor) const;

    EditKind m_kind;
    std::size_t m_index;
    render::Resolution m_resolution;
    std::vector<PixelChange> m_changes;
};

struct MaskPixelChange {
    int x = 0;
    int y = 0;
    uint8_t oldValue = 0;
    uint8_t newValue = 0;
};

// Pixels of a comparison mask, or of a frame's dynamic or background mask.
class MaskEditCommand final : public EditCommand {
public:
    // kind must be ComparisonMask, DynamicMask or BackgroundMask. The resolution is ignored
    // for comparison masks.
    MaskEditCommand(EditKind kind,
                    std::size_t index,
                    render::Resolution resolution,
                    std::vector<MaskPixelChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;

private:
    void write(document::ProjectDocument& document, bool useNewValue) const;

    EditKind m_kind;
    std::size_t m_index;
    render::Resolution m_resolution;
    std::vector<MaskPixelChange> m_changes;
};

class DynamicColorEditCommand final : public EditCommand {
public:
    DynamicColorEditCommand(std::size_t frameIndex,
                            render::Resolution resolution,
                            std::size_t setIndex,
                            std::size_t originalColorIndex,
                            uint16_t oldColor,
                            uint16_t newColor);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;

private:
    std::size_t m_frameIndex;
    render::Resolution m_resolution;
    std::size_t m_setIndex;
    std::size_t m_originalColorIndex;
    uint16_t m_oldColor;
    uint16_t m_newColor;
};

struct DynamicShadowChange {
    std::size_t frameIndex = 0;
    uint8_t oldDirection = 0;
    uint8_t newDirection = 0;
    uint16_t oldColor = 0;
    uint16_t newColor = 0;
};

// One dynamic set's shadow direction and/or color, changed on several frames at once.
class DynamicShadowEditCommand final : public EditCommand {
public:
    // ownerFrameIndex is the frame the edit was made from; it becomes the target index and may
    // differ from the frames listed in changes.
    DynamicShadowEditCommand(std::size_t ownerFrameIndex,
                             render::Resolution resolution,
                             std::size_t setIndex,
                             bool editDirection,
                             bool editColor,
                             std::vector<DynamicShadowChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;

private:
    void write(document::ProjectDocument& document, bool useNewValue) const;

    std::size_t m_ownerFrameIndex;
    render::Resolution m_resolution;
    std::size_t m_setIndex;
    bool m_editDirection;
    bool m_editColor;
    std::vector<DynamicShadowChange> m_changes;
};

struct PaletteColorChange {
    std::size_t colorIndex = 0;
    uint16_t oldColor = 0;
    uint16_t newColor = 0;
};

class PaletteEditCommand final : public EditCommand {
public:
    PaletteEditCommand(std::size_t paletteIndex, std::vector<PaletteColorChange> changes);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;

private:
    void write(document::ProjectDocument& document, bool useNewColor) const;

    std::size_t m_paletteIndex;
    std::vector<PaletteColorChange> m_changes;
};

}  // namespace colorizing::edit
