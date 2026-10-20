#include "ThumbnailCaptions.h"

#include <QApplication>
#include <QListWidget>
#include <QPainter>

#include <algorithm>

namespace colorizing::app {

namespace {

constexpr int kMargin = 4;
constexpr int kGap = 2;

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

    const QRect rect = option.rect.adjusted(kMargin, kMargin, -kMargin, -kMargin);
    int top = rect.top();
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    if (!icon.isNull()) {
        const QSize iconSize = m_list->iconSize();
        const QPixmap pixmap = icon.pixmap(iconSize);
        const QSize shown = pixmap.deviceIndependentSize().toSize().scaled(
            iconSize, Qt::KeepAspectRatio);
        painter->drawPixmap(QRect(rect.left() + (rect.width() - shown.width()) / 2,
                                  top + (iconSize.height() - shown.height()) / 2,
                                  shown.width(),
                                  shown.height()),
                            pixmap);
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

void showCaptionsBelowThumbnails(QListWidget* list)
{
    list->setItemDelegate(new ThumbnailCaptionDelegate(list));
    list->setResizeMode(QListView::Adjust);
    list->setUniformItemSizes(false);
    list->setWordWrap(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

}  // namespace colorizing::app
