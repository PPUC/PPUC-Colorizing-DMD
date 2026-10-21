#include "FrameListWidget.h"

#include <QKeyEvent>

#include "ListSelection.h"

#include <QDropEvent>
#include <QMouseEvent>
#include <QPainter>

namespace colorizing::app {

FrameListWidget::FrameListWidget(QWidget* parent)
    : QListWidget(parent)
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    // The list draws its own line where the frames go (paintEvent()).
    setDropIndicatorShown(false);
}

void FrameListWidget::useStripLayout(QSize gridSize)
{
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(false);
    setWordWrap(true);
    setGridSize(gridSize);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // After the view mode and the movement, which turn them off. Drag events go to the
    // viewport, and setDragDropMode() only turns drops on for the list itself: without the
    // viewport's, every drop was refused and the dragged frame went back (issue #15).
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    viewport()->setAcceptDrops(true);
}

int FrameListWidget::dropRow(const QPoint& position) const
{
    const QModelIndex index = indexAt(position);
    if (!index.isValid()) {
        return count();
    }
    // The second half of a frame, the right one in a strip, is behind it.
    const QRect rect = visualRect(index);
    const bool behind = flow() == QListView::LeftToRight ? position.x() >= rect.center().x()
                                                          : position.y() >= rect.center().y();
    return behind ? index.row() + 1 : index.row();
}

int FrameListWidget::shownDropRow() const
{
    return m_dropRow;
}

bool FrameListWidget::isFrameStepKey(const QKeyEvent& event)
{
    const Qt::KeyboardModifiers others =
        event.modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    return (event.key() == Qt::Key_Left || event.key() == Qt::Key_Right) &&
           others == Qt::NoModifier;
}

void FrameListWidget::stepWithKey(const QKeyEvent& event)
{
    QKeyEvent copy(event.type(), event.key(), event.modifiers(), event.text(),
                   event.isAutoRepeat(), static_cast<ushort>(event.count()));
    keyPressEvent(&copy);
}

bool FrameListWidget::takeDrag(QDragMoveEvent* event)
{
    if (event->source() != this) {
        return false;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    const int row = dropRow(event->position().toPoint());
    if (row != m_dropRow) {
        m_dropRow = row;
        viewport()->update();
    }
    return true;
}

QItemSelectionModel::SelectionFlags FrameListWidget::selectionCommand(const QModelIndex& index,
                                                                     const QEvent* event) const
{
    if (event && index.isValid() &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease)) {
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        const Qt::KeyboardModifiers selecting =
            Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier;
        if (mouse->button() == Qt::LeftButton && !(mouse->modifiers() & selecting) &&
            selectionModel()->isSelected(index) &&
            selectionModel()->selectedIndexes().size() > 1) {
            return QItemSelectionModel::NoUpdate;
        }
    }
    return QListWidget::selectionCommand(index, event);
}

void FrameListWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!takeDrag(event)) {
        QListWidget::dragEnterEvent(event);
    }
}

void FrameListWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->source() == this) {
        // The item view's own handling scrolls the strip near its ends; its verdict is then
        // replaced.
        QAbstractItemView::dragMoveEvent(event);
    }
    if (!takeDrag(event)) {
        QListWidget::dragMoveEvent(event);
    }
}

void FrameListWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_dropRow = -1;
    viewport()->update();
    QListWidget::dragLeaveEvent(event);
}

void FrameListWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    QListWidget::mouseDoubleClickEvent(event);
    if (event->button() != Qt::LeftButton || event->modifiers() != Qt::NoModifier) {
        return;
    }
    QListWidgetItem* item = itemAt(event->position().toPoint());
    if (item && selectedItems().size() > 1) {
        selectSingleRow(*this, row(item));
    }
}

void FrameListWidget::paintEvent(QPaintEvent* event)
{
    QListWidget::paintEvent(event);
    if (m_dropRow < 0 || count() == 0) {
        return;
    }
    // Before the row's frame, or behind the last one.
    const bool behindLast = m_dropRow >= count();
    const QRect rect = visualRect(model()->index(behindLast ? count() - 1 : m_dropRow, 0));
    const bool across = flow() == QListView::LeftToRight;
    QPainter painter(viewport());
    painter.setPen(QPen(palette().highlight(), 3));
    if (across) {
        const int x = behindLast ? rect.right() + 1 : rect.left() - 1;
        painter.drawLine(x, rect.top(), x, rect.bottom());
    } else {
        const int y = behindLast ? rect.bottom() + 1 : rect.top() - 1;
        painter.drawLine(rect.left(), y, rect.right(), y);
    }
}

void FrameListWidget::dropEvent(QDropEvent* event)
{
    m_dropRow = -1;
    viewport()->update();
    // The rows stay as they are; the list is built again from the moved frames.
    const int row = dropRow(event->position().toPoint());
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (event->source() == this && onMoveRequested) {
        onMoveRequested(row);
    }
}

}  // namespace colorizing::app
