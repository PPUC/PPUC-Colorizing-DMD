#include "ThumbnailCaptions.h"

#include <QApplication>
#include <QCoreApplication>
#include <QListWidget>
#include <QItemSelectionModel>
#include <QPainter>
#include <QSignalBlocker>

#include <algorithm>
#include <vector>

namespace colorizing::app {

namespace {

constexpr int kMargin = 4;
constexpr int kGap = 2;

// The badge of a record no frame uses, in the upper left corner of its thumbnail.
void drawUnusedBadge(QPainter* painter, const QRect& thumbnail)
{
    painter->save();
    QFont font = painter->font();
    font.setPixelSize(10);
    font.setBold(true);
    painter->setFont(font);
    const QString text = QCoreApplication::translate("ThumbnailCaptionDelegate", "Unused");
    const QRect badge(thumbnail.left() + 2,
                      thumbnail.top() + 2,
                      painter->fontMetrics().horizontalAdvance(text) + 8,
                      13);
    painter->fillRect(badge, QColor(150, 60, 40));
    painter->setPen(Qt::white);
    painter->drawText(badge, Qt::AlignCenter, text);
    painter->restore();
}

}  // namespace

ThumbnailCaptionDelegate::ThumbnailCaptionDelegate(QListWidget* list)
    : QStyledItemDelegate(list),
      m_list(list)
{
}

int ThumbnailCaptionDelegate::captionWidth() const
{
    return std::max(40, m_list->viewport()->width() - 2 * kMargin);
}

QSize ThumbnailCaptionDelegate::sizeHint(const QStyleOptionViewItem& option,
                                         const QModelIndex& index) const
{
    const bool hasIcon = !index.data(Qt::DecorationRole).value<QIcon>().isNull();
    const QString text = index.data(Qt::DisplayRole).toString();
    const QRect textRect = option.fontMetrics.boundingRect(
        QRect(0, 0, captionWidth(), 10000), Qt::AlignHCenter | Qt::TextWordWrap, text);
    const int iconHeight = hasIcon ? m_list->iconSize().height() + kGap : 0;
    // The row's height must not depend on the icon being built yet (LazyListIcons): an icon
    // role that is set counts, whatever it holds.
    return QSize(m_list->viewport()->width(),
                 kMargin + iconHeight + std::max(textRect.height(), option.fontMetrics.height()) +
                     kMargin);
}

void ThumbnailCaptionDelegate::paint(QPainter* painter,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    QStyleOptionViewItem item = option;
    initStyleOption(&item, index);
    // The selection and hover background, as the list draws it.
    QStyle* style = item.widget ? item.widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &item, painter, item.widget);

    if (index.data(kPutFirstRole).toBool()) {
        painter->save();
        painter->setPen(QPen(option.palette.color(QPalette::Highlight), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(option.rect).adjusted(1, 1, -1, -1), 3, 3);
        painter->restore();
    }

    const QRect rect = option.rect.adjusted(kMargin, kMargin, -kMargin, -kMargin);
    int top = rect.top();
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    if (!icon.isNull()) {
        const QSize iconSize = m_list->iconSize();
        const QPixmap pixmap = icon.pixmap(iconSize);
        const QSize shown = pixmap.deviceIndependentSize().toSize().scaled(
            iconSize, Qt::KeepAspectRatio);
        const QRect target(rect.left() + (rect.width() - shown.width()) / 2,
                           top + (iconSize.height() - shown.height()) / 2,
                           shown.width(),
                           shown.height());
        // A record no frame uses is faded and says so: it is not exported.
        const bool unused = index.data(kUnusedRole).toBool();
        painter->save();
        if (unused) {
            painter->setOpacity(0.4);
        }
        painter->drawPixmap(target, pixmap);
        painter->restore();
        if (unused) {
            drawUnusedBadge(painter, target);
        }
        top += iconSize.height() + kGap;
    }
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    painter->save();
    painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    painter->setFont(option.font);
    painter->drawText(QRect(rect.left(), top, rect.width(), rect.bottom() - top + 1),
                      Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                      index.data(Qt::DisplayRole).toString());
    painter->restore();
}

void drawHdBadge(QPixmap& pixmap)
{
    QPainter painter(&pixmap);
    QFont font = painter.font();
    font.setPixelSize(10);
    font.setBold(true);
    painter.setFont(font);
    const QRect badge(pixmap.width() - 26, 2, 24, 13);
    painter.fillRect(badge, QColor(35, 95, 160));
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, QCoreApplication::translate("FrameThumbnail", "HD"));
}

void showCaptionsBelowThumbnails(QListWidget* list)
{
    list->setItemDelegate(new ThumbnailCaptionDelegate(list));
    list->setResizeMode(QListView::Adjust);
    list->setUniformItemSizes(false);
    list->setWordWrap(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

bool markUnusedRecords(QListWidget* list, const std::vector<bool>& used)
{
    bool changed = false;
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        const int record = item->data(Qt::UserRole).toInt();
        const bool unused = record >= 0 && static_cast<std::size_t>(record) < used.size() &&
                            !used[static_cast<std::size_t>(record)];
        if (item->data(kUnusedRole).toBool() != unused) {
            item->setData(kUnusedRole, unused);
            changed = true;
        }
    }
    return changed;
}

bool putRecordsFirst(QListWidget* list, const std::set<int>& records)
{
    std::vector<QListWidgetItem*> items;
    items.reserve(static_cast<std::size_t>(list->count()));
    for (int row = 0; row < list->count(); ++row) {
        items.push_back(list->item(row));
    }
    const auto first = [&records](const QListWidgetItem* item) {
        return records.count(item->data(Qt::UserRole).toInt()) > 0;
    };
    std::vector<QListWidgetItem*> ordered = items;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&first](const QListWidgetItem* a, const QListWidgetItem* b) {
                         const bool aFirst = first(a);
                         const bool bFirst = first(b);
                         if (aFirst != bFirst) {
                             return aFirst;
                         }
                         return a->data(Qt::UserRole).toInt() < b->data(Qt::UserRole).toInt();
                     });
    bool changed = false;
    for (QListWidgetItem* item : items) {
        if (item->data(kPutFirstRole).toBool() != first(item)) {
            item->setData(kPutFirstRole, first(item));
            changed = true;
        }
    }
    if (ordered == items) {
        return changed;
    }
    // Taking the rows out loses the current and the selected ones: they are set again, as
    // they were, without a signal (nothing changed for the list's users).
    const QSignalBlocker listBlocker(list);
    const QSignalBlocker selectionBlocker(list->selectionModel());
    QListWidgetItem* current = list->currentItem();
    std::vector<QListWidgetItem*> selected;
    for (QListWidgetItem* item : list->selectedItems()) {
        selected.push_back(item);
    }
    while (list->count() > 0) {
        list->takeItem(0);
    }
    for (QListWidgetItem* item : ordered) {
        list->addItem(item);
    }
    if (current) {
        list->setCurrentItem(current, QItemSelectionModel::NoUpdate);
    }
    for (QListWidgetItem* item : selected) {
        item->setSelected(true);
    }
    return true;
}

}  // namespace colorizing::app
