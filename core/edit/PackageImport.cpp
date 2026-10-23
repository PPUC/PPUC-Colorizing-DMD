#include "edit/PackageImport.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "document/EditorMetadata.h"
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::edit {

namespace {

using document::ComponentId;
using document::ComponentIdHash;
using document::IndexedFrame;
using document::ProjectDocument;
using document::ProjectSnapshot;

std::string_view bytesOf(const IndexedFrame& picture)
{
    return {reinterpret_cast<const char*>(picture.pixels.data()), picture.pixels.size()};
}

// What libserum identifies a frame by: the hash of its original under the rule, with the
// rule's mask told by its pixels.
struct IdentificationKey {
    uint64_t mask = 0;
    bool shapeMode = false;
    uint32_t hash = 0;

    bool operator==(const IdentificationKey&) const = default;
};

struct IdentificationKeyHash {
    std::size_t operator()(const IdentificationKey& key) const
    {
        return static_cast<std::size_t>(key.mask ^ (uint64_t{key.hash} << 1) ^
                                        (key.shapeMode ? 0x9e3779b97f4a7c15ULL : 0));
    }
};

IdentificationKey identificationKey(const IndexedFrame& original,
                                    const IndexedFrame* mask,
                                    uint8_t shapeComparisonMode)
{
    // As comparisonRuleOf(): only the stored value 1 is shape mode.
    const bool shapeMode = shapeComparisonMode == 1;
    return {mask ? document::contentHash(bytesOf(*mask)) : 0, shapeMode,
            legacy::legacyFrameHash(original, mask, shapeMode)};
}

bool samePicture(const IndexedFrame& a, const IndexedFrame& b)
{
    return a.size.width == b.size.width && a.size.height == b.size.height &&
           a.pixels == b.pixels;
}

bool samePicture(const document::Rgb565Frame& a, const document::Rgb565Frame& b)
{
    return a.size.width == b.size.width && a.size.height == b.size.height &&
           a.pixels == b.pixels;
}

bool samePicture(const std::optional<document::Rgb565Frame>& a,
                 const std::optional<document::Rgb565Frame>& b)
{
    return a.has_value() == b.has_value() && (!a || samePicture(*a, *b));
}

bool sameBackground(const document::BackgroundRecord& a, const document::BackgroundRecord& b)
{
    return samePicture(a.colorizedSd, b.colorizedSd) && samePicture(a.colorizedHd, b.colorizedHd);
}

bool sameSprite(const document::SpriteRecord& a, const document::SpriteRecord& b)
{
    const auto sameAreas = [&]() {
        return std::equal(a.detectionAreas.begin(), a.detectionAreas.end(),
                          b.detectionAreas.begin(), b.detectionAreas.end(),
                          [](const auto& x, const auto& y) {
                              return x.x == y.x && x.y == y.y && x.width == y.width &&
                                     x.height == y.height;
                          });
    };
    return a.hasHd == b.hasHd && a.shapeMode == b.shapeMode &&
           samePicture(a.originalSd, b.originalSd) && samePicture(a.colorizedSd, b.colorizedSd) &&
           samePicture(a.maskHd, b.maskHd) && samePicture(a.colorizedHd, b.colorizedHd) &&
           a.detectionDwords == b.detectionDwords &&
           a.detectionDwordPositions == b.detectionDwordPositions && sameAreas() &&
           samePicture(a.dynamicMaskSd, b.dynamicMaskSd) &&
           samePicture(a.dynamicMaskHd, b.dynamicMaskHd) &&
           a.dynamicColorsSd == b.dynamicColorsSd && a.dynamicColorsHd == b.dynamicColorsHd;
}

// Equal but for the ID.
bool sameEffect(const document::LightEffect& a, const document::LightEffect& b)
{
    document::LightEffect other = b;
    other.id = a.id;
    return a == other;
}

std::string layoutText(const render::ProjectGeometry& geometry)
{
    const render::Size size = geometry.romFrameSize();
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

// The name with " 2", " 3", ... when the project has it, within a sprite name's bytes.
std::string freeSpriteName(const ProjectDocument& document, const std::string& name)
{
    if (!document.isSpriteNameUsed(name)) {
        return name;
    }
    for (int number = 2;; ++number) {
        const std::string suffix = " " + std::to_string(number);
        std::string base = name.substr(0, document::kSpriteNameBytes - 1 - suffix.size());
        // Not in the middle of a UTF-8 character.
        while (!base.empty() && (static_cast<unsigned char>(base.back()) & 0xc0) == 0x80) {
            base.pop_back();
        }
        if (!base.empty() && (static_cast<unsigned char>(base.back()) & 0xc0) == 0xc0) {
            base.pop_back();
        }
        if (!document.isSpriteNameUsed(base + suffix)) {
            return base + suffix;
        }
    }
}

}  // namespace

std::size_t PackageImportPlan::knownFrameCount() const
{
    return static_cast<std::size_t>(
        std::count_if(knownFrames.begin(), knownFrames.end(),
                      [](const std::optional<std::size_t>& frame) { return frame.has_value(); }));
}

ProjectSnapshot makePackage(const ProjectDocument& document,
                            const std::vector<std::size_t>& frames)
{
    document::SnapshotSubsetMap map;
    ProjectSnapshot package = document.toSubsetSnapshot(frames, &map);
    package.projectId = document.projectId();
    package.license = document.license();
    package.colorizationSettings.reset();
    for (document::ProjectFrameSnapshot& frame : package.frames) {
        frame.scene.reset();
    }
    const document::EditorMetadata& tables = document.editorMetadata();
    document::EditorMetadata& packageTables = package.editorMetadata;
    for (std::size_t mask = 0; mask < map.comparisonMasks.size(); ++mask) {
        document::setFixedName(packageTables.maskNames, mask, document::kMaskNameBytes,
                               document::fixedName(tables.maskNames, map.comparisonMasks[mask],
                                                   document::kMaskNameBytes));
    }
    document::ensureSpriteTableEntries(packageTables, package.sprites.size());
    for (std::size_t sprite = 0; sprite < package.sprites.size(); ++sprite) {
        document::setFixedName(packageTables.spriteNames, sprite, document::kSpriteNameBytes,
                               package.sprites[sprite].name);
        // Where it was taken from is the source project's business.
        packageTables.spriteSourceRects[sprite * 4] = document::kNoSpriteSourceRect;
    }
    // The light effects the frames start, without the project's defaults.
    document::SpeakerLightSettings lights;
    for (const document::ProjectFrameSnapshot& frame : package.frames) {
        for (const document::LightAction& action : frame.lights) {
            if (action.kind != document::LightActionKind::Effect ||
                lights.effect(action.effectId)) {
                continue;
            }
            if (const document::LightEffect* effect =
                    document.speakerLights().effect(action.effectId)) {
                lights.effects.push_back(*effect);
            }
        }
    }
    package.speakerLights = std::move(lights);
    return package;
}

PackageImportPlan planPackageImport(const ProjectDocument& document,
                                    const ProjectSnapshot& package)
{
    PackageImportPlan plan;
    if (package.geometry != document.geometry()) {
        plan.problem = "the package has frames of " + layoutText(package.geometry) +
                       ", the project of " + layoutText(document.geometry());
        return plan;
    }
    if (package.colorCount != document.colorCount()) {
        plan.problem = "the package's frames have " + std::to_string(package.colorCount) +
                       " shades, the project's " + std::to_string(document.colorCount());
        return plan;
    }

    std::unordered_map<ComponentId, std::size_t, ComponentIdHash> frameById;
    std::unordered_map<IdentificationKey, std::size_t, IdentificationKeyHash> frameByKey;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        frameById.emplace(document.frameMeta(frame).id, frame);
        const uint8_t maskId = document.comparisonMaskId(frame);
        const IndexedFrame* mask =
            maskId < document.comparisonMaskCount() ? &document.comparisonMask(maskId) : nullptr;
        frameByKey.emplace(identificationKey(document.originalFrame(frame), mask,
                                             document.shapeComparisonMode(frame)),
                           frame);
    }
    std::set<std::string> authors;
    for (const document::ProjectFrameSnapshot& frame : package.frames) {
        std::optional<std::size_t> known;
        if (const auto found = frameById.find(frame.meta.id); found != frameById.end()) {
            known = found->second;
        } else {
            const IndexedFrame* mask = frame.comparisonMaskId < package.comparisonMasks.size()
                                           ? &package.comparisonMasks[frame.comparisonMaskId]
                                           : nullptr;
            const auto byKey = frameByKey.find(
                identificationKey(frame.originalSd, mask, frame.shapeComparisonMode));
            if (byKey != frameByKey.end()) {
                known = byKey->second;
            }
        }
        plan.knownFrames.push_back(known);
        if (!frame.meta.author.empty()) {
            authors.insert(frame.meta.author);
        }
    }
    plan.authors.assign(authors.begin(), authors.end());

    std::size_t newMasks = 0;
    for (const IndexedFrame& mask : package.comparisonMasks) {
        std::optional<std::size_t> target;
        for (std::size_t index = 0; index < document.comparisonMaskCount() && !target; ++index) {
            if (samePicture(document.comparisonMask(index), mask)) {
                target = index;
            }
        }
        newMasks += target ? 0 : 1;
        plan.maskTargets.push_back(target);
    }
    std::size_t newBackgrounds = 0;
    for (const document::BackgroundRecord& background : package.backgrounds) {
        std::optional<std::size_t> target;
        for (std::size_t index = 0; index < document.backgroundCount() && !target; ++index) {
            if (sameBackground(document.background(index), background)) {
                target = index;
            }
        }
        newBackgrounds += target ? 0 : 1;
        plan.backgroundTargets.push_back(target);
    }
    std::size_t newSprites = 0;
    for (const document::SpriteRecord& sprite : package.sprites) {
        std::optional<std::size_t> target;
        for (std::size_t index = 0; index < document.spriteCount() && !target; ++index) {
            if (sameSprite(document.sprite(index), sprite)) {
                target = index;
            }
        }
        newSprites += target ? 0 : 1;
        plan.spriteTargets.push_back(target);
    }
    const document::LibserumLimits limits = document.limits();
    if (document.comparisonMaskCount() + newMasks > document::kMaxComparisonMasks) {
        plan.problem = "the project would have more than " +
                       std::to_string(document::kMaxComparisonMasks) + " comparison masks";
    } else if (document.backgroundCount() + newBackgrounds > limits.maxBackgrounds) {
        plan.problem = "the project would have more than " +
                       std::to_string(limits.maxBackgrounds) + " backgrounds";
    } else if (document.spriteCount() + newSprites > limits.maxSprites) {
        plan.problem =
            "the project would have more than " + std::to_string(limits.maxSprites) + " sprites";
    }
    return plan;
}

PackageImportCommand::PackageImportCommand(ProjectDocument before,
                                           ProjectDocument after,
                                           std::vector<std::size_t> updatedFrames,
                                           Counts counts)
    : m_before(std::move(before)),
      m_after(std::move(after)),
      m_updatedFrames(std::move(updatedFrames)),
      m_counts(counts)
{
}

void PackageImportCommand::apply(ProjectDocument& document) const
{
    document.restoreState(m_after);
}

void PackageImportCommand::revert(ProjectDocument& document) const
{
    document.restoreState(m_before);
}

EditTarget PackageImportCommand::target() const
{
    EditTarget target{EditKind::ProjectStructure, 0, render::Resolution::Sd, m_updatedFrames};
    for (std::size_t frame = m_before.frameCount(); frame < m_after.frameCount(); ++frame) {
        target.frameIndices.push_back(frame);
    }
    return target;
}

std::vector<ReplacedData> PackageImportCommand::replacedData() const
{
    std::vector<ReplacedData> replaced;
    for (const std::size_t frame : m_updatedFrames) {
        for (const EditKind kind : {EditKind::FramePixels, EditKind::DynamicMask,
                                    EditKind::DynamicColor, EditKind::DynamicShadow,
                                    EditKind::BackgroundMask, EditKind::FrameAttributes}) {
            for (const render::Resolution resolution :
                 {render::Resolution::Sd, render::Resolution::Hd}) {
                replaced.push_back({kind, frame, resolution});
            }
        }
    }
    return replaced;
}

std::vector<RecordReference> PackageImportCommand::recordReferences() const
{
    return {};
}

std::vector<RenumberedRecords> PackageImportCommand::renumberedRecords() const
{
    // The records it adds come and go with it.
    return {{RecordKind::Frame, m_before.frameCount()},
            {RecordKind::ComparisonMask, m_before.comparisonMaskCount()},
            {RecordKind::Background, m_before.backgroundCount()},
            {RecordKind::Sprite, m_before.spriteCount()}};
}

std::unique_ptr<PackageImportCommand> importPackage(ProjectDocument& document,
                                                    const ProjectSnapshot& package,
                                                    KnownFrames knownFrames)
{
    const PackageImportPlan plan = planPackageImport(document, package);
    if (!plan.problem.empty()) {
        throw std::invalid_argument(plan.problem);
    }
    ProjectDocument after = document;
    PackageImportCommand::Counts counts;
    std::vector<std::optional<std::size_t>> masks = plan.maskTargets;
    std::vector<std::optional<std::size_t>> backgrounds = plan.backgroundTargets;
    std::vector<std::optional<std::size_t>> sprites = plan.spriteTargets;

    const auto maskFor = [&](std::size_t index) -> std::size_t {
        if (!masks[index]) {
            document::RemovedComparisonMask added;
            added.index = after.comparisonMaskCount();
            added.mask = package.comparisonMasks[index];
            if (index < package.comparisonMaskMeta.size()) {
                added.meta = package.comparisonMaskMeta[index];
            }
            added.tables = after.recordTables();
            document::setFixedName(added.tables.maskNames, added.index, document::kMaskNameBytes,
                                   document::fixedName(package.editorMetadata.maskNames, index,
                                                       document::kMaskNameBytes));
            masks[index] = added.index;
            after.insertComparisonMask(std::move(added));
            ++counts.addedMasks;
        }
        return *masks[index];
    };
    const auto backgroundFor = [&](std::size_t index) -> std::size_t {
        if (!backgrounds[index]) {
            document::BackgroundRecord background = package.backgrounds[index];
            background.inactiveHd.reset();
            backgrounds[index] = after.appendBackground(std::move(background));
            ++counts.addedBackgrounds;
        }
        return *backgrounds[index];
    };
    const auto spriteFor = [&](std::size_t index) -> std::size_t {
        if (!sprites[index]) {
            document::SpriteRecord sprite = package.sprites[index];
            sprite.name = freeSpriteName(after, sprite.name);
            sprites[index] = after.appendSprite(std::move(sprite), 0);
            ++counts.addedSprites;
        }
        return *sprites[index];
    };

    // The light effects first: the frames name them.
    std::unordered_map<uint16_t, uint16_t> effects;
    {
        document::SpeakerLightSettings lights = after.speakerLights();
        for (const document::LightEffect& effect : package.speakerLights.effects) {
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

    std::vector<document::FrameRecord> added;
    std::vector<std::size_t> updated;
    const std::size_t slots = after.spritesPerFrame();
    for (std::size_t index = 0; index < package.frames.size(); ++index) {
        const std::optional<std::size_t> known = plan.knownFrames[index];
        if (known && knownFrames == KnownFrames::Skip) {
            continue;
        }
        document::ProjectFrameSnapshot frame = package.frames[index];
        frame.scene.reset();
        frame.comparisonMaskId = frame.comparisonMaskId < package.comparisonMasks.size()
                                     ? static_cast<uint8_t>(maskFor(frame.comparisonMaskId))
                                     : document::kNoComparisonMaskId;
        if (frame.backgroundData) {
            uint16_t& id = frame.backgroundData->backgroundId;
            id = id < package.backgrounds.size() ? static_cast<uint16_t>(backgroundFor(id))
                                                 : document::kNoBackgroundId;
        }
        for (document::FrameSpriteReference& slot : frame.spriteReferences) {
            slot.spriteId = slot.spriteId < package.sprites.size()
                                ? static_cast<uint16_t>(spriteFor(slot.spriteId))
                                : document::kNoSpriteId;
        }
        // The project's number of sprite slots.
        if (frame.spriteReferences.size() > slots) {
            if (std::any_of(frame.spriteReferences.begin() + static_cast<std::ptrdiff_t>(slots),
                            frame.spriteReferences.end(), [](const auto& slot) {
                                return slot.spriteId != document::kNoSpriteId;
                            })) {
                throw std::invalid_argument("frame " + std::to_string(index) +
                                            " of the package uses more than " +
                                            std::to_string(slots) +
                                            " sprites, the project allows no more per frame");
            }
        }
        // An empty list stands for empty slots, as in the source.
        if (!frame.spriteReferences.empty()) {
            frame.spriteReferences.resize(slots, document::FrameSpriteReference{});
        }
        for (document::LightAction& action : frame.lights) {
            if (action.kind == document::LightActionKind::Effect) {
                const auto mapped = effects.find(action.effectId);
                if (mapped != effects.end()) {
                    action.effectId = mapped->second;
                } else {
                    action = document::LightAction{};
                }
            }
        }
        document::FrameRecord record = ProjectDocument::frameRecordFrom(std::move(frame));
        if (known && knownFrames == KnownFrames::Update) {
            after.replaceFrame(*known, std::move(record));
            updated.push_back(*known);
        } else {
            added.push_back(std::move(record));
        }
    }
    if (added.empty() && updated.empty()) {
        return nullptr;
    }
    if (!added.empty()) {
        document::RemovedFrames frames;
        for (std::size_t offset = 0; offset < added.size(); ++offset) {
            frames.indices.push_back(after.frameCount() + offset);
        }
        frames.frames = std::move(added);
        frames.tables = after.recordTables();
        counts.addedFrames = frames.indices.size();
        after.insertFrames(std::move(frames));
    }
    std::sort(updated.begin(), updated.end());
    updated.erase(std::unique(updated.begin(), updated.end()), updated.end());
    counts.updatedFrames = updated.size();
    auto command = std::make_unique<PackageImportCommand>(document, std::move(after),
                                                          std::move(updated), counts);
    command->apply(document);
    return command;
}

}  // namespace colorizing::edit
