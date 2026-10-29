#pragma once

#include <QPixmap>
#include <QStyledItemDelegate>

#include <set>
#include <vector>

class QListWidget;

namespace colorizing::app {

// The rows of the left sidebar's lists as the frames of the strip: the thumbnail, and its
// caption below it, wrapped to the list's width, so that a narrow sidebar still shows all of
// it. A row without an icon is its caption alone. A row put first (putRecordsFirst()) has a
// frame in the highlight color; one no frame uses (markUnusedRecords()) a faded thumbnail with
// the badge "Unused".
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

// Orders a list of the left sidebar whose rows carry their record in Qt::UserRole: the rows of
// the records given first, then the others, each part by record, and marks the first ones for
// the delegate. With none given that is the order of the records, nothing marked. The current
// and the selected rows stay. True when the order or the marks changed.
bool putRecordsFirst(QListWidget* list, const std::set<int>& records);

// The mark of a row that putRecordsFirst() put first.
inline constexpr int kPutFirstRole = Qt::UserRole + 50;

// Marks the rows of a list of the left sidebar whose record (Qt::UserRole) used does not name
// as used, for the delegate; records past its end are not marked. True when a mark changed.
bool markUnusedRecords(QListWidget* list, const std::vector<bool>& used);

// The mark of a row that markUnusedRecords() marked.
inline constexpr int kUnusedRole = Qt::UserRole + 51;

}  // namespace colorizing::app
