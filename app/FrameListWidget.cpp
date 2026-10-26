#include "FrameListWidget.h"

#include <QKeyEvent>

#include "Appearance.h"
#include "ListSelection.h"

#include <QDropEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyleOptionSlider>

#include <algorithm>

namespace colorizing::app {

namespace {

// A scroll bar with a tick for each marked row of the list, where the row is in the whole
// list, so a part with marked frames can be scrolled to.
class MarkedRowsScrollBar final : public QScrollBar {
public:
    MarkedRowsScrollBar(Qt::Orientation orientation, const FrameListWidget* list)
        : QScrollBar(orientation)
        , m_list(list)
    {
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QScrollBar::paintEvent(event);
        const std::vector<int>& rows = m_list->markedRows();
        const int count = m_list->count();
        if (rows.empty() || count <= 0) {
            return;
        }
        QStyleOptionSlider option;
        initStyleOption(&option);
        QRect groove = style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                               QStyle::SC_ScrollBarGroove, this);
        if (groove.isEmpty()) {
            groove = rect();
        }
        // Along the outer edge, a third of the bar thick, so the handle stays visible.
        const bool across = orientation() == Qt::Horizontal;
        const int length = across ? groove.width() : groove.height();
        const int thickness = std::max(3, (across ? groove.height() : groove.width()) / 3);
        QPainter painter(this);
        int lastPosition = -1;
        for (const int row : rows) {
            const int position =
                static_cast<int>((static_cast<double>(row) + 0.5) * length / count);
            if (position == lastPosition) {
                continue;
            }
            lastPosition = position;
            const QRect tick = across ? QRect(groove.left() + position - 1,
                                              groove.bottom() - thickness + 1, 2, thickness)
                                      : QRect(groove.right() - thickness + 1,
                                              groove.top() + position - 1, thickness, 2);
            painter.fillRect(tick, markedFrameColor());
        }
    }

private:
    const FrameListWidget* m_list;
};

}  // namespace

FrameListWidget::FrameListWidget(QWidget* parent)
    : QListWidget(parent)
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    // The list draws its own line where the frames go (paintEvent()).
    setDropIndicatorShown(false);
    setHorizontalScrollBar(new MarkedRowsScrollBar(Qt::Horizontal, this));
    setVerticalScrollBar(new MarkedRowsScrollBar(Qt::Vertical, this));
    connect(this, &QListWidget::currentRowChanged, this, [this]() {
        if (m_shownMarkedRow >= 0) {
            m_shownMarkedRow = -1;
            viewport()->update();
        }
    });
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

bool FrameListWidget::selectCurrentAlone()
{
    if (selectedItems().size() <= 1 || currentRow() < 0) {
        return false;
    }
    selectSingleRow(*this, currentRow());
    return true;
}

void FrameListWidget::setClaimBadges(std::map<int, QImage> badges)
{
    m_claimBadges = std::move(badges);
    viewport()->update();
}

QImage FrameListWidget::claimBadge(const QImage& avatar, int size, qreal ratio)
{
    const int pixels = static_cast<int>(size * ratio);
    QImage badge(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    badge.fill(Qt::transparent);
    QPainter painter(&badge);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF whole(0, 0, pixels, pixels);
    // A light ring, so that the badge stands out from dark and light frames alike.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawEllipse(whole);
    const QRectF inner = whole.adjusted(pixels * 0.1, pixels * 0.1, -pixels * 0.1, -pixels * 0.1);
    if (!avatar.isNull()) {
        QPainterPath circle;
        circle.addEllipse(inner);
        painter.setClipPath(circle);
        painter.drawImage(inner, avatar);
        return badge;
    }
    // A lock: the shackle above the body.
    const QColor lock(70, 70, 70);
    const qreal s = inner.width();
    const QPointF o = inner.topLeft();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(lock, s * 0.12));
    painter.drawArc(QRectF(o.x() + s * 0.28, o.y() + s * 0.08, s * 0.44, s * 0.5), 0, 180 * 16);
    painter.drawLine(QPointF(o.x() + s * 0.28, o.y() + s * 0.33),
                     QPointF(o.x() + s * 0.28, o.y() + s * 0.45));
    painter.drawLine(QPointF(o.x() + s * 0.72, o.y() + s * 0.33),
                     QPointF(o.x() + s * 0.72, o.y() + s * 0.45));
    painter.setPen(Qt::NoPen);
    painter.setBrush(lock);
    painter.drawRoundedRect(QRectF(o.x() + s * 0.18, o.y() + s * 0.45, s * 0.64, s * 0.45),
                            s * 0.08, s * 0.08);
    return badge;
}

void FrameListWidget::setMarkedRows(std::vector<int> rows)
{
    if (rows == m_markedRows) {
        return;
    }
    m_markedRows = std::move(rows);
    if (!std::binary_search(m_markedRows.begin(), m_markedRows.end(), m_shownMarkedRow)) {
        m_shownMarkedRow = -1;
    }
    viewport()->update();
    horizontalScrollBar()->update();
    verticalScrollBar()->update();
}

const std::vector<int>& FrameListWidget::markedRows() const
{
    return m_markedRows;
}

bool FrameListWidget::isMarkedRowStepKey(const QKeyEvent& event)
{
    return (event.key() == Qt::Key_Left || event.key() == Qt::Key_Right) &&
           (event.modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier;
}

int FrameListWidget::showMarkedRow(int direction)
{
    const int from = m_shownMarkedRow >= 0 ? m_shownMarkedRow : currentRow();
    int row = -1;
    if (direction < 0) {
        const auto before = std::lower_bound(m_markedRows.begin(), m_markedRows.end(), from);
        if (before != m_markedRows.begin()) {
            row = *(before - 1);
        }
    } else {
        const auto after = std::upper_bound(m_markedRows.begin(), m_markedRows.end(), from);
        if (after != m_markedRows.end()) {
            row = *after;
        }
    }
    if (row < 0 || row >= count()) {
        return -1;
    }
    m_shownMarkedRow = row;
    scrollToItem(item(row), QAbstractItemView::PositionAtCenter);
    viewport()->update();
    return row;
}

int FrameListWidget::shownMarkedRow() const
{
    return m_shownMarkedRow;
}

void FrameListWidget::keyPressEvent(QKeyEvent* event)
{
    if (isMarkedRowStepKey(*event)) {
        // Taken also without a marked row, so that the current frame never moves.
        showMarkedRow(event->key() == Qt::Key_Left ? -1 : 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier &&
        selectCurrentAlone()) {
        event->accept();
        return;
    }
    const bool stepping = isFrameStepKey(*event);
    m_steppingWithKey = stepping;
    QListWidget::keyPressEvent(event);
    m_steppingWithKey = false;
}

void FrameListWidget::wheelEvent(QWheelEvent* event)
{
    if (flow() != QListView::LeftToRight || isWrapping() || count() < 2 ||
        event->modifiers() != Qt::NoModifier) {
        QListWidget::wheelEvent(event);
        return;
    }
    const QPoint angle = event->angleDelta();
    m_wheelRemainder += angle.y() != 0 ? angle.y() : angle.x();
    const int turns = m_wheelRemainder / 120;
    m_wheelRemainder -= turns * 120;
    event->accept();
    if (turns == 0) {
        return;
    }
    QScrollBar* bar = horizontalScrollBar();
    // Where frame 0 and the next one begin in the strip's content.
    const int first = visualRect(model()->index(0, 0)).left() + bar->value();
    const int step = visualRect(model()->index(1, 0)).left() + bar->value() - first;
    if (step <= 0) {
        return;
    }
    // The frame shown first, the one partly shown counting as passed when going on; a turn
    // down (negative) goes on.
    const int at = bar->value();
    const int shown = turns < 0 ? at / step : (at + step - 1) / step;
    const int row = std::clamp(shown - turns, 0, count() - 1);
    bar->setValue(row * step);
}

void FrameListWidget::paintEvent(QPaintEvent* event)
{
    QListWidget::paintEvent(event);
    QPainter painter(viewport());
    const QRect shown = viewport()->rect();
    for (const int row : m_markedRows) {
        if (row >= count()) {
            break;
        }
        const QRect rect = visualRect(model()->index(row, 0));
        if (!rect.intersects(shown)) {
            continue;
        }
        const int width = row == m_shownMarkedRow ? 4 : 2;
        painter.setPen(QPen(markedFrameColor(), width));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect.adjusted(width / 2, width / 2, -(width + 1) / 2, -(width + 1) / 2));
    }
    if (!m_claimBadges.empty()) {
        constexpr int kBadgeSize = 18;
        const qreal ratio = viewport()->devicePixelRatioF();
        for (const auto& [row, avatar] : m_claimBadges) {
            if (row >= count()) {
                break;
            }
            const QRect rect = visualRect(model()->index(row, 0));
            if (rect.intersects(shown)) {
                painter.drawImage(QRect(rect.topLeft() + QPoint(3, 3),
                                        QSize(kBadgeSize, kBadgeSize)),
                                  claimBadge(avatar, kBadgeSize, ratio));
            }
        }
    }
    if (m_dropRow < 0 || count() == 0) {
        return;
    }
    // Before the row's frame, or behind the last one.
    const bool behindLast = m_dropRow >= count();
    const QRect rect = visualRect(model()->index(behindLast ? count() - 1 : m_dropRow, 0));
    const bool across = flow() == QListView::LeftToRight;
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
