#include "FrameListWidget.h"

#include <QDropEvent>

namespace colorizing::app {

FrameListWidget::FrameListWidget(QWidget* parent)
    : QListWidget(parent)
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
}

int FrameListWidget::dropRow(const QPoint& position) const
{
    const QModelIndex index = indexAt(position);
    if (!index.isValid()) {
        return count();
    }
    // The lower half of a row is behind it.
    const QRect rect = visualRect(index);
    return position.y() >= rect.center().y() ? index.row() + 1 : index.row();
}

void FrameListWidget::dropEvent(QDropEvent* event)
{
    // The rows stay as they are; the list is built again from the moved frames.
    const int row = dropRow(event->position().toPoint());
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (event->source() == this && onMoveRequested) {
        onMoveRequested(row);
    }
}

}  // namespace colorizing::app
