#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "document/ComponentId.h"
#include "team/TeamChanges.h"

namespace colorizing::team {

// The claims of a team (claims.json in the team's repository): which member works on which
// component. The first change to a component claims it, a reservation claims it before; the
// merge that brings the change to the others releases it (it is common then). The others see
// a claimed component read-only, until they have merged the version the claim names: then it is
// common, and a member who goes on changing it makes a new version (blockingClaim()).
struct Claim {
    document::ComponentKind kind = document::ComponentKind::Frame;
    document::ComponentId id;
    std::string login;
    int64_t sinceMs = 0;
    // Claimed before any change ("Reserve").
    bool reserved = false;
    // The claimer's saved revision of it; null for a reservation, an edit not saved yet, a
    // deletion and the claims of editors before 3.99.12.
    document::ComponentId revision;

    bool operator==(const Claim&) const = default;
};

// A claim of something that is no component: the frame order, the settings.
struct WholeClaim {
    std::string login;
    int64_t sinceMs = 0;
    // The claimer's frame order (orderHash()) or settings (settingsHash()); 0 while unsaved or
    // unknown.
    uint64_t hash = 0;

    bool operator==(const WholeClaim&) const = default;
};

// Set when a member broke the lock: the others start again from the snapshot named.
struct LockBreak {
    std::string login;
    int64_t sinceMs = 0;
    document::ComponentId snapshotId;

    bool operator==(const LockBreak&) const = default;
};

struct Claims {
    std::vector<Claim> claims;
    std::optional<WholeClaim> order;
    std::optional<WholeClaim> settings;
    std::optional<LockBreak> lockBreak;

    bool operator==(const Claims&) const = default;
};

[[nodiscard]] std::string writeClaims(const Claims& claims);
// Nothing (with errorMessage) for a damaged file; an empty text is no claims.
[[nodiscard]] std::optional<Claims> parseClaims(std::string_view text,
                                                std::string& errorMessage);

using Reservation = std::pair<document::ComponentKind, document::ComponentId>;

// The claims with this member's made what the copy's changes and reservations are now: one per
// changed, added or deleted component and reservation, the frame order and the settings when
// they changed. A claim that was there keeps its time. The others' claims stay as they are.
[[nodiscard]] Claims withMyClaims(const Claims& current,
                                  const std::string& login,
                                  const ChangeSet& changes,
                                  const std::vector<Reservation>& reserved,
                                  int64_t nowMs);

// Another member's claim of the component that keeps this copy from changing it: one whose
// version this copy has not merged. commonRevision: the component's common revision in this
// copy (TeamState::common), null for none. A claim without a revision always does.
[[nodiscard]] const Claim* blockingClaim(const Claims& claims,
                                         document::ComponentKind kind,
                                         const document::ComponentId& id,
                                         const std::string& myLogin,
                                         const document::ComponentId& commonRevision);
// The same for the frame order or the settings: commonHash is this copy's common one.
[[nodiscard]] bool wholeClaimBlocks(const std::optional<WholeClaim>& claim,
                                    const std::string& myLogin,
                                    uint64_t commonHash);

// Another member's claim of the component; null when it has none.
[[nodiscard]] const Claim* partnerClaim(const Claims& claims,
                                        document::ComponentKind kind,
                                        const document::ComponentId& id,
                                        const std::string& myLogin);

}  // namespace colorizing::team
