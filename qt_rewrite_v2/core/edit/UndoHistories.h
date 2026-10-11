#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include "edit/EditCommand.h"

namespace colorizing::edit {

struct HistoryKey {
    EditKind kind = EditKind::FramePixels;
    std::size_t index = 0;
    render::Resolution resolution = render::Resolution::Sd;

    [[nodiscard]] bool operator<(const HistoryKey& other) const;
    [[nodiscard]] bool operator==(const HistoryKey& other) const;
};

// The history a command belongs to: its kind, primary index and resolution.
[[nodiscard]] HistoryKey historyKey(const EditTarget& target);

// Independent undo/redo stacks, one per key.
class UndoHistories {
public:
    // Records an already applied command and clears that key's redo stack.
    void push(const HistoryKey& key, std::unique_ptr<EditCommand> command);

    [[nodiscard]] bool canUndo(const HistoryKey& key) const;
    [[nodiscard]] bool canRedo(const HistoryKey& key) const;

    // Reverts or reapplies the key's latest command and returns it, or nullptr when there is
    // none. The pointer stays valid until the next push or clear.
    const EditCommand* undo(const HistoryKey& key, document::ProjectDocument& document);
    const EditCommand* redo(const HistoryKey& key, document::ProjectDocument& document);

    void clear();

private:
    struct History {
        std::vector<std::unique_ptr<EditCommand>> undoStack;
        std::vector<std::unique_ptr<EditCommand>> redoStack;
    };

    std::map<HistoryKey, History> m_histories;
};

}  // namespace colorizing::edit
