#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include <QCoreApplication>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QToolButton;

namespace colorizing::app {

// What decides whether a record can be added or deleted.
struct RecordListPageState {
    bool hasProject = false;
    std::size_t recordCount = 0;
    std::size_t maxRecordCount = 0;
    // What a new record is made from is there, e.g. a current frame for a background.
    bool hasSource = true;
    bool hasSelection = false;
};

// The count beside New and Delete (issue #37): "Total: 12", or "3 of 12" while the list shows
// only some (Used by the current frame).
[[nodiscard]] QString recordCountText(int shown, int total);
// The rows of the list that are not hidden.
[[nodiscard]] int shownRows(const QListWidget& list);

// A page of the left browser: the list of the comparison masks, backgrounds or sprites with
// New and Delete above it. The list and what it shows belong to the owner; the page only
// reports what the user asked for, and the owner calls refresh().
class RecordListPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(RecordListPage)

public:
    RecordListPage(QListWidget* list,
                   const QString& newToolTip,
                   const QString& deleteToolTip,
                   QWidget* parent = nullptr);

    // New is enabled while the project can hold another record, Delete while one is
    // selected.
    void refresh(const RecordListPageState& state);
    // The count again, after the list's filter hid or showed rows.
    void updateCount();
    [[nodiscard]] QString countText() const;

    // New, Delete and the list, in the order the Tab key follows.
    [[nodiscard]] std::vector<QWidget*> tabOrder() const;

    std::function<void()> onNewRequested;
    std::function<void()> onDeleteRequested;

private:
    QListWidget* m_list = nullptr;
    QToolButton* m_newButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    QLabel* m_countLabel = nullptr;
    // The project's records, as the last refresh() gave them.
    std::size_t m_total = 0;
    bool m_hasProject = false;
};

}  // namespace colorizing::app
