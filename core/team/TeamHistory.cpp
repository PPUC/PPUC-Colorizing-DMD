#include "team/TeamHistory.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace colorizing::team {

namespace {

using document::ComponentId;
using document::ComponentKind;

// In the order of the IDs, so that an entry is the same in every copy.
std::vector<ComponentId> sortedIds(std::vector<ComponentId> ids)
{
    std::sort(ids.begin(), ids.end());
    return ids;
}

const document::ComponentMeta& metaOf(const document::ProjectDocument& document,
                                      std::size_t kind,
                                      std::size_t index)
{
    switch (static_cast<ComponentKind>(kind)) {
    case ComponentKind::Frame:
        return document.frameMeta(index);
    case ComponentKind::ComparisonMask:
        return document.comparisonMaskMeta(index);
    case ComponentKind::Background:
        return document.backgroundMeta(index);
    case ComponentKind::Sprite:
        return document.spriteMeta(index);
    case ComponentKind::Palette:
        break;
    }
    return document.paletteMeta(index);
}

std::size_t countOf(const document::ProjectDocument& document, std::size_t kind)
{
    switch (static_cast<ComponentKind>(kind)) {
    case ComponentKind::Frame:
        return document.frameCount();
    case ComponentKind::ComparisonMask:
        return document.comparisonMaskCount();
    case ComponentKind::Background:
        return document.backgroundCount();
    case ComponentKind::Sprite:
        return document.spriteCount();
    case ComponentKind::Palette:
        break;
    }
    return document.paletteCount();
}

}  // namespace

document::TeamHistoryEntry historyEntryOf(const ChangePackage& package)
{
    document::TeamHistoryEntry entry;
    entry.packageId = package.packageId;
    entry.author = package.author;
    entry.createdMs = package.createdMs;
    entry.comment = package.comment;
    entry.orderChanged = package.orderChanged;
    entry.settingsChanged = package.settingsChanged;
    for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
        std::vector<ComponentId> added;
        std::vector<ComponentId> changed;
        for (const auto& [id, common] : package.base[kind]) {
            (common.isNull() ? added : changed).push_back(id);
        }
        std::vector<ComponentId> deleted;
        for (const auto& [id, common] : package.deleted[kind]) {
            deleted.push_back(id);
        }
        entry.added[kind] = sortedIds(std::move(added));
        entry.changed[kind] = sortedIds(std::move(changed));
        entry.deleted[kind] = sortedIds(std::move(deleted));
    }
    return entry;
}

void recordInHistory(document::TeamState& team, const ChangePackage& package)
{
    const bool recorded = std::any_of(
        team.history.begin(), team.history.end(),
        [&package](const document::TeamHistoryEntry& entry) {
            return entry.packageId == package.packageId;
        });
    if (!recorded) {
        team.history.push_back(historyEntryOf(package));
    }
}

std::vector<HistoryChange> describeHistoryEntry(const document::TeamHistoryEntry& entry,
                                                const document::ProjectDocument& document)
{
    std::vector<HistoryChange> changes;
    for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
        if (entry.added[kind].empty() && entry.changed[kind].empty() &&
            entry.deleted[kind].empty()) {
            continue;
        }
        std::unordered_map<ComponentId, std::size_t, document::ComponentIdHash> index;
        const std::size_t count = countOf(document, kind);
        for (std::size_t i = 0; i < count; ++i) {
            index.emplace(metaOf(document, kind, i).id, i);
        }
        const std::array<std::pair<HistoryChange::What, const std::vector<ComponentId>*>, 3>
            lists = {{{HistoryChange::What::Added, &entry.added[kind]},
                      {HistoryChange::What::Changed, &entry.changed[kind]},
                      {HistoryChange::What::Deleted, &entry.deleted[kind]}}};
        for (const auto& [what, ids] : lists) {
            if (ids->empty()) {
                continue;
            }
            HistoryChange change;
            change.kind = static_cast<ComponentKind>(kind);
            change.what = what;
            for (const ComponentId& id : *ids) {
                if (const auto found = index.find(id); found != index.end()) {
                    change.numbers.push_back(found->second);
                } else {
                    ++change.missing;
                }
            }
            std::sort(change.numbers.begin(), change.numbers.end());
            changes.push_back(std::move(change));
        }
    }
    return changes;
}

}  // namespace colorizing::team
