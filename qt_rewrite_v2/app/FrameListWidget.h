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
class FrameListWidget final : public QListWidget {
public:
    explicit FrameListWidget(QWidget* parent = nullptr);

    // The row the dragged frames go before; count() for behind the last row.
    std::function<void(int beforeRow)> onMoveRequested;

    // The row a drop at the position puts the frames before, as dropEvent() works it out.
    [[nodiscard]] int dropRow(const QPoint& position) const;

    // The row the line shows the frames going before during a drag; -1 without a drag.
    [[nodiscard]] int shownDropRow() const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Accepts a drag of the list's own rows and shows where they would go.
    bool takeDrag(QDragMoveEvent* event);

    int m_dropRow = -1;
};

}  // namespace colorizing::app
