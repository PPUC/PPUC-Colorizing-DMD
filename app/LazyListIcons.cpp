#include "LazyListIcons.h"

#include <algorithm>

#include <QEvent>
#include <QListWidget>
#include <QScrollBar>
#include <QTimer>

namespace colorizing::app {

namespace {

constexpr int kBuiltRole = Qt::UserRole + 1000;
// Rows built beyond both ends of the view, so that a small scroll step shows no placeholder.
constexpr int kMarginRows = 2;

}  // namespace

LazyListIcons::LazyListIcons(QListWidget* list, QIcon placeholder, Builder builder)
    : QObject(list)
    , m_list(list)
    , m_placeholder(std::move(placeholder))
    , m_builder(std::move(builder))
    , m_timer(new QTimer(this))
{
    // One pass per event loop turn, however many scroll steps and changes asked for it.
    m_timer->setSingleShot(true);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (const std::optional<std::pair<int, int>> rows = rowsInView()) {
            for (int row = rows->first; row <= rows->second; ++row) {
                if (!m_list->isRowHidden(row)) {
                    ensureBuilt(row, row);
                }
            }
        }
    });
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        scheduleRowsInView();
    });
    connect(m_list->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        scheduleRowsInView();
    });
    // The frame strip scrolls across.
    connect(m_list->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        scheduleRowsInView();
    });
    connect(m_list->horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        scheduleRowsInView();
    });
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);
}

const QIcon& LazyListIcons::placeholder() const
{
    return m_placeholder;
}

bool LazyListIcons::isBuilt(const QListWidgetItem* item) const
{
    return item && item->data(kBuiltRole).toBool();
}

void LazyListIcons::invalidate(QListWidgetItem* item)
{
    if (!item || !isBuilt(item)) {
        return;
    }
    const int row = m_list->row(item);
    const std::optional<std::pair<int, int>> rows = rowsInView();
    if (rows && row >= rows->first && row <= rows->second) {
        item->setIcon(m_builder(*item));
        return;
    }
    item->setData(kBuiltRole, false);
    item->setIcon(m_placeholder);
}

void LazyListIcons::ensureBuilt(int firstRow, int lastRow)
{
    firstRow = std::max(firstRow, 0);
    lastRow = std::min(lastRow, m_list->count() - 1);
    for (int row = firstRow; row <= lastRow; ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (item && !isBuilt(item)) {
            item->setIcon(m_builder(*item));
            item->setData(kBuiltRole, true);
        }
    }
}

void LazyListIcons::scheduleRowsInView()
{
    if (!m_timer->isActive()) {
        m_timer->start();
    }
}

bool LazyListIcons::eventFilter(QObject* watched, QEvent* event)
{
    // A paint also notices what moves rows without touching a scroll bar: rows hidden, shown or
    // put in another order in a list that is not longer than its view.
    if (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
        event->type() == QEvent::Paint) {
        scheduleRowsInView();
    }
    return QObject::eventFilter(watched, event);
}

std::optional<std::pair<int, int>> LazyListIcons::rowsInView() const
{
    const int count = m_list->count();
    if (count == 0 || !m_list->isVisible()) {
        return std::nullopt;
    }
    const QAbstractItemModel* model = m_list->model();
    // Rows follow each other from top to bottom, or from left to right in a strip (the frame
    // strip): the first row that ends inside the view or past it, and the first one that
    // starts past it. Measured across in a strip; down, every row of a strip would be in view.
    const bool across = m_list->flow() == QListView::LeftToRight && !m_list->isWrapping();
    const int viewSize = across ? m_list->viewport()->width() : m_list->viewport()->height();
    const auto start = [across](const QRect& rect) { return across ? rect.left() : rect.top(); };
    const auto end = [across](const QRect& rect) { return across ? rect.right() : rect.bottom(); };
    // A hidden row has no place in the view: the search looks at the next row that has one.
    const auto firstRowWhere = [&](const std::function<bool(const QRect&)>& matches) {
        int low = 0;
        int high = count;
        while (low < high) {
            const int middle = low + (high - low) / 2;
            int shown = middle;
            while (shown < high && m_list->isRowHidden(shown)) {
                ++shown;
            }
            if (shown == high || matches(m_list->visualRect(model->index(shown, 0)))) {
                high = middle;
            } else {
                low = shown + 1;
            }
        }
        return low;
    };
    const int first = firstRowWhere([&](const QRect& rect) { return end(rect) >= 0; });
    const int past = firstRowWhere([&](const QRect& rect) { return start(rect) >= viewSize; });
    return std::make_pair(std::max(first - kMarginRows, 0),
                          std::min(past - 1 + kMarginRows, count - 1));
}

}  // namespace colorizing::app
