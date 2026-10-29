#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <QPoint>
#include <QToolButton>

class QMimeData;
class QObject;
class QWidget;

namespace colorizing::app {

// What the color grids, the dynamic colors and the edit colors drag: the color, and the slot
// it comes from with the widget that shows the slot. A drop on another place takes the color;
// a drop on another slot of the same widget swaps the two colors (issue #45).
inline constexpr char kRgb565MimeType[] = "application/x-colorizing-rgb565";

// What the drag of a slot's color carries.
[[nodiscard]] QMimeData* colorDragMimeData(const QObject* owner, std::size_t slot, uint16_t rgb565);
// Runs the drag of a slot's color from the source widget, for the owner that shows the slots.
void execColorDrag(QWidget* source, const QObject* owner, std::size_t slot, uint16_t rgb565);

// The color a drag carries.
[[nodiscard]] std::optional<uint16_t> draggedColor(const QMimeData* mimeData);
// The slot a drag comes from when the owner shows it; nothing for a drag from elsewhere.
[[nodiscard]] std::optional<std::size_t> draggedSlotOf(const QMimeData* mimeData,
                                                       const QObject* owner);

// A button that shows the color of a slot: it drags the color, and takes a color or a slot of
// the same owner dropped on it.
class ColorSlotButton : public QToolButton {
public:
    ColorSlotButton(std::size_t slot, const QObject* owner, QWidget* parent);

    [[nodiscard]] std::size_t slot() const { return m_slot; }
    // The color it drags; nothing while it has none to drag.
    void setSlotColor(std::optional<uint16_t> rgb565);

    // Another slot of the owner dropped on this one.
    std::function<void(std::size_t from, std::size_t to)> onSlotDropped;
    // A color from elsewhere dropped on this slot.
    std::function<void(std::size_t slot, uint16_t rgb565)> onColorDropped;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    [[nodiscard]] bool takes(const QMimeData* mimeData) const;

    std::size_t m_slot;
    const QObject* m_owner;
    std::optional<uint16_t> m_rgb565;
    QPoint m_dragStartPosition;
};

}  // namespace colorizing::app
