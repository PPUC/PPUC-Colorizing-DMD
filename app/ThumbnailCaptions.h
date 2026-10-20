#pragma once

#include <QPixmap>
#include <QStyledItemDelegate>

class QListWidget;

namespace colorizing::app {

// The rows of the left sidebar's lists as the frames of the strip: the thumbnail, and its
// caption below it, wrapped to the list's width, so that a narrow sidebar still shows all of
// it. A row without an icon is its caption alone.
class ThumbnailCaptionDelegate final : public QStyledItemDelegate {
public:
    explicit ThumbnailCaptionDelegate(QListWidget* list);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

private:
    // Where the caption goes in a row of the width.
    [[nodiscard]] int captionWidth() const;

    QListWidget* m_list = nullptr;
};

// The frame list's blue HD badge in the upper right corner of a thumbnail of the left sidebar.
void drawHdBadge(QPixmap& pixmap);

// Gives the list the delegate (and what it needs: rows that follow the list's width).
void showCaptionsBelowThumbnails(QListWidget* list);

}  // namespace colorizing::app
