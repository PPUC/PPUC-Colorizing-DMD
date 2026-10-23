#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "document/TeamState.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::team {

// A team works on copies of one project (design: "Working as a team"). Each copy knows the
// common state, what every member had at the last point all of them did (TeamState); what a
// member changed since goes to the others as a change package, and their packages are merged
// (TeamMerge.h). Revisions are given by saves only, so a copy is saved before it shares or
// merges.

// The revision of every component of the project, by kind.
[[nodiscard]] document::RevisionMaps currentRevisions(const document::ProjectDocument& document);
// The hash of the project's settings that are no component (project::settingsContent()).
[[nodiscard]] uint64_t settingsHash(const document::ProjectDocument& document);
// A hash of a frame order, never 0.
[[nodiscard]] uint64_t orderHash(const std::vector<document::ComponentId>& order);

// Makes the project a team project: the snapshot every member starts from is the project as
// it is now. Throws std::logic_error when it has unsaved changes or a component without a
// revision (it needs a save first).
[[nodiscard]] document::TeamState startTeam(const document::ProjectDocument& document,
                                            std::string repository,
                                            std::string ownerLogin,
                                            std::vector<std::string> members);

// What this copy changed since the common state.
struct ChangeSet {
    // Changed or added components and their revision now.
    document::RevisionMaps changed;
    // Components deleted since, with their common revision.
    document::RevisionMaps deleted;
    bool orderChanged = false;
    bool settingsChanged = false;
    // The frame order and the settings now (orderHash(), settingsHash()).
    uint64_t orderHash = 0;
    uint64_t settingsHash = 0;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool contains(document::ComponentKind kind,
                                const document::ComponentId& id) const;
};
[[nodiscard]] ChangeSet changesSinceCommon(const document::ProjectDocument& document);

// A member's change package: the changed components with what they use (as a package holds
// them, so that every reference resolves inside it) and the facts of the change.
struct ChangePackage {
    document::ProjectSnapshot snapshot;
    document::ComponentId packageId;
    std::string author;
    int64_t createdMs = 0;
    document::ComponentId snapshotId;
    // The components carried as changes, with their common revision (null: added since).
    // The others of the snapshot only come along because changes use them.
    document::RevisionMaps base;
    // Components the author deleted, with their common revision.
    document::RevisionMaps deleted;
    // The author's frames in their order, and whether the order changed since the common one.
    std::vector<document::ComponentId> order;
    bool orderChanged = false;
    // Whether the settings changed (the snapshot holds them: license, scenes, lights, helper
    // lines) and the author's sections, by their first frame's ID and name.
    bool settingsChanged = false;
    std::vector<std::pair<std::string, document::ComponentId>> sections;
    // The partners' packages the author had merged.
    std::vector<document::ComponentId> merged;
    // The author's description of the changes for the partners; may be empty.
    std::string comment;
    // The time each author spent on the project, as the author's copy knows it (ProjectTime).
    std::map<std::string, uint64_t> authorTimes;

    [[nodiscard]] bool carries(document::ComponentKind kind,
                               const document::ComponentId& id) const;
};

// The package of what changed since the common state; throws std::logic_error for a solo
// project or unsaved changes.
[[nodiscard]] ChangePackage makeChangePackage(const document::ProjectDocument& document,
                                              const std::string& author,
                                              int64_t createdMs);
// What the copy keeps about a package it shares (TeamState::sent).
[[nodiscard]] document::SentPackage sentPackageOf(const ChangePackage& package);

[[nodiscard]] legacy::LegacySaveResult writeChangePackage(const std::string& path,
                                                          const ChangePackage& package,
                                                          const std::string& editorVersion);
struct ChangePackageLoad {
    std::optional<ChangePackage> package;
    std::string error;
};
[[nodiscard]] ChangePackageLoad readChangePackage(const std::string& path);

// Who made a package, when, and their comment, read without the components (for a list of
// the packages waiting); nothing for a file that is no package.
struct ChangePackageNote {
    std::string author;
    int64_t createdMs = 0;
    std::string comment;
};
[[nodiscard]] std::optional<ChangePackageNote> readChangePackageNote(const std::string& path);

}  // namespace colorizing::team
