#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QPoint>
#include <QToolButton>

class QMimeData;
class QPainter;
class QObject;
class QWidget;

namespace colorizing::app {

// What the color grids, the dynamic colors and the edit colors drag: the color, and the slot
// it comes from with the widget that shows the slot. A drop on another place takes the color;
// a drop on another slot of the same widget swaps the two colors (issue #45). A drag started
// with Shift held is a gradient drag: on another slot of the same widget it blends the colors
// between the two, as the Win32 editor's palette did (issue #77).
inline constexpr char kRgb565MimeType[] = "application/x-colorizing-rgb565";

// What the drag of a slot's color carries.
[[nodiscard]] QMimeData* colorDragMimeData(const QObject* owner,
                                           std::size_t slot,
                                           uint16_t rgb565,
                                           bool gradient = false);
// Runs the drag of a slot's color from the source widget, for the owner that shows the slots.
void execColorDrag(QWidget* source,
                   const QObject* owner,
                   std::size_t slot,
                   uint16_t rgb565,
                   bool gradient = false);
// Whether the drag was started with Shift held.
[[nodiscard]] bool isGradientDrag(const QMimeData* mimeData);

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
    // Another slot of the owner dragged here with Shift held at the press: the colors between
    // the two blend from the dragged one to this one. Without a handler it swaps as any drop.
    std::function<void(std::size_t from, std::size_t to)> onSlotsGradient;

    // While a gradient drag is over a slot of the same owner, the slots between show the color
    // the drop would give them, and the two ends are marked (issue #77); a swap drag shows
    // nothing of that kind. Nothing while no gradient drag is over the row.
    [[nodiscard]] std::optional<uint16_t> gradientPreview() const { return m_gradientPreview; }
    [[nodiscard]] bool isGradientEnd() const { return m_gradientEnd; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    // Draws the gradient preview over the button; a subclass that paints itself calls it last.
    void paintGradientPreview(QPainter& painter) const;

private:
    [[nodiscard]] bool takes(const QMimeData* mimeData) const;
    // The owner's slot buttons, this one included.
    [[nodiscard]] std::vector<ColorSlotButton*> rowButtons() const;
    void showGradientPreview(std::size_t from, uint16_t fromColor);
    void clearGradientPreview();

    std::size_t m_slot;
    const QObject* m_owner;
    std::optional<uint16_t> m_rgb565;
    QPoint m_dragStartPosition;
    bool m_gradientDrag = false;
    std::optional<uint16_t> m_gradientPreview;
    bool m_gradientEnd = false;
};

// The toolbar's active draw color (issue #68): it drags the color onto a palette color, a
// favorite, an edit color or a dynamic color, takes a color dropped on it, and a double click
// opens the color picker with it.
class DrawColorButton final : public ColorSlotButton {
public:
    explicit DrawColorButton(QWidget* parent = nullptr);

    std::function<void()> onDoubleClicked;

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
};

// A favorite draw color of the toolbar: it takes a color dropped on it, and a double click
// edits it, as the context menu's Edit Color does.
class FavoriteColorButton final : public QToolButton {
public:
    explicit FavoriteColorButton(QWidget* parent = nullptr);

    std::function<void(uint16_t rgb565)> onColorDropped;
    std::function<void()> onDoubleClicked;

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

}  // namespace colorizing::app
