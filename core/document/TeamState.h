#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "document/ComponentId.h"

namespace colorizing::document {

inline constexpr std::size_t kComponentKindCount = 5;

[[nodiscard]] constexpr std::size_t kindIndex(ComponentKind kind)
{
    return static_cast<std::size_t>(kind);
}

// The revision of each component, by its ID; one map per ComponentKind.
using RevisionMap = std::unordered_map<ComponentId, ComponentId, ComponentIdHash>;
using RevisionMaps = std::array<RevisionMap, kComponentKindCount>;

// A package of changes this copy shared (team::writeChangePackage()): what it carried, and
// which members merged it. Once every member has, its components are common.
struct SentPackage {
    ComponentId id;
    int64_t createdMs = 0;
    // The revision of each component it carried (a null revision: deleted).
    RevisionMaps components;
    std::vector<std::string> mergedBy;

    bool operator==(const SentPackage&) const = default;
};

// What a copy of a team project keeps about the team (design: "Working as a team"). Saved in
// the project file (team/state.json, team/common.bin); a project without it is a solo one.
struct TeamState {
    // The GitHub repository the claims and packages go through, "owner/name".
    std::string repository;
    // GitHub logins; the owner breaks the lock in the repository, the others by a fork.
    std::string ownerLogin;
    std::vector<std::string> members;
    // The snapshot every member started from; a new snapshot starts the team anew.
    ComponentId snapshotId;
    // What every member has, as far as this copy knows: the components and their revisions
    // at the last point all of them had them (the snapshot, then merges).
    RevisionMaps common;
    std::vector<ComponentId> commonOrder;
    // A hash of the project's settings, lights, editor data and sections then.
    uint64_t commonSettingsHash = 0;
    std::vector<SentPackage> sent;
    // The partners' packages merged into this copy.
    std::vector<ComponentId> mergedPackages;

    bool operator==(const TeamState&) const = default;
};

}  // namespace colorizing::document
