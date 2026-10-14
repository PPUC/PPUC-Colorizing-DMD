#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/FrameContentCommand.h"
#include "edit/PlaneSelection.h"
#include "render/ProjectGeometry.h"

namespace colorizing::edit {

// A dynamic set of the source frame that copied pixels belong to, as the Win32 editor's
// CopyDynaCol carries it to the frame pasted on: its colors and shadow.
struct CopiedDynamicSet {
    std::size_t setIndex = 0;
    std::vector<uint16_t> colors;
    uint8_t shadowDirection = 0;
    uint16_t shadowColor = 0;
};

// The selected part of a frame (IDC_COPY: Paste_Mask, Paste_ColN, Paste_Colo, Paste_Dyna), cut
// to the selection's bounding box. Per box pixel: whether it is selected, the colorized color on
// the plane, the original shade of the ROM frame pixel it shows (toRomFrame) and, when the plane
// is the dynamic plane, the dynamic mask value.
struct CopyBuffer {
    render::ProjectGeometry geometry;
    uint32_t colorCount = 4;
    render::Resolution plane = render::Resolution::Sd;
    std::size_t sourceFrame = 0;
    // The box on the source plane.
    render::Rect box;
    std::vector<uint8_t> mask;
    std::vector<uint16_t> colors;
    std::vector<uint8_t> originals;
    // Empty unless the plane is the dynamic plane and the frame has dynamic data.
    std::vector<uint8_t> dynamicMask;
    // The sets the copied dynamic pixels use, ascending.
    std::vector<CopiedDynamicSet> dynamicSets;

    [[nodiscard]] bool hasDynamicData() const { return !dynamicMask.empty(); }
    [[nodiscard]] std::size_t selectedCount() const;
};

// Throws std::invalid_argument for an empty selection and std::logic_error for a selection of
// a plane the frame has no colorized frame on.
[[nodiscard]] CopyBuffer copySelection(const document::ProjectDocument& document,
                                       std::size_t frameIndex,
                                       const PlaneSelection& selection);

// Where the box lands on the target plane (its top left corner) and how it is turned.
struct PastePlacement {
    int x = 0;
    int y = 0;
    bool mirrorHorizontally = false;
    bool mirrorVertically = false;
};

enum class PasteMode {
    // Colors, dynamic mask and the dynamic sets used (Win32: click).
    All,
    // The same, only on pixels whose ROM shade equals the copied one (Win32: Shift+click).
    WhereOriginalsMatch,
    // The dynamic mask alone (Win32: Alt+click).
    DynamicMaskOnly,
};

// The copied pixel that lands on a box position, after mirroring.
[[nodiscard]] std::size_t pasteSourceIndex(const CopyBuffer& buffer,
                                           const PastePlacement& placement,
                                           int boxX,
                                           int boxY);

// Why the buffer cannot be pasted on that plane of the document, or nothing. A buffer is
// pasted only on the plane of the same project layout it was copied from (the Win32 editor's
// Copy_Content), and dynamic content only on the dynamic plane.
[[nodiscard]] std::optional<std::string> pasteRefusal(const document::ProjectDocument& document,
                                                      const CopyBuffer& buffer,
                                                      render::Resolution plane,
                                                      PasteMode mode);

// The changes of pasting on one frame; pixels that fall outside the plane are clipped. Frames
// without a colorized frame on the plane are left alone (empty changes). Throws
// std::invalid_argument when pasteRefusal() gives a reason.
[[nodiscard]] FrameContentChanges makePasteChanges(const document::ProjectDocument& document,
                                                   const CopyBuffer& buffer,
                                                   std::size_t targetFrame,
                                                   render::Resolution plane,
                                                   const PastePlacement& placement,
                                                   PasteMode mode);

}  // namespace colorizing::edit
