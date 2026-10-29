#include "FlowLayout.h"

#include <QWidget>

#include <algorithm>
#include <utility>
#include <vector>

namespace colorizing::app {

FlowLayout::FlowLayout(QWidget* parent, int horizontalSpacing, int verticalSpacing)
    : QLayout(parent),
      m_horizontalSpacing(horizontalSpacing),
      m_verticalSpacing(verticalSpacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::setStretchLastItem(bool stretch, int minimumWidth)
{
    m_stretchLast = stretch;
    m_lastMinimumWidth = minimumWidth;
    invalidate();
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return static_cast<int>(m_items.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return index >= 0 && index < m_items.size() ? m_items.at(index) : nullptr;
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return arrange(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const
{
    // As narrow as the widest item: everything else wraps.
    QSize size;
    for (const QLayoutItem* item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, false);
}

int FlowLayout::arrange(const QRect& rect, bool testOnly) const
{
    const QRect area = rect.marginsRemoved(contentsMargins());
    int x = area.x();
    int y = area.y();
    int rowHeight = 0;
    const auto isVisible = [](const QLayoutItem* item) {
        return !item->widget() || !item->widget()->isHidden();
    };
    int lastVisible = -1;
    for (int index = 0; index < m_items.size(); ++index) {
        if (isVisible(m_items.at(index))) {
            lastVisible = index;
        }
    }
    // The items of the current row, placed once the row is complete: each centered in the
    // row's height, so that a group of lower buttons lines up with a taller field beside it.
    std::vector<std::pair<QLayoutItem*, QRect>> row;
    const auto placeRow = [&row, &rowHeight, testOnly]() {
        if (!testOnly) {
            for (auto& [item, geometry] : row) {
                geometry.translate(0, (rowHeight - geometry.height()) / 2);
                item->setGeometry(geometry);
            }
        }
        row.clear();
    };
    for (int index = 0; index < m_items.size(); ++index) {
        QLayoutItem* item = m_items.at(index);
        if (!isVisible(item)) {
            continue;
        }
        const bool stretches = m_stretchLast && index == lastVisible;
        QSize size = item->sizeHint();
        if (stretches) {
            size.setWidth(m_lastMinimumWidth);
        }
        // A new row when it does not fit, unless it is the first of its row.
        if (x > area.x() && x + size.width() > area.right() + 1) {
            placeRow();
            x = area.x();
            y += rowHeight + m_verticalSpacing;
            rowHeight = 0;
        }
        if (stretches) {
            size.setWidth(std::max(size.width(), area.right() + 1 - x));
        }
        row.emplace_back(item, QRect(QPoint(x, y), size));
        x += size.width() + m_horizontalSpacing;
        rowHeight = std::max(rowHeight, size.height());
    }
    placeRow();
    const QMargins margins = contentsMargins();
    return y + rowHeight - rect.y() + margins.bottom();
}

}  // namespace colorizing::app
