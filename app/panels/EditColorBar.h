#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QCoreApplication>
#include <QWidget>

#include "document/ProjectDocument.h"
#include "edit/ColorSetEdits.h"

class QToolButton;

namespace colorizing::app {

class EditColorSwatch;

// The row under the toolbar with the draw color mode and the edit colors (IDC_COL1..16,
// IDC_1COLMODE, IDC_4COLMODE, IDC_COLTODYNA, IDC_DYNATOCOL of the Win32 editor). It shows and
// reports; the owner changes the document and calls refresh().
class EditColorBar final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(EditColorBar)

public:
    explicit EditColorBar(QWidget* parent = nullptr);

    // 4 or 16 swatches, as many as the project has colors, each with the shade it stands for
    // in color set replace mode; none without a document.
    void refresh(const document::ProjectDocument* document);
    void setSelectedSlot(std::optional<std::size_t> slot);
    void setDrawColorMode(edit::DrawColorMode mode);
    [[nodiscard]] edit::DrawColorMode drawColorMode() const;
    // The key that selects each slot, shown in the tool tips.
    void setSlotKeyNames(std::vector<QString> keyNames);

    std::function<void(std::size_t slot)> onSlotClicked;
    // Double click or the context menu: change the color.
    std::function<void(std::size_t slot)> onSlotEditRequested;
    // A color dragged from the palette grid or a favorite.
    std::function<void(std::size_t slot, uint16_t rgb565)> onColorDropped;
    // An edit color dragged onto another one: the two swap (issue #45).
    std::function<void(std::size_t from, std::size_t to)> onColorsSwapped;
    std::function<void(edit::DrawColorMode mode)> onDrawColorModeChanged;
    std::function<void()> onEditColorsToDynamicRequested;
    std::function<void()> onDynamicToEditColorsRequested;

private:
    void updateToolTips();

    QToolButton* m_singleColorButton = nullptr;
    QToolButton* m_colorSetReplaceButton = nullptr;
    std::vector<EditColorSwatch*> m_swatches;
    QToolButton* m_toDynamicButton = nullptr;
    QToolButton* m_fromDynamicButton = nullptr;
    std::vector<uint16_t> m_colors;
    std::vector<QString> m_keyNames;
    uint32_t m_colorCount = 0;
    std::optional<std::size_t> m_selectedSlot;
};

}  // namespace colorizing::app
