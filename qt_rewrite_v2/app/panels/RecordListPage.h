#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include <QCoreApplication>
#include <QString>
#include <QWidget>

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

    // New, Delete and the list, in the order the Tab key follows.
    [[nodiscard]] std::vector<QWidget*> tabOrder() const;

    std::function<void()> onNewRequested;
    std::function<void()> onDeleteRequested;

private:
    QListWidget* m_list = nullptr;
    QToolButton* m_newButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
};

}  // namespace colorizing::app
