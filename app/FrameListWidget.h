#pragma once

#include <QListWidget>

#include <functional>

namespace colorizing::app {

// The frame list. The selected frames can be dragged to another place in the list; the list
// does not move its rows itself but asks onMoveRequested to move the frames, and is then built
// again from the project.
//
// The list takes its own drags itself: in the icon mode of the frame strip, QListView accepts a
// drop only onto an item that takes drops, and frames do not. A vertical line between the
// frames shows where they go.
//
// As in the Win32 editor, a plain click on a frame of a selection of several frames keeps the
// selection and only makes that frame the current one (so a canvas selection can be checked
// on each before copying); a plain click on another frame selects that one alone.
class FrameListWidget final : public QListWidget {
public:
    explicit FrameListWidget(QWidget* parent = nullptr);

    // The row the dragged frames go before; count() for behind the last row.
    std::function<void(int beforeRow)> onMoveRequested;

    // The frame strip: one row of icons of gridSize cells, left to right, frames dragged to
    // another place. QListView turns dragging and the viewport's drops off for the Static
    // movement its icon mode needs here; they are turned on again last.
    void useStripLayout(QSize gridSize);

    // The row a drop at the position puts the frames before, as dropEvent() works it out.
    [[nodiscard]] int dropRow(const QPoint& position) const;

    // The row the line shows the frames going before during a drag; -1 without a drag.
    [[nodiscard]] int shownDropRow() const;

protected:
    [[nodiscard]] QItemSelectionModel::SelectionFlags selectionCommand(
        const QModelIndex& index, const QEvent* event = nullptr) const override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // As in the Win32 editor, a double click selects the frame alone: a selection of several
    // frames is dropped (issue #18).
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Accepts a drag of the list's own rows and shows where they would go.
    bool takeDrag(QDragMoveEvent* event);

    int m_dropRow = -1;
};

}  // namespace colorizing::app
