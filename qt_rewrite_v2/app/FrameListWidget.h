#pragma once

#include <QListWidget>

#include <functional>

namespace colorizing::app {

// The frame list. The selected frames can be dragged to another place in the list; the list
// does not move its rows itself but asks onMoveRequested to move the frames, and is then built
// again from the project.
class FrameListWidget final : public QListWidget {
public:
    explicit FrameListWidget(QWidget* parent = nullptr);

    // The row the dragged frames go before; count() for behind the last row.
    std::function<void(int beforeRow)> onMoveRequested;

    // The row a drop at the position puts the frames before, as dropEvent() works it out.
    [[nodiscard]] int dropRow(const QPoint& position) const;

protected:
    void dropEvent(QDropEvent* event) override;
};

}  // namespace colorizing::app
