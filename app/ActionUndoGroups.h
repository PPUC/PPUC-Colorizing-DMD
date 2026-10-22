#pragma once

#include <cstddef>
#include <memory>

#include <QObject>

#include "edit/UndoHistories.h"

namespace colorizing::app {

// Makes what one user action records one undo step (UndoHistories groups), whatever it
// changes and in however many histories: the group opens at the action's first push and
// closes once the action has returned to the event loop.
class ActionUndoGroups {
public:
    explicit ActionUndoGroups(edit::UndoHistories& histories);

    std::size_t push(const edit::HistoryKey& key, std::unique_ptr<edit::EditCommand> command);

private:
    edit::UndoHistories& m_histories;
    bool m_open = false;
    // Receives the closing call; it goes with this object.
    QObject m_context;
};

}  // namespace colorizing::app
