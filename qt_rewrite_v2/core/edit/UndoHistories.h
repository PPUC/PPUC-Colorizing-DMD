#pragma once

#include <cstddef>
#include <cstdint>
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
// The one history of all structure commands, see StructureCommand.
[[nodiscard]] HistoryKey structureHistoryKey();
// The one history of the edit colors and color sets, see EditorColorsCommand.
[[nodiscard]] HistoryKey editorColorsHistoryKey();
// The one history of the sections, see SectionsCommand.
[[nodiscard]] HistoryKey sectionsHistoryKey();

// Independent undo/redo stacks, one per key. Every command also carries a position in the
// order in which all commands were pushed or redone, so that several histories can be undone
// together chronologically.
//
// A command that replaces data wholesale (EditCommand::replacedData) drops the histories of
// that data whenever it is pushed, undone or redone.
//
// A command that removes, adds or renumbers records (EditCommand::renumberedRecords) drops,
// whenever it is pushed, undone or redone, every history that holds a command naming one of
// those records (EditCommand::recordReferences), on its undo or its redo stack. Dropping
// instead of renumbering the commands: what is left needs no proof beyond "no remaining
// command names a record whose number changed", and that holds by construction. The
// history of the structure commands themselves stays, see StructureCommand.
//
// Memory: the structure history holds the records its deletions took out (frames above all),
// which can add up to the size of the project. Its undo steps are kept within a limit
// (structureMemoryLimit(), EditCommand::heldBytes()): when a push goes beyond it, the oldest
// structure steps are dropped, never the one just pushed. Dropping the bottom of a stack only
// shortens how far back undo reaches; every remaining command still finds the state it was
// made for. The other histories hold changes only and are not limited.
class UndoHistories {
public:
    static constexpr std::size_t kDefaultStructureMemoryLimit = std::size_t{1024} * 1024 * 1024;

    // Records an already applied command and clears that key's redo stack. Returns how many
    // of the oldest structure steps were dropped to stay within the memory limit.
    std::size_t push(const HistoryKey& key, std::unique_ptr<EditCommand> command);

    [[nodiscard]] std::size_t structureMemoryLimit() const;
    // Applies from the next push on.
    void setStructureMemoryLimit(std::size_t bytes);
    // What the structure history's commands hold (EditCommand::heldBytes()), undo and redo.
    [[nodiscard]] std::size_t structureMemoryBytes() const;

    [[nodiscard]] bool canUndo(const HistoryKey& key) const;
    [[nodiscard]] bool canRedo(const HistoryKey& key) const;
    [[nodiscard]] bool canUndoAny(const std::vector<HistoryKey>& keys) const;
    [[nodiscard]] bool canRedoAny(const std::vector<HistoryKey>& keys) const;

    // Reverts or reapplies the key's latest command and returns it, or nullptr when there is
    // none. The pointer stays valid until the next push or clear. When the command throws, the
    // exception is passed on and the histories are left as they were: the command stays on
    // its stack, so a later attempt can run it again. A command that throws has to leave the
    // document as it was (every command here does: it checks before it changes anything).
    const EditCommand* undo(const HistoryKey& key, document::ProjectDocument& document);
    const EditCommand* redo(const HistoryKey& key, document::ProjectDocument& document);

    // undoLatest reverts the command that was pushed or redone last among the given
    // histories. redoLatest reapplies the one that was undone last among them, so an undo
    // followed by a redo restores the previous state and repeated redos replay the commands
    // in their original order. Both return nullptr when none of the histories has a command.
    const EditCommand* undoLatest(const std::vector<HistoryKey>& keys,
                                  document::ProjectDocument& document);
    const EditCommand* redoLatest(const std::vector<HistoryKey>& keys,
                                  document::ProjectDocument& document);

    void clear();
    // Drops one key's stacks, or those of both resolutions of a kind and index, once the data
    // they edit has been replaced. The document is left alone.
    void clear(const HistoryKey& key);
    void clear(EditKind kind, std::size_t index);

private:
    struct Entry {
        std::unique_ptr<EditCommand> command;
        // Position among all pushes and redos while on an undo stack, among all undos while
        // on a redo stack.
        uint64_t sequence = 0;
    };

    struct History {
        std::vector<Entry> undoStack;
        std::vector<Entry> redoStack;
    };

    void dropReplacedHistories(const EditCommand& command);

    std::map<HistoryKey, History> m_histories;
    uint64_t m_nextSequence = 1;
    std::size_t m_structureMemoryLimit = kDefaultStructureMemoryLimit;
};

}  // namespace colorizing::edit
