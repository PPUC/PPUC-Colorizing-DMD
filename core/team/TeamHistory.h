#pragma once

#include <cstddef>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/TeamState.h"
#include "team/TeamChanges.h"

namespace colorizing::team {

// The history of a team's shared changes (design: "Working as a team"): every copy records
// the packages it shares and merges (TeamState::history), so that a partner can read later who
// shared what, with which comment, and what a merge took in.

// The entry of a package: its author, time and comment, and what it added, changed and
// deleted (the components it carries as changes; the others only come along).
[[nodiscard]] document::TeamHistoryEntry historyEntryOf(const ChangePackage& package);

// Records the package in the team state's history, once.
void recordInHistory(document::TeamState& team, const ChangePackage& package);

// What an entry did with the components of one kind, as the history shows it.
struct HistoryChange {
    enum class What { Added, Changed, Deleted };

    document::ComponentKind kind = document::ComponentKind::Frame;
    What what = What::Changed;
    // The components' numbers in the project now (their index), ascending.
    std::vector<std::size_t> numbers;
    // How many of them the project does not have (any more, or not yet).
    std::size_t missing = 0;

    [[nodiscard]] std::size_t count() const { return numbers.size() + missing; }
};

// What the entry changed, by kind (frames first) and added, changed, deleted; a kind without
// changes is left out. The numbers are those of the project as it is now.
[[nodiscard]] std::vector<HistoryChange> describeHistoryEntry(
    const document::TeamHistoryEntry& entry, const document::ProjectDocument& document);

}  // namespace colorizing::team
