#include "edit/UndoHistories.h"

#include <algorithm>
#include <iterator>
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

HistoryKey structureHistoryKey()
{
    return HistoryKey{EditKind::ProjectStructure, 0, render::Resolution::Sd};
}

HistoryKey editorColorsHistoryKey()
{
    return HistoryKey{EditKind::EditorColors, 0, render::Resolution::Sd};
}

HistoryKey sectionsHistoryKey()
{
    return HistoryKey{EditKind::Sections, 0, render::Resolution::Sd};
}

HistoryKey helperLinesHistoryKey()
{
    return HistoryKey{EditKind::HelperLines, 0, render::Resolution::Sd};
}

std::size_t UndoHistories::push(const HistoryKey& key, std::unique_ptr<EditCommand> command)
{
    if (!command) {
        throw std::invalid_argument("edit command must not be null");
    }
    dropReplacedHistories(*command);
    History& history = m_histories[key];
    history.undoStack.push_back(Entry{std::move(command), m_nextSequence++, m_openGroup});
    history.redoStack.clear();
    if (!(key == structureHistoryKey())) {
        return 0;
    }
    std::size_t held = structureMemoryBytes();
    std::size_t dropped = 0;
    while (held > m_structureMemoryLimit && history.undoStack.size() > 1) {
        held -= std::min(held, history.undoStack.front().command->heldBytes());
        history.undoStack.erase(history.undoStack.begin());
        ++dropped;
    }
    return dropped;
}

void UndoHistories::beginGroup()
{
    if (m_groupDepth++ == 0) {
        m_openGroup = m_nextGroup++;
    }
}

void UndoHistories::endGroup()
{
    if (m_groupDepth > 0 && --m_groupDepth == 0) {
        m_openGroup = 0;
    }
}

bool UndoHistories::isGroupOpen() const
{
    return m_groupDepth > 0;
}

std::size_t UndoHistories::structureMemoryLimit() const
{
    return m_structureMemoryLimit;
}

void UndoHistories::setStructureMemoryLimit(std::size_t bytes)
{
    m_structureMemoryLimit = bytes;
}

std::size_t UndoHistories::structureMemoryBytes() const
{
    const auto history = m_histories.find(structureHistoryKey());
    if (history == m_histories.end()) {
        return 0;
    }
    std::size_t bytes = 0;
    for (const auto* stack : {&history->second.undoStack, &history->second.redoStack}) {
        for (const Entry& entry : *stack) {
            bytes += entry.command->heldBytes();
        }
    }
    return bytes;
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

bool UndoHistories::canUndoAny(const std::vector<HistoryKey>& keys) const
{
    for (const HistoryKey& key : keys) {
        if (canUndo(key)) {
            return true;
        }
    }
    return false;
}

bool UndoHistories::canRedoAny(const std::vector<HistoryKey>& keys) const
{
    for (const HistoryKey& key : keys) {
        if (canRedo(key)) {
            return true;
        }
    }
    return false;
}

uint64_t UndoHistories::runOne(const HistoryKey& key,
                               bool undoing,
                               document::ProjectDocument& document,
                               const RunHandler& ran,
                               const EditCommand** command)
{
    *command = nullptr;
    const auto history = m_histories.find(key);
    if (history == m_histories.end()) {
        return 0;
    }
    std::vector<Entry>& from = undoing ? history->second.undoStack : history->second.redoStack;
    if (from.empty()) {
        return 0;
    }
    // The command leaves its stack only once it has run: a command that throws stays where it
    // is, so that a failed undo of a deletion cannot make the deletion permanent.
    if (undoing) {
        from.back().command->revert(document);
    } else {
        from.back().command->apply(document);
    }
    Entry entry = std::move(from.back());
    from.pop_back();
    dropReplacedHistories(*entry.command);
    History& own = m_histories[key];
    std::vector<Entry>& to = undoing ? own.redoStack : own.undoStack;
    entry.sequence = m_nextSequence++;
    const uint64_t group = entry.group;
    to.push_back(std::move(entry));
    *command = to.back().command.get();
    if (ran) {
        ran(**command);
    }
    return group;
}

std::optional<HistoryKey> UndoHistories::latestOfGroup(uint64_t group, bool undoing) const
{
    std::optional<HistoryKey> latestKey;
    uint64_t latestSequence = 0;
    for (const auto& [key, history] : m_histories) {
        const std::vector<Entry>& stack = undoing ? history.undoStack : history.redoStack;
        if (!stack.empty() && stack.back().group == group &&
            (!latestKey || stack.back().sequence > latestSequence)) {
            latestKey = key;
            latestSequence = stack.back().sequence;
        }
    }
    return latestKey;
}

const EditCommand* UndoHistories::undo(const HistoryKey& key,
                                       document::ProjectDocument& document,
                                       const RunHandler& ran)
{
    const EditCommand* command = nullptr;
    const uint64_t group = runOne(key, true, document, ran, &command);
    // The rest of the group, newest first. A command of it can drop the history of another
    // (replacedData), so the pointers handed to ran are valid only during the call.
    while (command && group != 0) {
        const std::optional<HistoryKey> next = latestOfGroup(group, true);
        if (!next) {
            break;
        }
        const EditCommand* member = nullptr;
        runOne(*next, true, document, ran, &member);
    }
    return command;
}

const EditCommand* UndoHistories::redo(const HistoryKey& key,
                                       document::ProjectDocument& document,
                                       const RunHandler& ran)
{
    const EditCommand* command = nullptr;
    const uint64_t group = runOne(key, false, document, ran, &command);
    // The rest of the group in the order it was made: undone newest first, the oldest has
    // the highest sequence on the redo stacks.
    while (command && group != 0) {
        const std::optional<HistoryKey> next = latestOfGroup(group, false);
        if (!next) {
            break;
        }
        const EditCommand* member = nullptr;
        runOne(*next, false, document, ran, &member);
    }
    return command;
}

std::optional<HistoryKey> UndoHistories::latestUndoKey(const std::vector<HistoryKey>& keys) const
{
    std::optional<HistoryKey> latestKey;
    uint64_t latestSequence = 0;
    for (const HistoryKey& key : keys) {
        const auto history = m_histories.find(key);
        if (history == m_histories.end() || history->second.undoStack.empty()) {
            continue;
        }
        const uint64_t sequence = history->second.undoStack.back().sequence;
        if (!latestKey || sequence > latestSequence) {
            latestKey = key;
            latestSequence = sequence;
        }
    }
    return latestKey;
}

std::optional<HistoryKey> UndoHistories::latestRedoKey(const std::vector<HistoryKey>& keys) const
{
    std::optional<HistoryKey> latestKey;
    uint64_t latestSequence = 0;
    for (const HistoryKey& key : keys) {
        const auto history = m_histories.find(key);
        if (history == m_histories.end() || history->second.redoStack.empty()) {
            continue;
        }
        const uint64_t sequence = history->second.redoStack.back().sequence;
        if (!latestKey || sequence > latestSequence) {
            latestKey = key;
            latestSequence = sequence;
        }
    }
    return latestKey;
}

std::vector<HistoryKey> UndoHistories::keys() const
{
    std::vector<HistoryKey> result;
    for (const auto& [key, history] : m_histories) {
        if (!history.undoStack.empty() || !history.redoStack.empty()) {
            result.push_back(key);
        }
    }
    return result;
}

const EditCommand* UndoHistories::undoLatest(const std::vector<HistoryKey>& keys,
                                             document::ProjectDocument& document,
                                             const RunHandler& ran)
{
    const std::optional<HistoryKey> key = latestUndoKey(keys);
    return key ? undo(*key, document, ran) : nullptr;
}

const EditCommand* UndoHistories::redoLatest(const std::vector<HistoryKey>& keys,
                                             document::ProjectDocument& document,
                                             const RunHandler& ran)
{
    const std::optional<HistoryKey> key = latestRedoKey(keys);
    return key ? redo(*key, document, ran) : nullptr;
}

void UndoHistories::dropReplacedHistories(const EditCommand& command)
{
    for (const ReplacedData& replaced : command.replacedData()) {
        m_histories.erase(HistoryKey{replaced.kind, replaced.index, replaced.resolution});
    }
    const std::vector<RenumberedRecords> renumbered = command.renumberedRecords();
    if (renumbered.empty()) {
        return;
    }
    const auto isRenumbered = [&renumbered](const Entry& entry) {
        for (const RecordReference& reference : entry.command->recordReferences()) {
            for (const RenumberedRecords& records : renumbered) {
                if (reference.kind == records.kind && reference.index >= records.firstIndex) {
                    return true;
                }
            }
        }
        return false;
    };
    for (auto history = m_histories.begin(); history != m_histories.end();) {
        const History& stacks = history->second;
        const bool stale =
            std::any_of(stacks.undoStack.begin(), stacks.undoStack.end(), isRenumbered) ||
            std::any_of(stacks.redoStack.begin(), stacks.redoStack.end(), isRenumbered);
        history = stale ? m_histories.erase(history) : std::next(history);
    }
}

void UndoHistories::clear()
{
    m_histories.clear();
}

void UndoHistories::clear(const HistoryKey& key)
{
    m_histories.erase(key);
}

void UndoHistories::clear(EditKind kind, std::size_t index)
{
    clear(HistoryKey{kind, index, render::Resolution::Sd});
    clear(HistoryKey{kind, index, render::Resolution::Hd});
}

}  // namespace colorizing::edit
