#include "edit/UndoHistories.h"

#include <stdexcept>
#include <utility>

namespace colorizing::edit {

bool HistoryKey::operator<(const HistoryKey& other) const
{
    if (kind != other.kind) {
        return static_cast<int>(kind) < static_cast<int>(other.kind);
    }
    if (index != other.index) {
        return index < other.index;
    }
    return static_cast<int>(resolution) < static_cast<int>(other.resolution);
}

bool HistoryKey::operator==(const HistoryKey& other) const
{
    return kind == other.kind && index == other.index && resolution == other.resolution;
}

HistoryKey historyKey(const EditTarget& target)
{
    return HistoryKey{target.kind, target.index, target.resolution};
}

void UndoHistories::push(const HistoryKey& key, std::unique_ptr<EditCommand> command)
{
    if (!command) {
        throw std::invalid_argument("edit command must not be null");
    }
    History& history = m_histories[key];
    history.undoStack.push_back(std::move(command));
    history.redoStack.clear();
}

bool UndoHistories::canUndo(const HistoryKey& key) const
{
    const auto history = m_histories.find(key);
    return history != m_histories.end() && !history->second.undoStack.empty();
}

bool UndoHistories::canRedo(const HistoryKey& key) const
{
    const auto history = m_histories.find(key);
    return history != m_histories.end() && !history->second.redoStack.empty();
}

const EditCommand* UndoHistories::undo(const HistoryKey& key,
                                       document::ProjectDocument& document)
{
    const auto history = m_histories.find(key);
    if (history == m_histories.end() || history->second.undoStack.empty()) {
        return nullptr;
    }
    std::unique_ptr<EditCommand> command = std::move(history->second.undoStack.back());
    history->second.undoStack.pop_back();
    command->revert(document);
    history->second.redoStack.push_back(std::move(command));
    return history->second.redoStack.back().get();
}

const EditCommand* UndoHistories::redo(const HistoryKey& key,
                                       document::ProjectDocument& document)
{
    const auto history = m_histories.find(key);
    if (history == m_histories.end() || history->second.redoStack.empty()) {
        return nullptr;
    }
    std::unique_ptr<EditCommand> command = std::move(history->second.redoStack.back());
    history->second.redoStack.pop_back();
    command->apply(document);
    history->second.undoStack.push_back(std::move(command));
    return history->second.undoStack.back().get();
}

void UndoHistories::clear()
{
    m_histories.clear();
}

}  // namespace colorizing::edit
