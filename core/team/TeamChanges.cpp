#include "team/TeamChanges.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "document/EditorMetadata.h"
#include "project/Json.h"
#include "project/ProjectFile.h"
#include "project/ZipArchive.h"

namespace colorizing::team {

namespace {

using document::ComponentId;
using document::ComponentKind;
using document::ProjectDocument;
using document::RevisionMap;
using document::RevisionMaps;
using project::JsonObjectReader;
using project::JsonValue;

constexpr const char* kChangesEntry = "team/changes.json";
constexpr std::array<const char*, document::kComponentKindCount> kKindNames = {
    "frames", "comparisonMasks", "backgrounds", "sprites", "palettes"};

std::size_t kindOf(ComponentKind kind)
{
    return document::kindIndex(kind);
}

JsonValue revisionsJson(const RevisionMaps& maps)
{
    JsonValue root = JsonValue::object();
    for (std::size_t kind = 0; kind < maps.size(); ++kind) {
        std::vector<std::pair<ComponentId, ComponentId>> sorted(maps[kind].begin(),
                                                                 maps[kind].end());
        std::sort(sorted.begin(), sorted.end());
        JsonValue entries = JsonValue::object();
        for (const auto& [id, revision] : sorted) {
            entries.set(id.toString(), revision.isNull() ? std::string() : revision.toString());
        }
        root.set(kKindNames[kind], std::move(entries));
    }
    return root;
}

ComponentId parseId(const std::string& text, const std::string& what)
{
    const std::optional<ComponentId> id = ComponentId::fromString(text);
    if (!id) {
        throw project::JsonFormatError(what + " has a damaged ID");
    }
    return *id;
}

RevisionMaps readRevisionsValue(const JsonValue* value, const std::string& what)
{
    RevisionMaps maps;
    if (!value || value->isNull()) {
        return maps;
    }
    if (value->type() != JsonValue::Type::Object) {
        throw project::JsonFormatError("the " + what + " is damaged");
    }
    for (std::size_t kind = 0; kind < maps.size(); ++kind) {
        const JsonValue* entries = value->find(kKindNames[kind]);
        if (!entries) {
            continue;
        }
        if (entries->type() != JsonValue::Type::Object) {
            throw project::JsonFormatError("the " + what + " is damaged");
        }
        for (const JsonValue::Member& member : entries->members()) {
            const std::string* revision = member.second.stringValue();
            if (!revision) {
                throw project::JsonFormatError("the " + what + " is damaged");
            }
            maps[kind][parseId(member.first, what)] =
                revision->empty() ? ComponentId{} : parseId(*revision, what);
        }
    }
    return maps;
}

}  // namespace

bool ChangeSet::empty() const
{
    const auto none = [](const auto& map) { return map.empty(); };
    return !orderChanged && !settingsChanged && std::all_of(changed.begin(), changed.end(), none) &&
           std::all_of(deleted.begin(), deleted.end(), none);
}

bool ChangeSet::contains(ComponentKind kind, const ComponentId& id) const
{
    return changed[kindOf(kind)].count(id) > 0 || deleted[kindOf(kind)].count(id) > 0;
}

bool ChangePackage::carries(ComponentKind kind, const ComponentId& id) const
{
    return base[kindOf(kind)].count(id) > 0;
}

RevisionMaps currentRevisions(const ProjectDocument& document)
{
    RevisionMaps maps;
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        maps[0][document.frameMeta(i).id] = document.frameMeta(i).revision;
    }
    for (std::size_t i = 0; i < document.comparisonMaskCount(); ++i) {
        maps[1][document.comparisonMaskMeta(i).id] = document.comparisonMaskMeta(i).revision;
    }
    for (std::size_t i = 0; i < document.backgroundCount(); ++i) {
        maps[2][document.backgroundMeta(i).id] = document.backgroundMeta(i).revision;
    }
    for (std::size_t i = 0; i < document.spriteCount(); ++i) {
        maps[3][document.spriteMeta(i).id] = document.spriteMeta(i).revision;
    }
    for (std::size_t i = 0; i < document.paletteCount(); ++i) {
        maps[4][document.paletteMeta(i).id] = document.paletteMeta(i).revision;
    }
    return maps;
}

uint64_t orderHash(const std::vector<ComponentId>& order)
{
    // FNV-1a over the IDs' bytes.
    uint64_t hash = 0xcbf29ce484222325ULL;
    const auto add = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte, value >>= 8) {
            hash = (hash ^ (value & 0xff)) * 0x100000001b3ULL;
        }
    };
    for (const ComponentId& id : order) {
        add(id.high);
        add(id.low);
    }
    return hash == 0 ? 1 : hash;
}

uint64_t settingsHash(const ProjectDocument& document)
{
    // A snapshot with what the settings hold, and the frames by their IDs only.
    document::ProjectSnapshot settings;
    settings.romName = document.romName();
    settings.license = document.license();
    settings.libserumTarget = document.libserumTarget();
    settings.colorizationSettings = document.colorizationSettings();
    settings.speakerLights = document.speakerLights();
    settings.helperLines = document.helperLines();
    settings.editorMetadata = document.editorMetadata();
    settings.frames.resize(document.frameCount());
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        settings.frames[i].meta = document.frameMeta(i);
    }
    return document::contentHash(project::settingsContent(settings));
}

document::TeamState startTeam(const ProjectDocument& document,
                              std::string repository,
                              std::string ownerLogin,
                              std::vector<std::string> members)
{
    if (document.isDirty()) {
        throw std::logic_error("the project has to be saved before a team starts from it");
    }
    document::TeamState team;
    team.common = currentRevisions(document);
    for (const RevisionMap& map : team.common) {
        for (const auto& [id, revision] : map) {
            if (revision.isNull()) {
                throw std::logic_error(
                    "the project has to be saved before a team starts from it");
            }
        }
    }
    team.repository = std::move(repository);
    team.ownerLogin = std::move(ownerLogin);
    team.members = std::move(members);
    if (std::find(team.members.begin(), team.members.end(), team.ownerLogin) ==
        team.members.end()) {
        team.members.insert(team.members.begin(), team.ownerLogin);
    }
    team.snapshotId = ComponentId::generate();
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        team.commonOrder.push_back(document.frameMeta(i).id);
    }
    team.commonSettingsHash = settingsHash(document);
    return team;
}

ChangeSet changesSinceCommon(const ProjectDocument& document)
{
    if (!document.team()) {
        throw std::logic_error("the project is no team project");
    }
    const document::TeamState& team = *document.team();
    const RevisionMaps current = currentRevisions(document);
    ChangeSet changes;
    for (std::size_t kind = 0; kind < current.size(); ++kind) {
        for (const auto& [id, revision] : current[kind]) {
            const auto common = team.common[kind].find(id);
            if (common == team.common[kind].end() || common->second != revision) {
                changes.changed[kind][id] = revision;
            }
        }
        for (const auto& [id, revision] : team.common[kind]) {
            if (current[kind].count(id) == 0) {
                changes.deleted[kind][id] = revision;
            }
        }
    }
    std::vector<ComponentId> order;
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        order.push_back(document.frameMeta(i).id);
    }
    changes.orderChanged = order != team.commonOrder;
    changes.orderHash = orderHash(order);
    changes.settingsHash = settingsHash(document);
    changes.settingsChanged = changes.settingsHash != team.commonSettingsHash;
    return changes;
}

ChangePackage makeChangePackage(const ProjectDocument& document,
                                const std::string& author,
                                int64_t createdMs)
{
    if (!document.team()) {
        throw std::logic_error("the project is no team project");
    }
    if (document.isDirty()) {
        throw std::logic_error("the project has to be saved before its changes are shared");
    }
    const document::TeamState& team = *document.team();
    const ChangeSet changes = changesSinceCommon(document);

    std::vector<std::size_t> frames;
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        if (changes.changed[0].count(document.frameMeta(i).id)) {
            frames.push_back(i);
        }
    }
    document::SnapshotSubsetMap map;
    ChangePackage package;
    package.snapshot = document.toSubsetSnapshot(frames, &map);
    document::ProjectSnapshot& snapshot = package.snapshot;
    // The changed masks, backgrounds and sprites the changed frames do not use.
    for (std::size_t i = 0; i < document.comparisonMaskCount(); ++i) {
        if (changes.changed[1].count(document.comparisonMaskMeta(i).id) &&
            std::find(map.comparisonMasks.begin(), map.comparisonMasks.end(), i) ==
                map.comparisonMasks.end()) {
            map.comparisonMasks.push_back(i);
            snapshot.comparisonMasks.push_back(document.comparisonMask(i));
            snapshot.comparisonMaskMeta.push_back(document.comparisonMaskMeta(i));
        }
    }
    for (std::size_t i = 0; i < document.backgroundCount(); ++i) {
        if (changes.changed[2].count(document.backgroundMeta(i).id) &&
            std::find(map.backgrounds.begin(), map.backgrounds.end(), i) ==
                map.backgrounds.end()) {
            map.backgrounds.push_back(i);
            snapshot.backgrounds.push_back(document.background(i));
            snapshot.backgrounds.back().inactiveHd.reset();
        }
    }
    for (std::size_t i = 0; i < document.spriteCount(); ++i) {
        if (changes.changed[3].count(document.spriteMeta(i).id) &&
            std::find(map.sprites.begin(), map.sprites.end(), i) == map.sprites.end()) {
            map.sprites.push_back(i);
            snapshot.sprites.push_back(document.sprite(i));
        }
    }
    const document::EditorMetadata& tables = document.editorMetadata();
    document::EditorMetadata& packageTables = snapshot.editorMetadata;
    for (std::size_t mask = 0; mask < map.comparisonMasks.size(); ++mask) {
        document::setFixedName(packageTables.maskNames, mask, document::kMaskNameBytes,
                               document::fixedName(tables.maskNames, map.comparisonMasks[mask],
                                                   document::kMaskNameBytes));
    }
    document::ensureSpriteTableEntries(packageTables, snapshot.sprites.size());
    for (std::size_t sprite = 0; sprite < snapshot.sprites.size(); ++sprite) {
        document::setFixedName(packageTables.spriteNames, sprite, document::kSpriteNameBytes,
                               snapshot.sprites[sprite].name);
        packageTables.spriteSourceRects[sprite * 4] = document::kNoSpriteSourceRect;
    }
    if (!changes.changed[4].empty()) {
        for (std::size_t i = 0; i < document.paletteCount(); ++i) {
            snapshot.palettes.push_back(document.palette(i));
        }
    }
    snapshot.projectId = document.projectId();
    snapshot.license = document.license();
    snapshot.helperLines = document.helperLines();

    for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
        for (const auto& [id, revision] : changes.changed[kind]) {
            const auto common = team.common[kind].find(id);
            package.base[kind][id] =
                common == team.common[kind].end() ? ComponentId{} : common->second;
        }
    }
    package.packageId = ComponentId::generate();
    package.author = author;
    package.createdMs = createdMs;
    package.snapshotId = team.snapshotId;
    package.deleted = changes.deleted;
    for (std::size_t i = 0; i < document.frameCount(); ++i) {
        package.order.push_back(document.frameMeta(i).id);
    }
    package.orderChanged = changes.orderChanged;
    package.settingsChanged = changes.settingsChanged;
    for (std::size_t s = 0; s < tables.sectionCount && s < tables.sectionFirstFrames.size(); ++s) {
        const uint32_t first = tables.sectionFirstFrames[s];
        if (first < document.frameCount()) {
            package.sections.emplace_back(
                document::fixedName(tables.sectionNames, s, document::kSectionNameBytes),
                document.frameMeta(first).id);
        }
    }
    package.merged = team.mergedPackages;
    return package;
}

document::SentPackage sentPackageOf(const ChangePackage& package)
{
    document::SentPackage sent;
    sent.id = package.packageId;
    sent.createdMs = package.createdMs;
    const RevisionMaps current = [&package]() {
        RevisionMaps maps;
        const document::ProjectSnapshot& snapshot = package.snapshot;
        for (const auto& frame : snapshot.frames) {
            maps[0][frame.meta.id] = frame.meta.revision;
        }
        for (const auto& meta : snapshot.comparisonMaskMeta) {
            maps[1][meta.id] = meta.revision;
        }
        for (const auto& background : snapshot.backgrounds) {
            maps[2][background.meta.id] = background.meta.revision;
        }
        for (const auto& sprite : snapshot.sprites) {
            maps[3][sprite.meta.id] = sprite.meta.revision;
        }
        for (const auto& palette : snapshot.palettes) {
            maps[4][palette.meta.id] = palette.meta.revision;
        }
        return maps;
    }();
    for (std::size_t kind = 0; kind < document::kComponentKindCount; ++kind) {
        for (const auto& [id, base] : package.base[kind]) {
            const auto found = current[kind].find(id);
            if (found != current[kind].end()) {
                sent.components[kind][id] = found->second;
            }
        }
        for (const auto& [id, base] : package.deleted[kind]) {
            sent.components[kind][id] = ComponentId{};
        }
    }
    return sent;
}

legacy::LegacySaveResult writeChangePackage(const std::string& path,
                                            const ChangePackage& package,
                                            const std::string& editorVersion)
{
    JsonValue root = JsonValue::object();
    root.set("packageId", package.packageId.toString());
    root.set("author", package.author);
    root.set("created", package.createdMs);
    root.set("snapshotId", package.snapshotId.toString());
    root.set("base", revisionsJson(package.base));
    root.set("deleted", revisionsJson(package.deleted));
    JsonValue order = JsonValue::array();
    for (const ComponentId& id : package.order) {
        order.push(id.toString());
    }
    root.set("order", std::move(order));
    root.set("orderChanged", package.orderChanged);
    root.set("settingsChanged", package.settingsChanged);
    JsonValue sections = JsonValue::array();
    for (const auto& [name, first] : package.sections) {
        JsonValue section = JsonValue::object();
        section.set("name", name);
        section.set("first", first.toString());
        sections.push(std::move(section));
    }
    root.set("sections", std::move(sections));
    JsonValue merged = JsonValue::array();
    for (const ComponentId& id : package.merged) {
        merged.push(id.toString());
    }
    root.set("merged", std::move(merged));
    if (!package.comment.empty()) {
        root.set("comment", package.comment);
    }
    if (!package.authorTimes.empty()) {
        root.set("authorTimes", project::authorTimesJson(package.authorTimes));
    }

    project::ProjectFileOptions options;
    options.editorVersion = editorVersion;
    options.kind = project::FileKind::Changes;
    options.extraEntries.emplace_back(kChangesEntry, project::writeJson(root));
    return project::saveProjectFile(path, package.snapshot, options);
}

ChangePackageLoad readChangePackage(const std::string& path)
{
    ChangePackageLoad result;
    project::KindLoadResult loaded =
        project::loadFileOfKind(path, project::FileKind::Changes, {kChangesEntry});
    if (!loaded.load.ok) {
        result.error = loaded.load.error.message;
        return result;
    }
    if (!loaded.extraEntries[0]) {
        result.error = "the package has no account of its changes";
        return result;
    }
    try {
        std::string parseError;
        const std::optional<JsonValue> value = project::parseJson(*loaded.extraEntries[0],
                                                                  &parseError);
        if (!value) {
            result.error = "the account of the changes is damaged: " + parseError;
            return result;
        }
        const JsonObjectReader root(*value, "account of the changes");
        ChangePackage package;
        package.snapshot = std::move(loaded.load.snapshot);
        package.packageId = parseId(root.string("packageId"), "the package");
        package.author = root.string("author", "");
        package.createdMs = root.integer("created", INT64_MIN, INT64_MAX, 0);
        package.snapshotId = parseId(root.string("snapshotId"), "the package");
        package.base = readRevisionsValue(value->find("base"), "list of changes");
        package.deleted = readRevisionsValue(value->find("deleted"), "list of deletions");
        for (const JsonValue& item : root.array("order")) {
            const std::string* text = item.stringValue();
            if (!text) {
                throw project::JsonFormatError("the frame order of the package is damaged");
            }
            package.order.push_back(parseId(*text, "the frame order"));
        }
        package.orderChanged = root.boolean("orderChanged", false);
        package.settingsChanged = root.boolean("settingsChanged", false);
        for (const JsonValue& item : root.optionalArray("sections")) {
            const JsonObjectReader section = project::objectItem(item, "section");
            package.sections.emplace_back(section.string("name", ""),
                                          parseId(section.string("first"), "a section"));
        }
        for (const JsonValue& item : root.optionalArray("merged")) {
            const std::string* text = item.stringValue();
            if (!text) {
                throw project::JsonFormatError("the list of merged packages is damaged");
            }
            package.merged.push_back(parseId(*text, "the merged packages"));
        }
        package.comment = root.string("comment", "");
        package.authorTimes = project::readAuthorTimes(value->find("authorTimes"));
        result.package = std::move(package);
    } catch (const project::JsonFormatError& error) {
        result.error = error.what();
    }
    return result;
}

std::optional<ChangePackageNote> readChangePackageNote(const std::string& path)
{
    project::ZipReader zip(path);
    const std::optional<std::string> text = zip.read(kChangesEntry);
    if (!text) {
        return std::nullopt;
    }
    try {
        const std::optional<JsonValue> value = project::parseJson(*text, nullptr);
        if (!value) {
            return std::nullopt;
        }
        const JsonObjectReader root(*value, "account of the changes");
        ChangePackageNote note;
        note.author = root.string("author", "");
        note.createdMs = root.integer("created", INT64_MIN, INT64_MAX, 0);
        note.comment = root.string("comment", "");
        return note;
    } catch (const project::JsonFormatError&) {
        return std::nullopt;
    }
}

}  // namespace colorizing::team
