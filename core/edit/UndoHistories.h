#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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
// The one history of the helper lines, see HelperLinesCommand.
[[nodiscard]] HistoryKey helperLinesHistoryKey();

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
// Groups: the commands pushed between beginGroup() and endGroup() are one step, whichever
// histories they are in (one user action that changes several frames records a command in
// each frame's history). Undoing one of them undoes every other command of its group that is
// the latest of its history, newest first; redoing one redoes the others the same way, in the
// order they were made. A command of the group that is no longer the latest of its history
// (its frame was edited afterwards) is left alone and undone with its history later; nothing
// pushed after the group is undone with it.
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

    // Opens a group for the commands pushed until the matching endGroup(). Nested calls join
    // the outer group.
    void beginGroup();
    void endGroup();
    [[nodiscard]] bool isGroupOpen() const;

    // Called right after each command an undo or redo ran, the key's own first.
    using RunHandler = std::function<void(const EditCommand& command)>;

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
    // With the other commands of its group, see above; ran is called for each, the key's own
    // first. The returned pointer is the key's own command.
    const EditCommand* undo(const HistoryKey& key,
                            document::ProjectDocument& document,
                            const RunHandler& ran = {});
    const EditCommand* redo(const HistoryKey& key,
                            document::ProjectDocument& document,
                            const RunHandler& ran = {});

    // undoLatest reverts the command that was pushed or redone last among the given
    // histories. redoLatest reapplies the one that was undone last among them, so an undo
    // followed by a redo restores the previous state and repeated redos replay the commands
    // in their original order. Both return nullptr when none of the histories has a command.
    const EditCommand* undoLatest(const std::vector<HistoryKey>& keys,
                                  document::ProjectDocument& document,
                                  const RunHandler& ran = {});
    const EditCommand* redoLatest(const std::vector<HistoryKey>& keys,
                                  document::ProjectDocument& document,
                                  const RunHandler& ran = {});
    // Every history that has a command to undo or redo, for undoing anywhere in the project.
    [[nodiscard]] std::vector<HistoryKey> keys() const;
    // The history undoLatest() or redoLatest() would use among the keys, without running it.
    [[nodiscard]] std::optional<HistoryKey> latestUndoKey(const std::vector<HistoryKey>& keys) const;
    [[nodiscard]] std::optional<HistoryKey> latestRedoKey(const std::vector<HistoryKey>& keys) const;

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
        // The group it was pushed in; 0 for none.
        uint64_t group = 0;
    };

    struct History {
        std::vector<Entry> undoStack;
        std::vector<Entry> redoStack;
    };

    void dropReplacedHistories(const EditCommand& command);
    // Moves the latest command of one stack of the key to the other, running it; returns its
    // group.
    uint64_t runOne(const HistoryKey& key,
                    bool undoing,
                    document::ProjectDocument& document,
                    const RunHandler& ran,
                    const EditCommand** command);
    // The key whose latest undo (or redo) command is the newest of the group; none if no
    // history has one of the group on top.
    [[nodiscard]] std::optional<HistoryKey> latestOfGroup(uint64_t group, bool undoing) const;

    std::map<HistoryKey, History> m_histories;
    uint64_t m_nextSequence = 1;
    uint64_t m_nextGroup = 1;
    uint64_t m_openGroup = 0;
    int m_groupDepth = 0;
    std::size_t m_structureMemoryLimit = kDefaultStructureMemoryLimit;
};

}  // namespace colorizing::edit
