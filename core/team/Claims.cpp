#include "team/Claims.h"

#include <algorithm>
#include <array>

#include "project/Json.h"

namespace colorizing::team {

namespace {

using document::ComponentId;
using document::ComponentKind;
using project::JsonObjectReader;
using project::JsonValue;

constexpr std::array<const char*, document::kComponentKindCount> kKindNames = {
    "frame", "comparisonMask", "background", "sprite", "palette"};

JsonValue wholeJson(const WholeClaim& claim)
{
    JsonValue value = JsonValue::object();
    value.set("login", claim.login);
    value.set("since", claim.sinceMs);
    return value;
}

WholeClaim readWhole(const JsonObjectReader& object)
{
    return {object.string("login"), object.integer("since", INT64_MIN, INT64_MAX, 0)};
}

ComponentId parseId(const std::string& text)
{
    const std::optional<ComponentId> id = ComponentId::fromString(text);
    if (!id) {
        throw project::JsonFormatError("a claim has a damaged ID");
    }
    return *id;
}

}  // namespace

std::string writeClaims(const Claims& claims)
{
    JsonValue root = JsonValue::object();
    root.set("version", 1);
    JsonValue list = JsonValue::array();
    for (const Claim& claim : claims.claims) {
        JsonValue item = JsonValue::object();
        item.set("kind", kKindNames[document::kindIndex(claim.kind)]);
        item.set("id", claim.id.toString());
        item.set("login", claim.login);
        item.set("since", claim.sinceMs);
        if (claim.reserved) {
            item.set("reserved", true);
        }
        list.push(std::move(item));
    }
    root.set("claims", std::move(list));
    if (claims.order) {
        root.set("order", wholeJson(*claims.order));
    }
    if (claims.settings) {
        root.set("settings", wholeJson(*claims.settings));
    }
    if (claims.lockBreak) {
        JsonValue value = JsonValue::object();
        value.set("login", claims.lockBreak->login);
        value.set("since", claims.lockBreak->sinceMs);
        value.set("snapshotId", claims.lockBreak->snapshotId.toString());
        root.set("lockBreak", std::move(value));
    }
    return project::writeJson(root);
}

std::optional<Claims> parseClaims(std::string_view text, std::string& errorMessage)
{
    if (text.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return Claims{};
    }
    const std::optional<JsonValue> value = project::parseJson(text, &errorMessage);
    if (!value) {
        errorMessage = "the claims are damaged: " + errorMessage;
        return std::nullopt;
    }
    try {
        const JsonObjectReader root(*value, "claims");
        Claims claims;
        for (const JsonValue& item : root.optionalArray("claims")) {
            const JsonObjectReader object = project::objectItem(item, "claim");
            Claim claim;
            const std::string& kind = object.string("kind");
            const auto found = std::find(kKindNames.begin(), kKindNames.end(), kind);
            if (found == kKindNames.end()) {
                // A kind of a later version: not one this copy has.
                continue;
            }
            claim.kind = static_cast<ComponentKind>(found - kKindNames.begin());
            claim.id = parseId(object.string("id"));
            claim.login = object.string("login");
            claim.sinceMs = object.integer("since", INT64_MIN, INT64_MAX, 0);
            claim.reserved = object.boolean("reserved", false);
            claims.claims.push_back(std::move(claim));
        }
        if (root.has("order")) {
            claims.order = readWhole(root.object("order"));
        }
        if (root.has("settings")) {
            claims.settings = readWhole(root.object("settings"));
        }
        if (root.has("lockBreak")) {
            const JsonObjectReader lock = root.object("lockBreak");
            claims.lockBreak = LockBreak{lock.string("login"),
                                         lock.integer("since", INT64_MIN, INT64_MAX, 0),
                                         parseId(lock.string("snapshotId"))};
        }
        return claims;
    } catch (const project::JsonFormatError& error) {
        errorMessage = error.what();
        return std::nullopt;
    }
}

Claims withMyClaims(const Claims& current,
                    const std::string& login,
                    const ChangeSet& changes,
                    const std::vector<Reservation>& reserved,
                    int64_t nowMs)
{
    Claims result = current;
    std::vector<Claim> mine;
    const auto since = [&](ComponentKind kind, const ComponentId& id) {
        for (const Claim& claim : current.claims) {
            if (claim.login == login && claim.kind == kind && claim.id == id) {
                return claim.sinceMs;
            }
        }
        return nowMs;
    };
    for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
        const auto componentKind = static_cast<ComponentKind>(kind);
        std::vector<ComponentId> ids;
        for (const auto& entry : changes.changed[kind]) {
            ids.push_back(entry.first);
        }
        for (const auto& entry : changes.deleted[kind]) {
            ids.push_back(entry.first);
        }
        std::sort(ids.begin(), ids.end());
        for (const ComponentId& id : ids) {
            mine.push_back({componentKind, id, login, since(componentKind, id), false});
        }
    }
    for (const auto& [kind, id] : reserved) {
        const bool changed = std::any_of(mine.begin(), mine.end(), [&](const Claim& claim) {
            return claim.kind == kind && claim.id == id;
        });
        if (!changed) {
            mine.push_back({kind, id, login, since(kind, id), true});
        }
    }
    result.claims.erase(std::remove_if(result.claims.begin(), result.claims.end(),
                                       [&login](const Claim& claim) {
                                           return claim.login == login;
                                       }),
                        result.claims.end());
    result.claims.insert(result.claims.end(), mine.begin(), mine.end());
    const auto whole = [&](const std::optional<WholeClaim>& claim, bool changed) {
        if (claim && claim->login != login) {
            return claim;
        }
        if (!changed) {
            return std::optional<WholeClaim>{};
        }
        return std::optional<WholeClaim>{WholeClaim{login, claim ? claim->sinceMs : nowMs}};
    };
    result.order = whole(current.order, changes.orderChanged);
    result.settings = whole(current.settings, changes.settingsChanged);
    return result;
}

const Claim* partnerClaim(const Claims& claims,
                          ComponentKind kind,
                          const ComponentId& id,
                          const std::string& myLogin)
{
    for (const Claim& claim : claims.claims) {
        if (claim.kind == kind && claim.id == id && claim.login != myLogin) {
            return &claim;
        }
    }
    return nullptr;
}

}  // namespace colorizing::team
