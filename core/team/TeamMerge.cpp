#include "team/TeamMerge.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "document/EditorMetadata.h"
#include "project/ProjectFile.h"

namespace colorizing::team {

namespace {

using document::ComponentId;
using document::ComponentIdHash;
using document::ComponentKind;
using document::ComponentMeta;
using document::ProjectDocument;
using document::ProjectSnapshot;
using document::RevisionMaps;

using IdIndex = std::unordered_map<ComponentId, std::size_t, ComponentIdHash>;
using IdIndexes = std::array<IdIndex, document::kComponentKindCount>;

IdIndexes indexesOf(const ProjectDocument& document)
{
    IdIndexes indexes;
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        indexes[0][document.frameMeta(i).id] = i;
    }
    for (std::size_t i = 0; i < document.comparisonMaskCount(); ++i) {
        indexes[1][document.comparisonMaskMeta(i).id] = i;
    }
    for (std::size_t i = 0; i < document.backgroundCount(); ++i) {
        indexes[2][document.backgroundMeta(i).id] = i;
    }
    for (std::size_t i = 0; i < document.spriteCount(); ++i) {
        indexes[3][document.spriteMeta(i).id] = i;
    }
    for (std::size_t i = 0; i < document.paletteCount(); ++i) {
        indexes[4][document.paletteMeta(i).id] = i;
    }
    return indexes;
}

std::vector<ComponentMeta> metasOf(const ProjectSnapshot& snapshot, ComponentKind kind)
{
    std::vector<ComponentMeta> metas;
    switch (kind) {
    case ComponentKind::Frame:
        for (const auto& frame : snapshot.frames) {
            metas.push_back(frame.meta);
        }
        break;
    case ComponentKind::ComparisonMask:
        metas = snapshot.comparisonMaskMeta;
        break;
    case ComponentKind::Background:
        for (const auto& background : snapshot.backgrounds) {
            metas.push_back(background.meta);
        }
        break;
    case ComponentKind::Sprite:
        for (const auto& sprite : snapshot.sprites) {
            metas.push_back(sprite.meta);
        }
        break;
    case ComponentKind::Palette:
        for (const auto& palette : snapshot.palettes) {
            metas.push_back(palette.meta);
        }
        break;
    }
    return metas;
}

IdIndexes indexesOf(const ProjectSnapshot& snapshot)
{
    IdIndexes indexes;
    for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
        const std::vector<ComponentMeta> metas =
            metasOf(snapshot, static_cast<ComponentKind>(kind));
        for (std::size_t i = 0; i < metas.size(); ++i) {
            indexes[kind][metas[i].id] = i;
        }
    }
    return indexes;
}

ComponentMeta metaOf(const ProjectDocument& document, ComponentKind kind, std::size_t index)
{
    switch (kind) {
    case ComponentKind::Frame:
        return document.frameMeta(index);
    case ComponentKind::ComparisonMask:
        return document.comparisonMaskMeta(index);
    case ComponentKind::Background:
        return document.backgroundMeta(index);
    case ComponentKind::Sprite:
        return document.spriteMeta(index);
    case ComponentKind::Palette:
        return document.paletteMeta(index);
    }
    return {};
}

// What the partner's copy has as its settings hash (settingsHash() there).
uint64_t theirSettingsHash(const ChangePackage& package)
{
    ProjectSnapshot settings;
    const ProjectSnapshot& snapshot = package.snapshot;
    settings.romName = snapshot.romName;
    settings.license = snapshot.license;
    settings.colorizationSettings = snapshot.colorizationSettings;
    settings.speakerLights = snapshot.speakerLights;
    settings.helperLines = snapshot.helperLines;
    settings.frames.resize(package.order.size());
    IdIndex position;
    for (std::size_t i = 0; i < package.order.size(); ++i) {
        settings.frames[i].meta.id = package.order[i];
        position[package.order[i]] = i;
    }
    document::EditorMetadata& tables = settings.editorMetadata;
    tables.sectionCount = static_cast<uint32_t>(package.sections.size());
    for (std::size_t s = 0; s < package.sections.size() && s < document::kMaxSections; ++s) {
        document::setFixedName(tables.sectionNames, s, document::kSectionNameBytes,
                               package.sections[s].first);
        const auto found = position.find(package.sections[s].second);
        tables.sectionFirstFrames[s] =
            found != position.end() ? static_cast<uint32_t>(found->second) : 0;
    }
    return document::contentHash(project::settingsContent(settings));
}

bool sameEffect(const document::LightEffect& a, const document::LightEffect& b)
{
    document::LightEffect other = b;
    other.id = a.id;
    return a == other;
}

}  // namespace

std::size_t MergePlan::conflictCount() const
{
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const auto& item) {
               return item.action == MergeAction::Conflict ||
                      item.action == MergeAction::DeleteConflict ||
                      item.action == MergeAction::RestoreConflict;
           })) +
           (settingsConflict ? 1 : 0);
}

MergePlan planMerge(const ProjectDocument& document, const ChangePackage& package)
{
    MergePlan plan;
    if (!document.team()) {
        plan.problem = "the project is no team project";
        return plan;
    }
    const document::TeamState& team = *document.team();
    if (package.snapshotId != team.snapshotId) {
        plan.problem = "the package belongs to another snapshot of the project: start again "
                       "from the newest snapshot";
        return plan;
    }
    if (std::find(team.mergedPackages.begin(), team.mergedPackages.end(), package.packageId) !=
        team.mergedPackages.end()) {
        plan.problem = "the package is merged already";
        return plan;
    }
    const IdIndexes mine = indexesOf(document);
    const IdIndexes theirs = indexesOf(package.snapshot);
    for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
        const auto componentKind = static_cast<ComponentKind>(kind);
        const std::vector<ComponentMeta> theirMetas = metasOf(package.snapshot, componentKind);
        // In the order of the package's snapshot, so that frames are added in their order.
        std::vector<std::pair<ComponentId, ComponentId>> carried(package.base[kind].begin(),
                                                                 package.base[kind].end());
        std::sort(carried.begin(), carried.end(), [&](const auto& a, const auto& b) {
            const auto ia = theirs[kind].find(a.first);
            const auto ib = theirs[kind].find(b.first);
            const std::size_t pa = ia != theirs[kind].end() ? ia->second : SIZE_MAX;
            const std::size_t pb = ib != theirs[kind].end() ? ib->second : SIZE_MAX;
            return pa != pb ? pa < pb : a.first < b.first;
        });
        for (const auto& [id, base] : carried) {
            const auto their = theirs[kind].find(id);
            if (their == theirs[kind].end()) {
                continue;
            }
            MergeItem item{componentKind, id, MergeAction::Take, std::nullopt, their->second};
            const ComponentId theirRevision = theirMetas[their->second].revision;
            const auto common = team.common[kind].find(id);
            if (const auto own = mine[kind].find(id); own != mine[kind].end()) {
                item.mine = own->second;
                const ComponentId myRevision =
                    metaOf(document, componentKind, own->second).revision;
                const bool mineChanged =
                    common == team.common[kind].end() || common->second != myRevision;
                if (myRevision == theirRevision) {
                    item.action = MergeAction::Same;
                } else if (!mineChanged || (!base.isNull() && myRevision == base)) {
                    item.action = MergeAction::Take;
                } else {
                    item.action = MergeAction::Conflict;
                }
            } else {
                item.action = common != team.common[kind].end() ? MergeAction::RestoreConflict
                                                                : MergeAction::Add;
            }
            plan.items.push_back(item);
        }
        for (const auto& [id, base] : package.deleted[kind]) {
            MergeItem item{componentKind, id, MergeAction::Delete, std::nullopt, std::nullopt};
            if (const auto own = mine[kind].find(id); own != mine[kind].end()) {
                item.mine = own->second;
                const ComponentId myRevision =
                    metaOf(document, componentKind, own->second).revision;
                const auto common = team.common[kind].find(id);
                const bool mineChanged =
                    common == team.common[kind].end() || common->second != myRevision;
                item.action = !mineChanged || myRevision == base ? MergeAction::Delete
                                                                 : MergeAction::DeleteConflict;
            } else {
                item.action = MergeAction::AlreadyDeleted;
            }
            plan.items.push_back(item);
        }
    }
    if (package.settingsChanged) {
        const bool mineChanged = settingsHash(document) != team.commonSettingsHash;
        plan.takeSettings = !mineChanged;
        plan.settingsConflict = mineChanged;
    }
    std::vector<ComponentId> order;
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        order.push_back(document.frameMeta(i).id);
    }
    plan.takeOrder = package.orderChanged && order == team.commonOrder;
    return plan;
}

TeamMergeCommand::TeamMergeCommand(ProjectDocument before,
                                   ProjectDocument after,
                                   std::vector<std::size_t> replacedFrames,
                                   Counts counts)
    : DocumentStateCommand(std::move(before), std::move(after), std::move(replacedFrames)),
      m_counts(counts)
{
}

std::unique_ptr<TeamMergeCommand> mergeChangePackage(ProjectDocument& document,
                                                     const ChangePackage& package,
                                                     const MergeChoices& choices,
                                                     const std::string& myLogin)
{
    const MergePlan plan = planMerge(document, package);
    if (!plan.problem.empty()) {
        throw std::invalid_argument(plan.problem);
    }
    const ProjectSnapshot& theirs = package.snapshot;
    ProjectDocument after = document;
    TeamMergeCommand::Counts counts;
    std::vector<document::StampedMeta> theirMetas;
    document::TeamState team = *document.team();

    const auto choice = [&choices](const MergeItem& item) {
        const auto found = choices.components.find(item.id);
        return found != choices.components.end() ? found->second : MergeChoice::Mine;
    };
    // Whether the item ends with the partner's version (or deletion).
    const auto takesTheirs = [&](const MergeItem& item) {
        switch (item.action) {
        case MergeAction::Take:
        case MergeAction::Add:
        case MergeAction::Delete:
            return true;
        case MergeAction::Conflict:
        case MergeAction::DeleteConflict:
        case MergeAction::RestoreConflict:
            return choice(item) == MergeChoice::Theirs;
        case MergeAction::Same:
        case MergeAction::AlreadyDeleted:
            break;
        }
        return false;
    };
    const auto theirMeta = [&](ComponentKind kind, std::size_t index) {
        return metasOf(theirs, kind)[index];
    };

    // The package's masks, backgrounds and sprites in this copy, added where it lacks them.
    std::vector<std::optional<std::size_t>> masks(theirs.comparisonMasks.size());
    std::vector<std::optional<std::size_t>> backgrounds(theirs.backgrounds.size());
    std::vector<std::optional<std::size_t>> sprites(theirs.sprites.size());
    {
        const IdIndexes mine = indexesOf(after);
        for (std::size_t i = 0; i < masks.size(); ++i) {
            if (const auto found = mine[1].find(theirs.comparisonMaskMeta[i].id);
                found != mine[1].end()) {
                masks[i] = found->second;
            }
        }
        for (std::size_t i = 0; i < backgrounds.size(); ++i) {
            if (const auto found = mine[2].find(theirs.backgrounds[i].meta.id);
                found != mine[2].end()) {
                backgrounds[i] = found->second;
            }
        }
        for (std::size_t i = 0; i < sprites.size(); ++i) {
            if (const auto found = mine[3].find(theirs.sprites[i].meta.id);
                found != mine[3].end()) {
                sprites[i] = found->second;
            }
        }
    }
    const auto maskFor = [&](std::size_t index) -> std::size_t {
        if (!masks[index]) {
            document::RemovedComparisonMask added;
            added.index = after.comparisonMaskCount();
            added.mask = theirs.comparisonMasks[index];
            added.meta = theirs.comparisonMaskMeta[index];
            added.tables = after.recordTables();
            document::setFixedName(added.tables.maskNames, added.index, document::kMaskNameBytes,
                                   document::fixedName(theirs.editorMetadata.maskNames, index,
                                                       document::kMaskNameBytes));
            masks[index] = added.index;
            after.insertComparisonMask(std::move(added));
        }
        return *masks[index];
    };
    const auto backgroundFor = [&](std::size_t index) -> std::size_t {
        if (!backgrounds[index]) {
            document::BackgroundRecord background = theirs.backgrounds[index];
            background.inactiveHd.reset();
            backgrounds[index] = after.appendBackground(std::move(background));
        }
        return *backgrounds[index];
    };
    const auto spriteFor = [&](std::size_t index) -> std::size_t {
        if (!sprites[index]) {
            sprites[index] = after.appendSprite(theirs.sprites[index], 0);
        }
        return *sprites[index];
    };

    // Light effects the partner's frames start, by their content.
    std::unordered_map<uint16_t, uint16_t> effects;
    {
        document::SpeakerLightSettings lights = after.speakerLights();
        for (const document::LightEffect& effect : theirs.speakerLights.effects) {
            const auto equal = std::find_if(
                lights.effects.begin(), lights.effects.end(),
                [&effect](const document::LightEffect& own) { return sameEffect(own, effect); });
            if (equal != lights.effects.end()) {
                effects[effect.id] = equal->id;
                continue;
            }
            document::LightEffect added = effect;
            added.id = lights.effect(effect.id) ? lights.nextEffectId() : effect.id;
            effects[effect.id] = added.id;
            lights.effects.push_back(std::move(added));
        }
        after.setSpeakerLights(std::move(lights));
    }
    const auto mapLights = [&effects](document::LightActions& actions) {
        for (document::LightAction& action : actions) {
            if (action.kind == document::LightActionKind::Effect) {
                const auto mapped = effects.find(action.effectId);
                action = mapped != effects.end()
                             ? document::LightAction{action.kind, mapped->second, action.color,
                                                     action.automatic}
                             : document::LightAction{};
            }
        }
    };

    // Masks, backgrounds, sprites and palettes the partner's version replaces.
    for (const MergeItem& item : plan.items) {
        if (!takesTheirs(item) || !item.theirs) {
            continue;
        }
        const std::size_t index = *item.theirs;
        switch (item.kind) {
        case ComponentKind::ComparisonMask:
            if (item.mine) {
                after.replaceComparisonMask(*item.mine, theirs.comparisonMasks[index]);
            } else {
                (void)maskFor(index);
            }
            break;
        case ComponentKind::Background:
            if (item.mine) {
                document::BackgroundRecord background = theirs.backgrounds[index];
                background.inactiveHd.reset();
                after.replaceBackground(*item.mine, std::move(background));
            } else {
                (void)backgroundFor(index);
            }
            break;
        case ComponentKind::Sprite:
            if (item.mine) {
                after.restoreSprite(*item.mine, theirs.sprites[index]);
            } else {
                (void)spriteFor(index);
            }
            break;
        case ComponentKind::Palette:
            if (item.mine) {
                const document::PaletteRecord& palette = theirs.palettes[index];
                for (std::size_t color = 0; color < palette.colors.size(); ++color) {
                    after.setPaletteColor(*item.mine, color, palette.colors[color]);
                }
                after.setPaletteName(*item.mine, palette.name);
            }
            break;
        case ComponentKind::Frame:
            continue;
        }
        theirMetas.push_back({item.kind, theirMeta(item.kind, index)});
        ++counts.taken;
    }

    // The frames.
    std::vector<std::size_t> replaced;
    std::vector<document::FrameRecord> added;
    std::vector<ComponentId> addedIds;
    std::unordered_set<ComponentId, ComponentIdHash> deletedFrames;
    for (const MergeItem& item : plan.items) {
        if (item.kind != ComponentKind::Frame || !takesTheirs(item)) {
            continue;
        }
        if (item.action == MergeAction::Delete || item.action == MergeAction::DeleteConflict) {
            deletedFrames.insert(item.id);
            continue;
        }
        document::ProjectFrameSnapshot frame = theirs.frames[*item.theirs];
        frame.comparisonMaskId = frame.comparisonMaskId < theirs.comparisonMasks.size()
                                     ? static_cast<uint8_t>(maskFor(frame.comparisonMaskId))
                                     : document::kNoComparisonMaskId;
        if (frame.backgroundData) {
            uint16_t& id = frame.backgroundData->backgroundId;
            id = id < theirs.backgrounds.size() ? static_cast<uint16_t>(backgroundFor(id))
                                                : document::kNoBackgroundId;
        }
        for (document::FrameSpriteReference& slot : frame.spriteReferences) {
            slot.spriteId = slot.spriteId < theirs.sprites.size()
                                ? static_cast<uint16_t>(spriteFor(slot.spriteId))
                                : document::kNoSpriteId;
        }
        if (!frame.spriteReferences.empty()) {
            frame.spriteReferences.resize(document::kSpritesPerFrame,
                                          document::FrameSpriteReference{});
        }
        mapLights(frame.lights);
        theirMetas.push_back({ComponentKind::Frame, frame.meta});
        document::FrameRecord record = ProjectDocument::frameRecordFrom(std::move(frame));
        if (item.mine) {
            after.replaceFrame(*item.mine, std::move(record));
            replaced.push_back(*item.mine);
            ++counts.taken;
        } else {
            added.push_back(std::move(record));
            addedIds.push_back(item.id);
            ++counts.added;
        }
    }
    if (!added.empty()) {
        document::RemovedFrames frames;
        for (std::size_t offset = 0; offset < added.size(); ++offset) {
            frames.indices.push_back(after.frameCount() + offset);
        }
        frames.frames = std::move(added);
        frames.tables = after.recordTables();
        after.insertFrames(std::move(frames));
    }
    // The partner's metas, before the frames are deleted and moved: by ID.
    after.adoptComponentMeta(theirMetas);
    if (!deletedFrames.empty()) {
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < after.frameCount(); ++i) {
            if (deletedFrames.count(after.frameMeta(i).id)) {
                indices.push_back(i);
            }
        }
        if (indices.size() < after.frameCount()) {
            (void)after.removeFrames(indices);
            counts.deleted += indices.size();
        }
    }

    // The frame order: the partner's when this copy kept the common one, else this copy's
    // with the added frames behind the frame they follow in the partner's order.
    {
        IdIndex current;
        for (std::size_t i = 0; i < after.frameCount(); ++i) {
            current[after.frameMeta(i).id] = i;
        }
        std::vector<ComponentId> order;
        if (plan.takeOrder) {
            for (const ComponentId& id : package.order) {
                if (current.count(id)) {
                    order.push_back(id);
                }
            }
            for (std::size_t i = 0; i < after.frameCount(); ++i) {
                if (std::find(package.order.begin(), package.order.end(),
                              after.frameMeta(i).id) == package.order.end()) {
                    order.push_back(after.frameMeta(i).id);
                }
            }
        } else {
            const std::unordered_set<ComponentId, ComponentIdHash> newFrames(addedIds.begin(),
                                                                             addedIds.end());
            for (std::size_t i = 0; i < after.frameCount(); ++i) {
                if (!newFrames.count(after.frameMeta(i).id)) {
                    order.push_back(after.frameMeta(i).id);
                }
            }
            for (const ComponentId& id : addedIds) {
                const auto at = std::find(package.order.begin(), package.order.end(), id);
                auto place = order.end();
                for (auto before = at; before != package.order.begin();) {
                    --before;
                    const auto found = std::find(order.begin(), order.end(), *before);
                    if (found != order.end()) {
                        place = found + 1;
                        break;
                    }
                }
                if (at == package.order.begin()) {
                    place = order.begin();
                }
                order.insert(place, id);
            }
        }
        std::vector<std::size_t> newOrder;
        newOrder.reserve(order.size());
        for (const ComponentId& id : order) {
            newOrder.push_back(current.at(id));
        }
        bool moves = false;
        for (std::size_t i = 0; i < newOrder.size(); ++i) {
            moves = moves || newOrder[i] != i;
        }
        if (moves) {
            try {
                after.reorderFrames(newOrder);
            } catch (const std::logic_error&) {
                // Frames with an imported palette stay the first ones; the order stays.
            }
        }
    }

    // Masks, backgrounds and sprites the partner deleted, from the last one on.
    for (const ComponentKind kind :
         {ComponentKind::ComparisonMask, ComponentKind::Background, ComponentKind::Sprite}) {
        std::vector<std::size_t> indices;
        const IdIndexes mine = indexesOf(after);
        for (const MergeItem& item : plan.items) {
            if (item.kind == kind && takesTheirs(item) &&
                (item.action == MergeAction::Delete ||
                 item.action == MergeAction::DeleteConflict)) {
                if (const auto found = mine[document::kindIndex(kind)].find(item.id);
                    found != mine[document::kindIndex(kind)].end()) {
                    indices.push_back(found->second);
                }
            }
        }
        std::sort(indices.rbegin(), indices.rend());
        for (const std::size_t index : indices) {
            if (kind == ComponentKind::ComparisonMask) {
                (void)after.removeComparisonMask(index);
            } else if (kind == ComponentKind::Background) {
                (void)after.removeBackground(index);
            } else {
                (void)after.removeSprite(index);
            }
            ++counts.deleted;
        }
    }

    // The settings.
    if (plan.takeSettings || (plan.settingsConflict && choices.settings == MergeChoice::Theirs)) {
        after.setLicense(theirs.license);
        after.setColorizationSettings(theirs.colorizationSettings);
        after.setHelperLines(theirs.helperLines);
        try {
            document::SpeakerLightSettings lights = after.speakerLights();
            lights.defaults = theirs.speakerLights.defaults;
            mapLights(lights.defaults);
            after.setSpeakerLights(std::move(lights));
        } catch (const std::invalid_argument&) {
        }
        document::RecordTables tables = after.recordTables();
        IdIndex position;
        for (std::size_t i = 0; i < after.frameCount(); ++i) {
            position[after.frameMeta(i).id] = i;
        }
        uint32_t count = 0;
        for (const auto& [name, first] : package.sections) {
            const auto found = position.find(first);
            if (found == position.end() || count >= document::kMaxSections) {
                continue;
            }
            document::setFixedName(tables.sectionNames, count, document::kSectionNameBytes, name);
            tables.sectionFirstFrames[count] = static_cast<uint32_t>(found->second);
            ++count;
        }
        for (uint32_t s = count; s < tables.sectionCount; ++s) {
            document::setFixedName(tables.sectionNames, s, document::kSectionNameBytes, "");
            tables.sectionFirstFrames[s] = 0;
        }
        tables.sectionCount = count;
        after.restoreRecordTables(std::move(tables));
    }

    // The common state moves on.
    for (const MergeItem& item : plan.items) {
        document::RevisionMap& common = team.common[document::kindIndex(item.kind)];
        switch (item.action) {
        case MergeAction::Take:
        case MergeAction::Same:
        case MergeAction::Add:
        case MergeAction::Conflict:
        case MergeAction::RestoreConflict:
            common[item.id] = theirMeta(item.kind, *item.theirs).revision;
            break;
        case MergeAction::Delete:
        case MergeAction::AlreadyDeleted:
        case MergeAction::DeleteConflict:
            common.erase(item.id);
            break;
        }
        if (item.action == MergeAction::Conflict || item.action == MergeAction::DeleteConflict ||
            item.action == MergeAction::RestoreConflict) {
            ++counts.conflicts;
        }
    }
    if (plan.takeOrder) {
        team.commonOrder = package.order;
    }
    if (package.settingsChanged) {
        team.commonSettingsHash = theirSettingsHash(package);
    }
    team.mergedPackages.push_back(package.packageId);
    for (auto sent = team.sent.begin(); sent != team.sent.end();) {
        if (std::find(package.merged.begin(), package.merged.end(), sent->id) !=
                package.merged.end() &&
            std::find(sent->mergedBy.begin(), sent->mergedBy.end(), package.author) ==
                sent->mergedBy.end()) {
            sent->mergedBy.push_back(package.author);
        }
        const bool everyone = std::all_of(
            team.members.begin(), team.members.end(), [&](const std::string& member) {
                return member == myLogin ||
                       std::find(sent->mergedBy.begin(), sent->mergedBy.end(), member) !=
                           sent->mergedBy.end();
            });
        if (!everyone) {
            ++sent;
            continue;
        }
        for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
            for (const auto& [id, revision] : sent->components[kind]) {
                if (revision.isNull()) {
                    team.common[kind].erase(id);
                } else {
                    team.common[kind][id] = revision;
                }
            }
        }
        sent = team.sent.erase(sent);
    }
    after.setTeam(std::move(team));

    std::sort(replaced.begin(), replaced.end());
    auto command = std::make_unique<TeamMergeCommand>(document, std::move(after),
                                                      std::move(replaced), counts);
    command->apply(document);
    return command;
}

}  // namespace colorizing::team
