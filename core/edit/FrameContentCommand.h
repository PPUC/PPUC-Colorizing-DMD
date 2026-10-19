#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// One dynamic set of a frame: some of its colors and, optionally, its shadow.
struct DynamicSetChange {
    std::size_t setIndex = 0;
    std::vector<DynamicColorChange> colors;
    bool shadow = false;
    DynamicShadowChange shadowChange;
};

// What a paste, an auto copy, a scrolling fill or a recolor helper changes in one frame: the
// colorized pixels of one plane and the dynamic content, which exists on the document's
// dynamicPlane() only.
struct FrameContentChanges {
    render::Resolution plane = render::Resolution::Sd;
    std::vector<PixelChange> pixels;
    std::vector<MaskPixelChange> dynamicMask;
    std::vector<DynamicSetChange> dynamicSets;

    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool changesDynamicData() const;
};

// Collects the new values of one frame's content and turns them into FrameContentChanges with
// the old values read from the document. A later value for the same pixel or color replaces an
// earlier one; values equal to the document's are left out.
class FrameContentBuilder {
public:
    // plane must have a colorized frame for setPixel(); the dynamic setters need the frame's
    // dynamic data, on document.dynamicPlane(). Both throw like the document accessors.
    FrameContentBuilder(const document::ProjectDocument& document,
                        std::size_t frameIndex,
                        render::Resolution plane);

    void setPixel(render::Point point, uint16_t color);
    void setDynamicMask(render::Point point, uint8_t value);
    void setDynamicColor(std::size_t setIndex, std::size_t originalColorIndex, uint16_t color);
    void setDynamicShadow(std::size_t setIndex, uint8_t direction, uint16_t color);

    [[nodiscard]] FrameContentChanges changes() const;

private:
    const document::ProjectDocument& m_document;
    std::size_t m_frameIndex;
    render::Resolution m_plane;
    // -1 for "unchanged", else the new value.
    std::vector<int32_t> m_pixels;
    std::vector<int32_t> m_dynamicMask;
    std::vector<int32_t> m_dynamicColors;
    std::vector<int32_t> m_shadowDirections;
    std::vector<int32_t> m_shadowColors;
};

// The changes of FrameContentChanges as one undoable step for one frame. The command lives in
// the history of historyKind (FramePixels on the changes' plane, or DynamicMask on the dynamic
// plane), which the caller picks so that the step can be undone where it was made; the UI
// refreshes what it changes whatever the kind.
class FrameContentCommand final : public EditCommand {
public:
    // historyKind is FramePixels or DynamicMask. Throws std::invalid_argument otherwise.
    FrameContentCommand(std::size_t frameIndex,
                        render::Resolution dynamicPlane,
                        FrameContentChanges changes,
                        EditKind historyKind = EditKind::FramePixels);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

    [[nodiscard]] std::size_t frameIndex() const;
    [[nodiscard]] const FrameContentChanges& changes() const;

private:
    void check(const document::ProjectDocument& document) const;
    void write(document::ProjectDocument& document, bool useNewValues) const;

    std::size_t m_frameIndex;
    render::Resolution m_dynamicPlane;
    FrameContentChanges m_changes;
    EditKind m_historyKind;
};

}  // namespace colorizing::edit
