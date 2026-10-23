#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/DocumentStateCommand.h"
#include "team/TeamChanges.h"

namespace colorizing::team {

// What a merge of a partner's change package does with one component.
enum class MergeAction {
    // Only the partner changed it: theirs is taken.
    Take,
    // Both have the same revision already.
    Same,
    // The partner added it.
    Add,
    // Only the partner deleted it: it is deleted.
    Delete,
    // The partner deleted it, and this copy does not have it either.
    AlreadyDeleted,
    // Both changed it: the author keeps one of the two (MergeChoice).
    Conflict,
    // The partner deleted it, this copy changed it.
    DeleteConflict,
    // The partner changed it, this copy deleted it.
    RestoreConflict,
};

struct MergeItem {
    document::ComponentKind kind = document::ComponentKind::Frame;
    document::ComponentId id;
    MergeAction action = MergeAction::Take;
    // The component's place in this copy, and in the package's snapshot.
    std::optional<std::size_t> mine;
    std::optional<std::size_t> theirs;
};

struct MergePlan {
    // Why the package cannot be merged (another snapshot, merged already); empty when it can.
    std::string problem;
    std::vector<MergeItem> items;
    // Whether the partner's settings are taken, or both changed them.
    bool takeSettings = false;
    bool settingsConflict = false;
    // Whether the partner's frame order is taken (this copy did not change its own).
    bool takeOrder = false;

    [[nodiscard]] std::size_t conflictCount() const;
};

[[nodiscard]] MergePlan planMerge(const document::ProjectDocument& document,
                                  const ChangePackage& package);

// Which side a conflict keeps: this copy's version, or the partner's (for a deletion: the
// deletion).
enum class MergeChoice { Mine, Theirs };

struct MergeChoices {
    // By the conflict's component ID; a conflict without a choice keeps mine.
    std::unordered_map<document::ComponentId, MergeChoice, document::ComponentIdHash> components;
    MergeChoice settings = MergeChoice::Mine;
};

// The merge as one step in the structure history.
class TeamMergeCommand final : public edit::DocumentStateCommand {
public:
    struct Counts {
        std::size_t taken = 0;
        std::size_t added = 0;
        std::size_t deleted = 0;
        std::size_t conflicts = 0;
    };

    TeamMergeCommand(document::ProjectDocument before,
                     document::ProjectDocument after,
                     std::vector<std::size_t> replacedFrames,
                     Counts counts);
    [[nodiscard]] const Counts& counts() const { return m_counts; }

private:
    Counts m_counts;
};

// Merges the package into the project as the plan says, with the author's choices for the
// conflicts, and moves the common state on: what both have now is common, the package counts
// as merged, and this copy's own packages that every member has merged become common.
// myLogin is this copy's member. Returns the command, already applied. Throws
// std::invalid_argument with the plan's problem, the document left as it was.
[[nodiscard]] std::unique_ptr<TeamMergeCommand> mergeChangePackage(
    document::ProjectDocument& document,
    const ChangePackage& package,
    const MergeChoices& choices,
    const std::string& myLogin);

}  // namespace colorizing::team
