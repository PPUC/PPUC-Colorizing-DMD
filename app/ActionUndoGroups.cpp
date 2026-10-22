#include "ActionUndoGroups.h"

#include <QTimer>

namespace colorizing::app {

ActionUndoGroups::ActionUndoGroups(edit::UndoHistories& histories)
    : m_histories(histories)
{
}

std::size_t ActionUndoGroups::push(const edit::HistoryKey& key,
                                   std::unique_ptr<edit::EditCommand> command)
{
    if (!m_open) {
        m_histories.beginGroup();
        m_open = true;
        QTimer::singleShot(0, &m_context, [this]() {
            m_histories.endGroup();
            m_open = false;
        });
    }
    return m_histories.push(key, std::move(command));
}

}  // namespace colorizing::app
