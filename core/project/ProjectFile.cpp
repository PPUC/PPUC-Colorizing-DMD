#include "project/ProjectFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "document/EditorMetadata.h"
#include "document/Triggers.h"
#include "platform/Utf8Path.h"
#include "project/Json.h"
#include "project/ZipArchive.h"

namespace colorizing::project {

namespace {

using document::ComponentId;
using document::ComponentIdHash;
using document::ComponentMeta;
using document::IndexedFrame;
using document::ProjectSnapshot;
using document::Rgb565Frame;
using Tag = std::array<char, 4>;

constexpr std::size_t kSpriteSlotBytes = 16 + 2 + 4 * 2;
constexpr std::size_t kLightActionBytes = 8;

// What a damaged or incomplete file throws inside the reader; the message is the user's.
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Thrown inside the reader when LegacyIoProgress::cancel is set.
struct Cancelled {};

// Writing --------------------------------------------------------------------------------

void appendLittleEndian(std::string& out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

void appendId(std::string& out, const ComponentId& id)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((id.high >> shift) & 0xff));
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((id.low >> shift) & 0xff));
    }
}

void appendField(std::string& out, const char (&tag)[5], const std::string& data)
{
    out.append(tag, 4);
    appendLittleEndian(out, 0, 4);
    appendLittleEndian(out, data.size(), 8);
    out += data;
}

std::string integerData(uint64_t value, std::size_t bytes)
{
    std::string data;
    appendLittleEndian(data, value, bytes);
    return data;
}

std::string idData(const ComponentId& id)
{
    std::string data;
    appendId(data, id);
    return data;
}

std::string metaData(const ComponentMeta& meta)
{
    std::string data;
    appendId(data, meta.id);
    appendId(data, meta.revision);
    appendLittleEndian(data, static_cast<uint64_t>(meta.modifiedMs), 8);
    appendLittleEndian(data, meta.contentHash, 8);
    data += meta.author;
    return data;
}

std::string pictureData(const IndexedFrame& picture)
{
    std::string data;
    appendLittleEndian(data, static_cast<uint64_t>(picture.size.width), 2);
    appendLittleEndian(data, static_cast<uint64_t>(picture.size.height), 2);
    data.append(reinterpret_cast<const char*>(picture.pixels.data()), picture.pixels.size());
    return data;
}

template <typename T>
std::string valuesData(const T* values, std::size_t count)
{
    std::string data;
    data.reserve(count * sizeof(T));
    for (std::size_t i = 0; i < count; ++i) {
        appendLittleEndian(data, static_cast<uint64_t>(values[i]), sizeof(T));
    }
    return data;
}

std::string pictureData(const Rgb565Frame& picture)
{
    std::string data;
    appendLittleEndian(data, static_cast<uint64_t>(picture.size.width), 2);
    appendLittleEndian(data, static_cast<uint64_t>(picture.size.height), 2);
    data += valuesData(picture.pixels.data(), picture.pixels.size());
    return data;
}

std::string lightsData(const document::LightActions& actions)
{
    std::string data;
    for (const document::LightAction& action : actions) {
        appendLittleEndian(data, static_cast<uint8_t>(action.kind), 1);
        appendLittleEndian(data, action.automatic ? 1 : 0, 1);
        appendLittleEndian(data, action.effectId, 2);
        appendLittleEndian(data, action.color, 4);
    }
    return data;
}

std::string dofEventsData(const document::DofEvents& events)
{
    std::string data;
    for (const document::DofEvent& event : events) {
        appendLittleEndian(data, static_cast<uint8_t>(event.type), 1);
        appendLittleEndian(data, event.offValue ? 1 : 0, 1);
        for (const int32_t value : {event.number, event.value, event.offValue.value_or(0)}) {
            appendLittleEndian(data, static_cast<uint32_t>(value), 4);
        }
    }
    return data;
}

std::string stringListData(const std::vector<std::string>& texts)
{
    std::string data;
    appendLittleEndian(data, texts.size(), 4);
    for (const std::string& text : texts) {
        appendLittleEndian(data, text.size(), 4);
        data += text;
    }
    return data;
}

std::string hexOf(uint64_t value, int digits)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        text.push_back(kDigits[(value >> shift) & 0xf]);
    }
    return text;
}

// RGB565 colors as four hex digits each.
template <typename Range>
std::string colorsHex(const Range& colors)
{
    std::string text;
    for (const uint16_t color : colors) {
        text += hexOf(color, 4);
    }
    return text;
}

JsonValue metaJson(const ComponentMeta& meta)
{
    JsonValue value = JsonValue::object();
    value.set("id", meta.id.toString());
    if (!meta.revision.isNull()) {
        value.set("revision", meta.revision.toString());
    }
    if (!meta.author.empty()) {
        value.set("author", meta.author);
    }
    if (meta.modifiedMs != 0) {
        value.set("modified", meta.modifiedMs);
    }
    if (meta.contentHash != 0) {
        value.set("hash", hexOf(meta.contentHash, 16));
    }
    return value;
}

std::string layoutName(render::PlaneLayout layout)
{
    switch (layout) {
    case render::PlaneLayout::NativeExtra:
        return "native-extra";
    case render::PlaneLayout::NativeOriginal:
        return "native-original";
    case render::PlaneLayout::Ordinary:
        break;
    }
    return "ordinary";
}

// Projects are made for libserum 2.9 (document/ProjectLimits.h). The manifest keeps naming it,
// as editors before 3.99.14 refuse a file without it; a reader ignores it (earlier files may
// name "2.8").
constexpr const char* kLibserumTargetName = "2.9";

constexpr std::array<const char*, document::kLastLicenseKind + 1> kLicenseNames = {
    "unspecified", "all-rights-reserved", "cc0-1.0",  "cc-by-4.0",
    "cc-by-sa-4.0", "cc-by-nc-4.0",       "cc-by-nc-sa-4.0", "custom",
};

// Index of a component by its position, and the UUIDs of each kind, for the references.
struct References {
    std::vector<ComponentId> frames;
    std::vector<ComponentId> masks;
    std::vector<ComponentId> backgrounds;
    std::vector<ComponentId> sprites;
};

void requireIds(const std::vector<ComponentId>& ids, const char* kind)
{
    std::unordered_set<ComponentId, ComponentIdHash> seen;
    for (const ComponentId& id : ids) {
        if (id.isNull() || !seen.insert(id).second) {
            throw std::invalid_argument(std::string("a ") + kind +
                                        " of the snapshot has no ID of its own");
        }
    }
}

References referencesOf(const ProjectSnapshot& snapshot)
{
    References references;
    for (const auto& frame : snapshot.frames) {
        references.frames.push_back(frame.meta.id);
    }
    if (snapshot.comparisonMaskMeta.size() != snapshot.comparisonMasks.size()) {
        throw std::invalid_argument("the snapshot lacks the comparison masks' IDs");
    }
    for (const ComponentMeta& meta : snapshot.comparisonMaskMeta) {
        references.masks.push_back(meta.id);
    }
    for (const auto& background : snapshot.backgrounds) {
        references.backgrounds.push_back(background.meta.id);
    }
    for (const auto& sprite : snapshot.sprites) {
        references.sprites.push_back(sprite.meta.id);
    }
    requireIds(references.frames, "frame");
    requireIds(references.masks, "comparison mask");
    requireIds(references.backgrounds, "background");
    requireIds(references.sprites, "sprite");
    std::vector<ComponentId> palettes;
    for (const auto& palette : snapshot.palettes) {
        palettes.push_back(palette.meta.id);
    }
    requireIds(palettes, "palette");
    return references;
}

// The fields of a component after META: what its content hash is taken of.
std::string frameContent(const ProjectSnapshot& snapshot,
                         std::size_t frameIndex,
                         const References& references)
{
    const document::ProjectFrameSnapshot& frame = snapshot.frames[frameIndex];
    std::string out;
    appendField(out, "ORIG", pictureData(frame.originalSd));
    appendField(out, "COLS", pictureData(frame.colorizedSd));
    if (frame.colorizedHd) {
        appendField(out, "COLH", pictureData(*frame.colorizedHd));
    }
    if (frame.inactiveHd) {
        appendField(out, "INHD", pictureData(*frame.inactiveHd));
    }
    appendField(out, "SHAP", integerData(frame.shapeComparisonMode, 1));
    if (frame.comparisonMaskId < references.masks.size()) {
        appendField(out, "CMSK", idData(references.masks[frame.comparisonMaskId]));
    } else if (frame.comparisonMaskId != document::kNoComparisonMaskId) {
        appendField(out, "CMSX", integerData(frame.comparisonMaskId, 1));
    }
    if (frame.dynamicData) {
        const document::FrameDynamicData& dynamic = *frame.dynamicData;
        std::string data;
        appendField(data, "DMSK", pictureData(dynamic.maskSd));
        appendField(data, "DMSH", pictureData(dynamic.maskHd));
        appendField(data, "DCOS", valuesData(dynamic.colorsSd.data(), dynamic.colorsSd.size()));
        appendField(data, "DCOH", valuesData(dynamic.colorsHd.data(), dynamic.colorsHd.size()));
        appendField(data, "SDDS",
                    valuesData(dynamic.shadowDirectionsSd.data(),
                               dynamic.shadowDirectionsSd.size()));
        appendField(data, "SDCS",
                    valuesData(dynamic.shadowColorsSd.data(), dynamic.shadowColorsSd.size()));
        appendField(data, "SDDH",
                    valuesData(dynamic.shadowDirectionsHd.data(),
                               dynamic.shadowDirectionsHd.size()));
        appendField(data, "SDCH",
                    valuesData(dynamic.shadowColorsHd.data(), dynamic.shadowColorsHd.size()));
        appendField(out, "DYNA", data);
    }
    if (frame.backgroundData) {
        const document::FrameBackgroundData& background = *frame.backgroundData;
        std::string data;
        if (background.backgroundId < references.backgrounds.size()) {
            appendField(data, "BGID", idData(references.backgrounds[background.backgroundId]));
        } else {
            appendField(data, "BGIX", integerData(background.backgroundId, 2));
        }
        appendField(data, "BMSK", pictureData(background.maskSd));
        appendField(data, "BMSH", pictureData(background.maskHd));
        appendField(out, "BGND", data);
    }
    {
        std::string data;
        appendLittleEndian(data, frame.spriteReferences.size(), 4);
        for (const document::FrameSpriteReference& slot : frame.spriteReferences) {
            const bool known = slot.spriteId < references.sprites.size();
            appendId(data, known ? references.sprites[slot.spriteId] : ComponentId{});
            appendLittleEndian(data, known ? document::kNoSpriteId : slot.spriteId, 2);
            for (const uint16_t value : {slot.minX, slot.minY, slot.maxX, slot.maxY}) {
                appendLittleEndian(data, value, 2);
            }
        }
        appendField(out, "SPRS", data);
    }
    {
        // The sprite offsets of the slots that have one (a field older editors skip).
        std::string data;
        for (std::size_t slot = 0; slot < frame.spriteReferences.size(); ++slot) {
            const document::FrameSpriteReference& reference = frame.spriteReferences[slot];
            if (reference.offsetX != 0 || reference.offsetY != 0) {
                appendLittleEndian(data, slot, 1);
                appendLittleEndian(data, static_cast<uint16_t>(reference.offsetX), 2);
                appendLittleEndian(data, static_cast<uint16_t>(reference.offsetY), 2);
            }
        }
        if (!data.empty()) {
            appendField(out, "SPRO", data);
        }
    }
    // One ID for an editor before 3.99.17, the three triggers for the others.
    appendField(out, "TRIG",
                integerData(document::combinedTriggerId(frame.pupTriggerId, frame.sceneTriggerId,
                                                        frame.monochromeTriggerId),
                            4));
    {
        std::string data;
        for (const uint32_t trigger :
             {frame.pupTriggerId, frame.sceneTriggerId, frame.monochromeTriggerId}) {
            appendLittleEndian(data, trigger, 4);
        }
        appendField(out, "TRGS", data);
    }
    appendField(out, "DURA", integerData(frame.durationMs, 4));
    appendField(out, "ROTS",
                valuesData(frame.colorRotationsSd.data(), frame.colorRotationsSd.size()));
    appendField(out, "ROTH",
                valuesData(frame.colorRotationsHd.data(), frame.colorRotationsHd.size()));
    if (frame.scene) {
        std::string data;
        appendLittleEndian(data, frame.scene->sceneId, 2);
        appendLittleEndian(data, frame.scene->group, 1);
        appendLittleEndian(data, frame.scene->frameIndex, 2);
        appendField(out, "SCEN", data);
    }
    if (!frame.tags.empty()) {
        appendField(out, "TAGS", stringListData(frame.tags));
    }
    appendField(out, "LGHT", lightsData(frame.lights));
    if (frame.originalOnlyDynamicSets != 0) {
        appendField(out, "OOSD", integerData(frame.originalOnlyDynamicSets, 4));
    }
    if (!frame.dofEvents.empty()) {
        appendField(out, "DOFE", dofEventsData(frame.dofEvents));
    }
    const std::vector<uint8_t>& palettes = snapshot.editorMetadata.importedPalettes;
    if ((frameIndex + 1) * document::kImportedPaletteBytes <= palettes.size()) {
        const std::size_t offset = frameIndex * document::kImportedPaletteBytes;
        appendField(out, "IPAL",
                    std::string(reinterpret_cast<const char*>(palettes.data()) + offset,
                                document::kImportedPaletteBytes));
    }
    return out;
}

std::string maskContent(const ProjectSnapshot& snapshot, std::size_t maskIndex)
{
    std::string out;
    appendField(out, "NAME",
                document::fixedName(snapshot.editorMetadata.maskNames, maskIndex,
                                    document::kMaskNameBytes));
    appendField(out, "PICT", pictureData(snapshot.comparisonMasks[maskIndex]));
    return out;
}

std::string backgroundContent(const document::BackgroundRecord& background)
{
    std::string out;
    appendField(out, "COLS", pictureData(background.colorizedSd));
    if (background.colorizedHd) {
        appendField(out, "COLH", pictureData(*background.colorizedHd));
    }
    if (background.inactiveHd) {
        appendField(out, "INHD", pictureData(*background.inactiveHd));
    }
    return out;
}

std::string spriteContent(const ProjectSnapshot& snapshot,
                          std::size_t spriteIndex,
                          const References& references)
{
    const document::SpriteRecord& sprite = snapshot.sprites[spriteIndex];
    const document::EditorMetadata& tables = snapshot.editorMetadata;
    std::string out;
    appendField(out, "NAME", sprite.name);
    appendField(out, "FLAG", integerData(sprite.hasHd ? 1 : 0, 1));
    appendField(out, "ORIG", pictureData(sprite.originalSd));
    appendField(out, "COLS", pictureData(sprite.colorizedSd));
    appendField(out, "MSKH", pictureData(sprite.maskHd));
    appendField(out, "COLH", pictureData(sprite.colorizedHd));
    appendField(out, "DETD",
                valuesData(sprite.detectionDwords.data(), sprite.detectionDwords.size()));
    appendField(out, "DETP",
                valuesData(sprite.detectionDwordPositions.data(),
                           sprite.detectionDwordPositions.size()));
    {
        std::string data;
        for (const document::SpriteDetectionArea& area : sprite.detectionAreas) {
            for (const uint16_t value : {area.x, area.y, area.width, area.height}) {
                appendLittleEndian(data, value, 2);
            }
        }
        appendField(out, "DETA", data);
    }
    appendField(out, "DMSK", pictureData(sprite.dynamicMaskSd));
    appendField(out, "DMSH", pictureData(sprite.dynamicMaskHd));
    appendField(out, "DCOS",
                valuesData(sprite.dynamicColorsSd.data(), sprite.dynamicColorsSd.size()));
    appendField(out, "DCOH",
                valuesData(sprite.dynamicColorsHd.data(), sprite.dynamicColorsHd.size()));
    appendField(out, "SHAP", integerData(sprite.shapeMode, 1));
    if (spriteIndex < tables.spriteSourceFrames.size()) {
        const uint32_t source = tables.spriteSourceFrames[spriteIndex];
        if (source < references.frames.size()) {
            appendField(out, "SRCF", idData(references.frames[source]));
        } else {
            appendField(out, "SRCX", integerData(source, 4));
        }
        appendField(out, "SRCR",
                    valuesData(tables.spriteSourceRects.data() + spriteIndex * 4, 4));
        appendField(out, "MIRR",
                    valuesData(tables.spriteSourceMirrors.data() + spriteIndex * 2, 2));
    }
    return out;
}

JsonValue manifestJson(const ProjectSnapshot& snapshot, const ProjectFileOptions& options)
{
    JsonValue manifest = JsonValue::object();
    manifest.set("format", kProjectFormatName);
    manifest.set("formatVersion", kProjectFormatVersion);
    manifest.set("kind", options.kind == FileKind::Package   ? "package"
                         : options.kind == FileKind::Changes ? "changes"
                                                             : "project");
    manifest.set("editorVersion", options.editorVersion);
    manifest.set("projectId", snapshot.projectId.toString());
    manifest.set("romName", snapshot.romName);
    manifest.set("colorCount", snapshot.colorCount);
    manifest.set("libserumTarget", kLibserumTargetName);
    manifest.set("layout", layoutName(snapshot.geometry.layout()));
    // The cROM header revision written (format version 1); editors before 3.99.14 refuse a
    // file without it, a reader ignores it.
    manifest.set("cromRevision", 21);
    JsonValue license = JsonValue::object();
    license.set("kind", kLicenseNames[static_cast<std::size_t>(snapshot.license.kind)]);
    license.set("text", snapshot.license.customText);
    license.set("attribution", snapshot.license.attribution);
    license.set("url", snapshot.license.url);
    manifest.set("license", std::move(license));
    if (snapshot.colorizationSettings) {
        const document::ColorizationSettings& settings = *snapshot.colorizationSettings;
        JsonValue colorization = JsonValue::object();
        colorization.set("scalingAlgorithm", settings.scalingAlgorithm);
        colorization.set("shadowOffset", settings.shadowOffset);
        JsonValue scenes = JsonValue::array();
        for (const document::SceneDefinition& scene : settings.scenes) {
            JsonValue item = JsonValue::object();
            item.set("id", scene.sceneId);
            item.set("frameCount", scene.frameCount);
            item.set("durationMs", scene.durationMs);
            item.set("interruptable", scene.interruptable);
            item.set("immediateStart", scene.immediateStart);
            item.set("repeat", scene.repeat);
            item.set("frameGroups", scene.frameGroups);
            item.set("random", scene.random);
            item.set("autoStartSeconds", scene.autoStartSeconds);
            item.set("options", scene.options);
            if (!scene.name.empty()) {
                item.set("name", scene.name);
            }
            if (scene.baseSceneId) {
                item.set("framesOf", *scene.baseSceneId);
            }
            scenes.push(std::move(item));
        }
        colorization.set("scenes", std::move(scenes));
        manifest.set("colorization", std::move(colorization));
    }
    if (!snapshot.unsupportedPayloads.empty()) {
        JsonValue unsupported = JsonValue::array();
        for (const document::UnsupportedLegacyPayload& payload : snapshot.unsupportedPayloads) {
            unsupported.push(payload.name);
        }
        manifest.set("unsupported", std::move(unsupported));
    }
    return manifest;
}

JsonValue idsJson(const std::vector<ComponentId>& ids)
{
    JsonValue array = JsonValue::array();
    for (const ComponentId& id : ids) {
        array.push(id.toString());
    }
    return array;
}

JsonValue orderJson(const ProjectSnapshot& snapshot, const References& references)
{
    JsonValue order = JsonValue::object();
    order.set("frames", idsJson(references.frames));
    order.set("comparisonMasks", idsJson(references.masks));
    order.set("backgrounds", idsJson(references.backgrounds));
    order.set("sprites", idsJson(references.sprites));
    const document::EditorMetadata& tables = snapshot.editorMetadata;
    JsonValue sections = JsonValue::array();
    for (std::size_t s = 0; s < tables.sectionCount && s < tables.sectionFirstFrames.size();
         ++s) {
        JsonValue section = JsonValue::object();
        section.set("name",
                    document::fixedName(tables.sectionNames, s, document::kSectionNameBytes));
        const uint32_t first = tables.sectionFirstFrames[s];
        if (first < references.frames.size()) {
            section.set("first", references.frames[first].toString());
        } else {
            section.set("firstIndex", first);
        }
        sections.push(std::move(section));
    }
    order.set("sections", std::move(sections));
    return order;
}

// The change information to write for a component of that content: as it is, or with a new
// revision, the author and the time when the save stamps revisions and the content is not the
// one of its revision.
ComponentMeta stampedMeta(const ComponentMeta& meta,
                          document::ComponentKind kind,
                          std::string_view content,
                          const ProjectFileOptions& options,
                          std::vector<document::StampedMeta>* stamped)
{
    if (!options.stamp) {
        return meta;
    }
    const uint64_t hash = document::contentHash(content);
    if (hash == meta.contentHash && !meta.revision.isNull()) {
        return meta;
    }
    ComponentMeta written = meta;
    written.revision = ComponentId::generate();
    written.author = options.stamp->author;
    written.modifiedMs = options.stamp->modifiedMs;
    written.contentHash = hash;
    if (stamped) {
        stamped->push_back({kind, written});
    }
    return written;
}

std::string componentEntry(const ComponentMeta& meta,
                           document::ComponentKind kind,
                           const std::string& content,
                           const ProjectFileOptions& options,
                           std::vector<document::StampedMeta>* stamped)
{
    std::string out;
    appendField(out, "META", metaData(stampedMeta(meta, kind, content, options, stamped)));
    out += content;
    return out;
}

JsonValue palettesJson(const ProjectSnapshot& snapshot,
                       const ProjectFileOptions& options,
                       std::vector<document::StampedMeta>* stamped)
{
    JsonValue palettes = JsonValue::array();
    for (const document::PaletteRecord& palette : snapshot.palettes) {
        JsonValue item = JsonValue::object();
        std::string content = palette.name;
        content.push_back('\0');
        content += valuesData(palette.colors.data(), palette.colors.size());
        item.set("meta", metaJson(stampedMeta(palette.meta, document::ComponentKind::Palette,
                                              content, options, stamped)));
        item.set("name", palette.name);
        item.set("colors", colorsHex(palette.colors));
        palettes.push(std::move(item));
    }
    JsonValue root = JsonValue::object();
    root.set("palettes", std::move(palettes));
    return root;
}

JsonValue actionJson(const document::LightAction& action)
{
    JsonValue value = JsonValue::object();
    value.set("kind", static_cast<int>(action.kind));
    value.set("effect", action.effectId);
    value.set("color", action.color);
    value.set("automatic", action.automatic);
    return value;
}

JsonValue lightsJson(const document::SpeakerLightSettings& lights)
{
    JsonValue effects = JsonValue::array();
    for (const document::LightEffect& effect : lights.effects) {
        JsonValue item = JsonValue::object();
        item.set("id", effect.id);
        item.set("name", effect.name);
        item.set("mode", effect.mode);
        item.set("options", effect.options);
        item.set("speed", effect.speed);
        item.set("durationMs", effect.durationMs);
        JsonValue colors = JsonValue::array();
        for (const uint32_t color : effect.colors) {
            colors.push(color);
        }
        item.set("colors", std::move(colors));
        item.set("brightness", effect.brightness);
        effects.push(std::move(item));
    }
    JsonValue defaults = JsonValue::array();
    for (const document::LightAction& action : lights.defaults) {
        defaults.push(actionJson(action));
    }
    JsonValue root = JsonValue::object();
    root.set("effects", std::move(effects));
    root.set("defaults", std::move(defaults));
    return root;
}

JsonValue zoneJson(const document::PictureZone& zone)
{
    JsonValue value = JsonValue::object();
    value.set("x", zone.x);
    value.set("y", zone.y);
    value.set("width", zone.width);
    value.set("height", zone.height);
    return value;
}

JsonValue editorJson(const ProjectSnapshot& snapshot)
{
    const document::EditorMetadata& tables = snapshot.editorMetadata;
    JsonValue root = JsonValue::object();
    root.set("editColors", colorsHex(tables.editColors));
    JsonValue colorSets = JsonValue::array();
    for (std::size_t set = 0; set < document::kMaxColorSets; ++set) {
        JsonValue item = JsonValue::object();
        item.set("active", tables.activeColorSets[set] != 0);
        item.set("name", document::fixedName(tables.colorSetNames, set,
                                             document::kColorSetNameBytes));
        const auto begin = tables.colorSets.begin() +
                           static_cast<std::ptrdiff_t>(set * document::kColorSetColorCount);
        item.set("colors",
                 colorsHex(std::vector<uint16_t>(
                     begin, begin + static_cast<std::ptrdiff_t>(document::kColorSetColorCount))));
        colorSets.push(std::move(item));
    }
    root.set("colorSets", std::move(colorSets));
    root.set("currentColorSet", tables.currentColorSet);
    root.set("firstDisplayedColorSet", tables.firstDisplayedColorSet);
    root.set("drawColorMode", tables.drawColorMode);
    root.set("drawMode", tables.drawMode);
    root.set("maskSelectionMode", tables.maskSelectionMode);
    root.set("fillMode", tables.fillMode);
    JsonValue positions = JsonValue::array();
    for (std::size_t p = 0; p < tables.imagePositionCount; ++p) {
        JsonValue item = JsonValue::object();
        item.set("name", document::fixedName(tables.imagePositionNames, p,
                                             document::kImagePositionNameBytes));
        JsonValue values = JsonValue::array();
        for (std::size_t v = 0; v < document::kImagePositionValueCount; ++v) {
            values.push(tables.imagePositions[p * document::kImagePositionValueCount + v]);
        }
        item.set("values", std::move(values));
        positions.push(std::move(item));
    }
    root.set("imagePositions", std::move(positions));
    root.set("timeElapsedMs", tables.timeElapsedMs);
    if (!tables.authorTimeMs.empty()) {
        root.set("authorTimes", authorTimesJson(tables.authorTimeMs));
    }
    JsonValue pup = JsonValue::object();
    pup.set("has", tables.hasPupPack);
    pup.set("name", tables.pupPackName);
    root.set("pupPack", std::move(pup));
    JsonValue lines = JsonValue::array();
    for (const document::HelperLine& line : snapshot.helperLines) {
        JsonValue item = JsonValue::object();
        item.set("name", line.name);
        item.set("vertical", line.vertical);
        item.set("position", line.position);
        item.set("plane", line.plane == render::Resolution::Hd ? "hd" : "sd");
        item.set("visible", line.visible);
        lines.push(std::move(item));
    }
    root.set("helperLines", std::move(lines));
    JsonValue zones = JsonValue::array();
    for (const document::SavedImageZone& zone : snapshot.imageZones) {
        JsonValue item = JsonValue::object();
        item.set("name", zone.name);
        item.set("start", zoneJson(zone.start));
        if (zone.end) {
            item.set("end", zoneJson(*zone.end));
        }
        item.set("brightness", zone.brightness);
        item.set("contrast", zone.contrast);
        item.set("blur", zone.blur);
        zones.push(std::move(item));
    }
    root.set("imageZones", std::move(zones));
    return root;
}

std::string entryName(const char* folder, const ComponentId& id)
{
    return std::string(folder) + "/" + id.toString() + ".bin";
}

// The folder of a kind's entries; none for palettes, which are JSON.
const char* folderOf(document::ComponentKind kind)
{
    switch (kind) {
    case document::ComponentKind::Frame:
        return "frames";
    case document::ComponentKind::ComparisonMask:
        return "masks";
    case document::ComponentKind::Background:
        return "backgrounds";
    case document::ComponentKind::Sprite:
        return "sprites";
    case document::ComponentKind::Palette:
        break;
    }
    return nullptr;
}

constexpr std::array<document::ComponentKind, document::kComponentKindCount> kKinds = {
    document::ComponentKind::Frame, document::ComponentKind::ComparisonMask,
    document::ComponentKind::Background, document::ComponentKind::Sprite,
    document::ComponentKind::Palette};

void appendRevisions(std::string& out, const document::RevisionMaps& maps)
{
    for (const document::RevisionMap& map : maps) {
        // In the order of the IDs, so that a file is written the same way every time.
        std::vector<std::pair<ComponentId, ComponentId>> sorted(map.begin(), map.end());
        std::sort(sorted.begin(), sorted.end());
        appendLittleEndian(out, sorted.size(), 4);
        for (const auto& [id, revision] : sorted) {
            appendId(out, id);
            appendId(out, revision);
        }
    }
}

std::string teamCommonData(const document::TeamState& team)
{
    std::string out;
    appendRevisions(out, team.common);
    appendLittleEndian(out, team.commonOrder.size(), 4);
    for (const ComponentId& id : team.commonOrder) {
        appendId(out, id);
    }
    appendLittleEndian(out, team.sent.size(), 4);
    for (const document::SentPackage& sent : team.sent) {
        appendRevisions(out, sent.components);
    }
    return out;
}

JsonValue stringsJson(const std::vector<std::string>& texts)
{
    JsonValue array = JsonValue::array();
    for (const std::string& text : texts) {
        array.push(text);
    }
    return array;
}

JsonValue teamStateJson(const document::TeamState& team)
{
    JsonValue root = JsonValue::object();
    root.set("repository", team.repository);
    root.set("owner", team.ownerLogin);
    root.set("members", stringsJson(team.members));
    root.set("snapshotId", team.snapshotId.toString());
    root.set("commonSettingsHash", hexOf(team.commonSettingsHash, 16));
    JsonValue sent = JsonValue::array();
    for (const document::SentPackage& package : team.sent) {
        JsonValue item = JsonValue::object();
        item.set("id", package.id.toString());
        item.set("created", package.createdMs);
        item.set("mergedBy", stringsJson(package.mergedBy));
        sent.push(std::move(item));
    }
    root.set("sent", std::move(sent));
    JsonValue merged = JsonValue::array();
    for (const ComponentId& id : team.mergedPackages) {
        merged.push(id.toString());
    }
    root.set("mergedPackages", std::move(merged));
    return root;
}

// The revision in an entry's META, or nothing for an entry that has none.
std::optional<ComponentId> revisionOfEntry(std::string_view entry)
{
    // char[4] tag, uint32 0, uint64 size, then the UUID and the revision.
    if (entry.size() < 16 + 32 || entry.substr(0, 4) != "META") {
        return std::nullopt;
    }
    ComponentId revision;
    for (int i = 0; i < 8; ++i) {
        revision.high = (revision.high << 8) | static_cast<unsigned char>(entry[32 + i]);
        revision.low = (revision.low << 8) | static_cast<unsigned char>(entry[40 + i]);
    }
    return revision;
}

// Reading --------------------------------------------------------------------------------

class Cursor {
public:
    Cursor(std::string_view data, std::string what)
        : m_data(data),
          m_what(std::move(what))
    {
    }

    [[nodiscard]] bool atEnd() const { return m_position == m_data.size(); }
    [[nodiscard]] std::size_t remaining() const { return m_data.size() - m_position; }

    uint64_t integer(std::size_t bytes)
    {
        need(bytes);
        uint64_t value = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            value |= static_cast<uint64_t>(static_cast<unsigned char>(m_data[m_position + i]))
                     << (8 * i);
        }
        m_position += bytes;
        return value;
    }

    ComponentId id()
    {
        need(16);
        ComponentId id;
        for (int i = 0; i < 8; ++i) {
            id.high = (id.high << 8) | static_cast<unsigned char>(m_data[m_position + i]);
            id.low = (id.low << 8) | static_cast<unsigned char>(m_data[m_position + 8 + i]);
        }
        m_position += 16;
        return id;
    }

    std::string_view bytes(std::size_t count)
    {
        need(count);
        const std::string_view result = m_data.substr(m_position, count);
        m_position += count;
        return result;
    }

    std::string_view rest() { return bytes(remaining()); }

    [[noreturn]] void fail() const { throw FormatError(m_what + " is damaged"); }

    void need(std::size_t bytes) const
    {
        if (remaining() < bytes) {
            fail();
        }
    }

private:
    std::string_view m_data;
    std::size_t m_position = 0;
    std::string m_what;
};

// The fields of an entry by tag; the last of a tag counts.
class Fields {
public:
    Fields(std::string_view data, std::string what)
        : m_what(std::move(what))
    {
        Cursor cursor(data, m_what);
        while (!cursor.atEnd()) {
            Tag tag{};
            const std::string_view tagBytes = cursor.bytes(4);
            std::copy(tagBytes.begin(), tagBytes.end(), tag.begin());
            (void)cursor.integer(4);
            const uint64_t size = cursor.integer(8);
            if (size > cursor.remaining()) {
                cursor.fail();
            }
            m_fields[std::string(tag.data(), 4)] = cursor.bytes(static_cast<std::size_t>(size));
        }
    }

    [[nodiscard]] bool has(const char* tag) const { return m_fields.count(tag) > 0; }

    [[nodiscard]] std::string_view get(const char* tag) const
    {
        const auto found = m_fields.find(tag);
        if (found == m_fields.end()) {
            throw FormatError(m_what + " lacks its field " + tag);
        }
        return found->second;
    }

    [[nodiscard]] Cursor cursor(const char* tag) const
    {
        return Cursor(get(tag), m_what + " (" + tag + ")");
    }

    [[nodiscard]] uint64_t integer(const char* tag, std::size_t bytes) const
    {
        Cursor field = cursor(tag);
        const uint64_t value = field.integer(bytes);
        return value;
    }

    [[nodiscard]] Fields nested(const char* tag) const
    {
        return Fields(get(tag), m_what + " (" + tag + ")");
    }

    [[nodiscard]] const std::string& what() const { return m_what; }

private:
    std::unordered_map<std::string, std::string_view> m_fields;
    std::string m_what;
};

ComponentMeta readMeta(const Fields& fields)
{
    Cursor cursor = fields.cursor("META");
    ComponentMeta meta;
    meta.id = cursor.id();
    meta.revision = cursor.id();
    meta.modifiedMs = static_cast<int64_t>(cursor.integer(8));
    meta.contentHash = cursor.integer(8);
    meta.author = std::string(cursor.rest());
    return meta;
}

std::pair<render::Size, Cursor> pictureHeader(const Fields& fields, const char* tag,
                                              std::size_t bytesPerPixel)
{
    Cursor cursor = fields.cursor(tag);
    render::Size size;
    size.width = static_cast<int>(cursor.integer(2));
    size.height = static_cast<int>(cursor.integer(2));
    if (cursor.remaining() !=
        static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) *
            bytesPerPixel) {
        cursor.fail();
    }
    return {size, cursor};
}

IndexedFrame readIndexed(const Fields& fields, const char* tag)
{
    auto [size, cursor] = pictureHeader(fields, tag, 1);
    IndexedFrame picture;
    picture.size = size;
    const std::string_view pixels = cursor.rest();
    picture.pixels.assignValues(reinterpret_cast<const uint8_t*>(pixels.data()), pixels.size());
    picture.pixels.compact();
    return picture;
}

template <typename T>
std::vector<T> readValues(Cursor cursor)
{
    if (cursor.remaining() % sizeof(T) != 0) {
        cursor.fail();
    }
    std::vector<T> values(cursor.remaining() / sizeof(T));
    for (T& value : values) {
        value = static_cast<T>(cursor.integer(sizeof(T)));
    }
    return values;
}

template <typename T>
document::CompactArray<T> readArray(const Fields& fields, const char* tag)
{
    document::CompactArray<T> array(readValues<T>(fields.cursor(tag)));
    array.compact();
    return array;
}

Rgb565Frame readRgb565(const Fields& fields, const char* tag)
{
    auto [size, cursor] = pictureHeader(fields, tag, 2);
    Rgb565Frame picture;
    picture.size = size;
    picture.pixels = document::CompactArray<uint16_t>(readValues<uint16_t>(cursor));
    picture.pixels.compact();
    return picture;
}

std::optional<Rgb565Frame> readOptionalRgb565(const Fields& fields, const char* tag)
{
    if (!fields.has(tag)) {
        return std::nullopt;
    }
    return readRgb565(fields, tag);
}

ComponentId readId(const Fields& fields, const char* tag)
{
    Cursor cursor = fields.cursor(tag);
    return cursor.id();
}

document::LightActions readLights(const Fields& fields)
{
    document::LightActions actions{};
    Cursor cursor = fields.cursor("LGHT");
    for (document::LightAction& action : actions) {
        if (cursor.remaining() < kLightActionBytes) {
            break;
        }
        const uint64_t kind = cursor.integer(1);
        if (kind > static_cast<uint64_t>(document::LightActionKind::Off)) {
            cursor.fail();
        }
        action.kind = static_cast<document::LightActionKind>(kind);
        action.automatic = (cursor.integer(1) & 1) != 0;
        action.effectId = static_cast<uint16_t>(cursor.integer(2));
        action.color = static_cast<uint32_t>(cursor.integer(4));
    }
    return actions;
}

document::DofEvents readDofEvents(const Fields& fields)
{
    document::DofEvents events;
    if (!fields.has("DOFE")) {
        return events;
    }
    Cursor cursor = fields.cursor("DOFE");
    while (cursor.remaining() > 0) {
        document::DofEvent event;
        event.type = static_cast<char>(cursor.integer(1));
        const uint64_t flags = cursor.integer(1);
        if (!document::isDofEventType(event.type) || flags > 1) {
            cursor.fail();
        }
        event.number = static_cast<int32_t>(static_cast<uint32_t>(cursor.integer(4)));
        event.value = static_cast<int32_t>(static_cast<uint32_t>(cursor.integer(4)));
        const auto offValue = static_cast<int32_t>(static_cast<uint32_t>(cursor.integer(4)));
        if (flags != 0) {
            event.offValue = offValue;
        }
        events.push_back(event);
    }
    return events;
}

std::vector<std::string> readStringList(Cursor cursor)
{
    const uint64_t count = cursor.integer(4);
    if (count > cursor.remaining() / 4) {
        cursor.fail();
    }
    std::vector<std::string> texts;
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t length = cursor.integer(4);
        texts.emplace_back(cursor.bytes(static_cast<std::size_t>(length)));
    }
    return texts;
}

// Index of each UUID of a kind, for resolving references.
using IdIndex = std::unordered_map<ComponentId, std::size_t, ComponentIdHash>;

IdIndex indexOf(const std::vector<ComponentId>& ids, const std::string& kind)
{
    IdIndex index;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i].isNull() || !index.emplace(ids[i], i).second) {
            throw FormatError("the order lists a " + kind + " twice or without an ID");
        }
    }
    return index;
}

std::size_t resolve(const IdIndex& index, const ComponentId& id, const std::string& what)
{
    const auto found = index.find(id);
    if (found == index.end()) {
        throw FormatError(what + " refers to a component the project does not have");
    }
    return found->second;
}

struct ReadIndexes {
    IdIndex frames;
    IdIndex masks;
    IdIndex backgrounds;
    IdIndex sprites;
};

document::ProjectFrameSnapshot readFrame(const Fields& fields,
                                         const ReadIndexes& indexes,
                                         std::string* importedPalette)
{
    document::ProjectFrameSnapshot frame;
    frame.meta = readMeta(fields);
    frame.originalSd = readIndexed(fields, "ORIG");
    frame.colorizedSd = readRgb565(fields, "COLS");
    frame.colorizedHd = readOptionalRgb565(fields, "COLH");
    frame.inactiveHd = readOptionalRgb565(fields, "INHD");
    frame.shapeComparisonMode = static_cast<uint8_t>(fields.integer("SHAP", 1));
    if (fields.has("CMSK")) {
        frame.comparisonMaskId = static_cast<uint8_t>(
            resolve(indexes.masks, readId(fields, "CMSK"), fields.what()));
    } else if (fields.has("CMSX")) {
        frame.comparisonMaskId = static_cast<uint8_t>(fields.integer("CMSX", 1));
    } else {
        frame.comparisonMaskId = document::kNoComparisonMaskId;
    }
    if (fields.has("DYNA")) {
        const Fields dynamicFields = fields.nested("DYNA");
        document::FrameDynamicData dynamic;
        dynamic.maskSd = readIndexed(dynamicFields, "DMSK");
        dynamic.maskHd = readIndexed(dynamicFields, "DMSH");
        dynamic.colorsSd = readArray<uint16_t>(dynamicFields, "DCOS");
        dynamic.colorsHd = readArray<uint16_t>(dynamicFields, "DCOH");
        dynamic.shadowDirectionsSd = readArray<uint8_t>(dynamicFields, "SDDS");
        dynamic.shadowColorsSd = readArray<uint16_t>(dynamicFields, "SDCS");
        dynamic.shadowDirectionsHd = readArray<uint8_t>(dynamicFields, "SDDH");
        dynamic.shadowColorsHd = readArray<uint16_t>(dynamicFields, "SDCH");
        frame.dynamicData = std::move(dynamic);
    }
    if (fields.has("BGND")) {
        const Fields backgroundFields = fields.nested("BGND");
        document::FrameBackgroundData background;
        if (backgroundFields.has("BGID")) {
            background.backgroundId = static_cast<uint16_t>(resolve(
                indexes.backgrounds, readId(backgroundFields, "BGID"), fields.what()));
        } else {
            background.backgroundId =
                static_cast<uint16_t>(backgroundFields.integer("BGIX", 2));
        }
        background.maskSd = readIndexed(backgroundFields, "BMSK");
        background.maskHd = readIndexed(backgroundFields, "BMSH");
        frame.backgroundData = std::move(background);
    }
    {
        Cursor cursor = fields.cursor("SPRS");
        const uint64_t count = cursor.integer(4);
        if (count > cursor.remaining() / kSpriteSlotBytes) {
            cursor.fail();
        }
        frame.spriteReferences.resize(static_cast<std::size_t>(count));
        for (document::FrameSpriteReference& slot : frame.spriteReferences) {
            const ComponentId id = cursor.id();
            const auto raw = static_cast<uint16_t>(cursor.integer(2));
            slot.spriteId = id.isNull()
                                ? raw
                                : static_cast<uint16_t>(
                                      resolve(indexes.sprites, id, fields.what()));
            slot.minX = static_cast<uint16_t>(cursor.integer(2));
            slot.minY = static_cast<uint16_t>(cursor.integer(2));
            slot.maxX = static_cast<uint16_t>(cursor.integer(2));
            slot.maxY = static_cast<uint16_t>(cursor.integer(2));
        }
    }
    if (fields.has("SPRO")) {
        Cursor cursor = fields.cursor("SPRO");
        while (cursor.remaining() > 0) {
            const uint64_t slot = cursor.integer(1);
            const auto x = static_cast<int16_t>(static_cast<uint16_t>(cursor.integer(2)));
            const auto y = static_cast<int16_t>(static_cast<uint16_t>(cursor.integer(2)));
            if (slot >= frame.spriteReferences.size()) {
                cursor.fail();
            }
            frame.spriteReferences[slot].offsetX = x;
            frame.spriteReferences[slot].offsetY = y;
        }
    }
    if (fields.has("TRGS")) {
        Cursor cursor = fields.cursor("TRGS");
        frame.pupTriggerId = static_cast<uint32_t>(cursor.integer(4));
        frame.sceneTriggerId = static_cast<uint32_t>(cursor.integer(4));
        frame.monochromeTriggerId = static_cast<uint32_t>(cursor.integer(4));
    } else {
        // The scenes are read later: linkPupTriggerScenes() then.
        document::setTriggersFromId(frame, static_cast<uint32_t>(fields.integer("TRIG", 4)),
                                    std::nullopt);
    }
    frame.durationMs = static_cast<uint32_t>(fields.integer("DURA", 4));
    frame.colorRotationsSd = readArray<uint16_t>(fields, "ROTS");
    frame.colorRotationsHd = readArray<uint16_t>(fields, "ROTH");
    if (fields.has("SCEN")) {
        Cursor cursor = fields.cursor("SCEN");
        document::SceneFrameRef scene;
        scene.sceneId = static_cast<uint16_t>(cursor.integer(2));
        scene.group = static_cast<uint8_t>(cursor.integer(1));
        scene.frameIndex = static_cast<uint16_t>(cursor.integer(2));
        frame.scene = scene;
    }
    if (fields.has("TAGS")) {
        frame.tags = readStringList(fields.cursor("TAGS"));
    }
    frame.lights = readLights(fields);
    frame.dofEvents = readDofEvents(fields);
    if (fields.has("OOSD")) {
        frame.originalOnlyDynamicSets = static_cast<uint32_t>(fields.integer("OOSD", 4));
    }
    if (fields.has("IPAL")) {
        const std::string_view palette = fields.get("IPAL");
        if (palette.size() != document::kImportedPaletteBytes) {
            throw FormatError(fields.what() + " has a damaged imported palette");
        }
        *importedPalette = std::string(palette);
    }
    return frame;
}

document::SpriteRecord readSprite(const Fields& fields,
                                  const ReadIndexes& indexes,
                                  std::size_t spriteIndex,
                                  document::EditorMetadata& tables)
{
    document::SpriteRecord sprite;
    sprite.meta = readMeta(fields);
    sprite.name = std::string(fields.get("NAME"));
    sprite.hasHd = (fields.integer("FLAG", 1) & 1) != 0;
    sprite.originalSd = readIndexed(fields, "ORIG");
    sprite.colorizedSd = readRgb565(fields, "COLS");
    sprite.maskHd = readIndexed(fields, "MSKH");
    sprite.colorizedHd = readRgb565(fields, "COLH");
    sprite.detectionDwords = readValues<uint32_t>(fields.cursor("DETD"));
    sprite.detectionDwordPositions = readValues<uint16_t>(fields.cursor("DETP"));
    {
        const std::vector<uint16_t> values = readValues<uint16_t>(fields.cursor("DETA"));
        if (values.size() % 4 != 0) {
            throw FormatError(fields.what() + " has damaged detection areas");
        }
        for (std::size_t i = 0; i < values.size(); i += 4) {
            sprite.detectionAreas.push_back(
                {values[i], values[i + 1], values[i + 2], values[i + 3]});
        }
    }
    sprite.dynamicMaskSd = readIndexed(fields, "DMSK");
    sprite.dynamicMaskHd = readIndexed(fields, "DMSH");
    sprite.dynamicColorsSd = readArray<uint16_t>(fields, "DCOS");
    sprite.dynamicColorsHd = readArray<uint16_t>(fields, "DCOH");
    sprite.shapeMode = static_cast<uint8_t>(fields.integer("SHAP", 1));

    document::ensureSpriteTableEntries(tables, spriteIndex + 1);
    document::setFixedName(tables.spriteNames, spriteIndex, document::kSpriteNameBytes,
                           sprite.name);
    if (fields.has("SRCF")) {
        tables.spriteSourceFrames[spriteIndex] = static_cast<uint32_t>(
            resolve(indexes.frames, readId(fields, "SRCF"), fields.what()));
    } else if (fields.has("SRCX")) {
        tables.spriteSourceFrames[spriteIndex] = static_cast<uint32_t>(fields.integer("SRCX", 4));
    }
    if (fields.has("SRCR")) {
        const std::vector<uint16_t> rect = readValues<uint16_t>(fields.cursor("SRCR"));
        if (rect.size() != 4) {
            throw FormatError(fields.what() + " has a damaged source rectangle");
        }
        std::copy(rect.begin(), rect.end(),
                  tables.spriteSourceRects.begin() + static_cast<std::ptrdiff_t>(spriteIndex * 4));
    }
    if (fields.has("MIRR")) {
        const std::vector<uint32_t> mirrors = readValues<uint32_t>(fields.cursor("MIRR"));
        if (mirrors.size() != 2) {
            throw FormatError(fields.what() + " has damaged mirrors");
        }
        std::copy(mirrors.begin(), mirrors.end(),
                  tables.spriteSourceMirrors.begin() +
                      static_cast<std::ptrdiff_t>(spriteIndex * 2));
    }
    return sprite;
}

uint64_t parseHex(const std::string& text, const std::string& what)
{
    if (text.empty() || text.size() > 16) {
        throw FormatError(what + " is no hexadecimal number");
    }
    uint64_t value = 0;
    for (const char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<uint64_t>(c - 'a' + 10);
        } else {
            throw FormatError(what + " is no hexadecimal number");
        }
    }
    return value;
}

std::vector<uint16_t> parseColorsHex(const std::string& text, const std::string& what)
{
    if (text.size() % 4 != 0) {
        throw FormatError(what + " has damaged colors");
    }
    std::vector<uint16_t> colors;
    for (std::size_t i = 0; i < text.size(); i += 4) {
        colors.push_back(static_cast<uint16_t>(parseHex(text.substr(i, 4), what)));
    }
    return colors;
}

ComponentId parseId(const std::string& text, const std::string& what)
{
    const std::optional<ComponentId> id = ComponentId::fromString(text);
    if (!id) {
        throw FormatError(what + " has a damaged ID");
    }
    return *id;
}

ComponentMeta readMetaJson(const JsonObjectReader& object)
{
    ComponentMeta meta;
    meta.id = parseId(object.string("id"), object.what());
    if (object.has("revision")) {
        meta.revision = parseId(object.string("revision"), object.what());
    }
    meta.author = object.string("author", "");
    meta.modifiedMs = object.integer("modified", INT64_MIN, INT64_MAX, 0);
    if (object.has("hash")) {
        meta.contentHash = parseHex(object.string("hash"), object.what());
    }
    return meta;
}

std::vector<ComponentId> readIds(const JsonObjectReader& object, std::string_view key)
{
    std::vector<ComponentId> ids;
    for (const JsonValue& item : object.array(key)) {
        const std::string* text = item.stringValue();
        if (!text) {
            throw FormatError("the " + object.what() + " has a damaged ID");
        }
        ids.push_back(parseId(*text, object.what()));
    }
    return ids;
}

JsonValue readJsonEntry(
    const std::function<std::optional<std::string>(const std::string&)>& read,
    const std::string& name,
    bool required)
{
    const std::optional<std::string> text = read(name);
    if (!text) {
        if (required) {
            throw FormatError("the project file has no " + name);
        }
        return JsonValue::object();
    }
    std::string error;
    std::optional<JsonValue> value = parseJson(*text, &error);
    if (!value) {
        throw FormatError(name + " is damaged: " + error);
    }
    return std::move(*value);
}

document::PictureZone readZone(const JsonObjectReader& object)
{
    document::PictureZone zone;
    zone.x = static_cast<uint32_t>(object.integer("x", 0, UINT32_MAX));
    zone.y = static_cast<uint32_t>(object.integer("y", 0, UINT32_MAX));
    zone.width = static_cast<uint32_t>(object.integer("width", 0, UINT32_MAX));
    zone.height = static_cast<uint32_t>(object.integer("height", 0, UINT32_MAX));
    return zone;
}

document::LightAction readAction(const JsonObjectReader& object)
{
    document::LightAction action;
    action.kind = static_cast<document::LightActionKind>(
        object.integer("kind", 0, static_cast<int>(document::LightActionKind::Off)));
    action.effectId = static_cast<uint16_t>(object.integer("effect", 0, UINT16_MAX));
    action.color = static_cast<uint32_t>(object.integer("color", 0, UINT32_MAX));
    action.automatic = object.boolean("automatic", false);
    return action;
}

void readManifest(const JsonValue& value, ProjectSnapshot& snapshot, FileKind* fileKind)
{
    const JsonObjectReader manifest(value, "manifest");
    if (manifest.string("format") != kProjectFormatName) {
        throw FormatError("the file is no project of this editor");
    }
    const int64_t version = manifest.integer("formatVersion", 1, INT32_MAX);
    if (version > kProjectFormatVersion) {
        throw FormatError("the project was saved by a newer editor (format version " +
                          std::to_string(version) + ")");
    }
    const std::string kind = manifest.string("kind", "project");
    if (kind != "project" && kind != "package" && kind != "changes") {
        throw FormatError("the file holds an unknown kind of data: " + kind);
    }
    if (fileKind) {
        *fileKind = kind == "package"   ? FileKind::Package
                    : kind == "changes" ? FileKind::Changes
                                        : FileKind::Project;
    }
    snapshot.projectId = parseId(manifest.string("projectId"), "manifest");
    snapshot.romName = manifest.string("romName");
    snapshot.colorCount = static_cast<uint32_t>(manifest.integer("colorCount", 1, 64));
    const std::string& layout = manifest.string("layout");
    if (layout == "ordinary") {
        snapshot.geometry = render::ProjectGeometry(render::PlaneLayout::Ordinary);
    } else if (layout == "native-extra") {
        snapshot.geometry = render::ProjectGeometry(render::PlaneLayout::NativeExtra);
    } else if (layout == "native-original") {
        snapshot.geometry = render::ProjectGeometry(render::PlaneLayout::NativeOriginal);
    } else {
        throw FormatError("the project has an unknown plane layout " + layout);
    }
    if (manifest.has("license")) {
        const JsonObjectReader license = manifest.object("license");
        const std::string kind = license.string("kind", "unspecified");
        const auto known = std::find(kLicenseNames.begin(), kLicenseNames.end(), kind);
        // A kind of a later version: its terms are not known here, the texts are kept.
        snapshot.license.kind = known != kLicenseNames.end()
                                    ? static_cast<document::LicenseKind>(known -
                                                                         kLicenseNames.begin())
                                    : document::LicenseKind::Custom;
        snapshot.license.customText = license.string("text", "");
        snapshot.license.attribution = license.string("attribution", "");
        snapshot.license.url = license.string("url", "");
    }
    if (manifest.has("colorization")) {
        const JsonObjectReader colorization = manifest.object("colorization");
        document::ColorizationSettings settings;
        settings.scalingAlgorithm =
            static_cast<uint8_t>(colorization.integer("scalingAlgorithm", 0, 255));
        settings.shadowOffset = static_cast<uint8_t>(colorization.integer("shadowOffset", 0, 255));
        for (const JsonValue& item : colorization.optionalArray("scenes")) {
            const JsonObjectReader scene = objectItem(item, "scene");
            document::SceneDefinition definition;
            definition.sceneId = static_cast<uint16_t>(scene.integer("id", 0, UINT16_MAX));
            definition.frameCount =
                static_cast<uint16_t>(scene.integer("frameCount", 0, UINT16_MAX));
            definition.durationMs =
                static_cast<uint16_t>(scene.integer("durationMs", 0, UINT16_MAX));
            definition.interruptable = scene.boolean("interruptable", false);
            definition.immediateStart = scene.boolean("immediateStart", false);
            definition.repeat = static_cast<uint8_t>(scene.integer("repeat", 0, 255));
            definition.frameGroups = static_cast<uint8_t>(scene.integer("frameGroups", 0, 255));
            definition.random = scene.boolean("random", false);
            definition.autoStartSeconds =
                static_cast<uint8_t>(scene.integer("autoStartSeconds", 0, 255));
            definition.options = static_cast<uint8_t>(scene.integer("options", 0, 255));
            definition.name = scene.string("name", "");
            if (scene.has("framesOf")) {
                definition.baseSceneId =
                    static_cast<uint16_t>(scene.integer("framesOf", 0, UINT16_MAX));
            }
            settings.scenes.push_back(definition);
        }
        snapshot.colorizationSettings = std::move(settings);
    }
    for (const JsonValue& item : manifest.optionalArray("unsupported")) {
        const std::string* name = item.stringValue();
        if (!name) {
            throw FormatError("the manifest has a damaged list of unsupported data");
        }
        snapshot.unsupportedPayloads.push_back({*name, {}});
    }
}

}  // namespace

std::map<std::string, uint64_t> readAuthorTimes(const JsonValue* value)
{
    std::map<std::string, uint64_t> times;
    if (!value) {
        return times;
    }
    for (const auto& [author, ms] : value->members()) {
        const std::optional<int64_t> number = ms.intValue();
        if (!number || *number < 0) {
            throw JsonFormatError("the time of an author is damaged");
        }
        times[author] = static_cast<uint64_t>(*number);
    }
    return times;
}

JsonValue authorTimesJson(const std::map<std::string, uint64_t>& times)
{
    JsonValue authors = JsonValue::object();
    for (const auto& [author, ms] : times) {
        authors.set(author, static_cast<int64_t>(ms));
    }
    return authors;
}

namespace {

void readEditor(const JsonValue& value, ProjectSnapshot& snapshot)
{
    const JsonObjectReader editor(value, "editor state");
    document::EditorMetadata& tables = snapshot.editorMetadata;
    if (editor.has("editColors")) {
        const std::vector<uint16_t> colors =
            parseColorsHex(editor.string("editColors"), "editor state");
        std::copy_n(colors.begin(), std::min(colors.size(), tables.editColors.size()),
                    tables.editColors.begin());
    }
    const std::vector<JsonValue>& sets = editor.optionalArray("colorSets");
    for (std::size_t set = 0; set < sets.size() && set < document::kMaxColorSets; ++set) {
        const JsonObjectReader item = objectItem(sets[set], "color set");
        tables.activeColorSets[set] = item.boolean("active", false) ? 1 : 0;
        document::setFixedName(tables.colorSetNames, set, document::kColorSetNameBytes,
                               item.string("name", ""));
        const std::vector<uint16_t> colors = parseColorsHex(item.string("colors", ""), "color set");
        std::copy_n(colors.begin(), std::min(colors.size(), document::kColorSetColorCount),
                    tables.colorSets.begin() +
                        static_cast<std::ptrdiff_t>(set * document::kColorSetColorCount));
    }
    tables.currentColorSet = static_cast<uint8_t>(editor.integer("currentColorSet", 0, 255, 0));
    tables.firstDisplayedColorSet =
        static_cast<uint8_t>(editor.integer("firstDisplayedColorSet", 0, 255, 0));
    tables.drawColorMode = static_cast<uint32_t>(editor.integer("drawColorMode", 0, UINT32_MAX, 0));
    tables.drawMode = static_cast<uint8_t>(editor.integer("drawMode", 0, 255, 0));
    tables.maskSelectionMode =
        static_cast<int32_t>(editor.integer("maskSelectionMode", INT32_MIN, INT32_MAX, 0));
    tables.fillMode = static_cast<uint32_t>(editor.integer("fillMode", 0, UINT32_MAX, 0));
    const std::vector<JsonValue>& positions = editor.optionalArray("imagePositions");
    if (positions.size() > document::kMaxImagePositions) {
        throw FormatError("the editor state has too many image positions");
    }
    tables.imagePositionCount = static_cast<uint32_t>(positions.size());
    for (std::size_t p = 0; p < positions.size(); ++p) {
        const JsonObjectReader item = objectItem(positions[p], "image position");
        document::setFixedName(tables.imagePositionNames, p, document::kImagePositionNameBytes,
                               item.string("name", ""));
        const std::vector<JsonValue>& values = item.array("values");
        for (std::size_t v = 0; v < values.size() && v < document::kImagePositionValueCount;
             ++v) {
            const std::optional<int64_t> number = values[v].intValue();
            if (!number || *number < INT32_MIN || *number > INT32_MAX) {
                throw FormatError("an image position is damaged");
            }
            tables.imagePositions[p * document::kImagePositionValueCount + v] =
                static_cast<int32_t>(*number);
        }
    }
    tables.timeElapsedMs = static_cast<uint32_t>(editor.integer("timeElapsedMs", 0, UINT32_MAX, 0));
    tables.authorTimeMs = readAuthorTimes(value.find("authorTimes"));
    if (editor.has("pupPack")) {
        const JsonObjectReader pup = editor.object("pupPack");
        tables.hasPupPack = static_cast<uint32_t>(pup.integer("has", 0, UINT32_MAX, 0));
        tables.pupPackName = pup.string("name", "");
    }
    for (const JsonValue& item : editor.optionalArray("helperLines")) {
        const JsonObjectReader line = objectItem(item, "helper line");
        document::HelperLine helper;
        helper.name = line.string("name", "");
        helper.vertical = line.boolean("vertical", false);
        helper.position = static_cast<uint32_t>(line.integer("position", 0, UINT32_MAX));
        helper.plane = line.string("plane", "sd") == "hd" ? render::Resolution::Hd
                                                           : render::Resolution::Sd;
        helper.visible = line.boolean("visible", true);
        snapshot.helperLines.push_back(std::move(helper));
    }
    for (const JsonValue& item : editor.optionalArray("imageZones")) {
        const JsonObjectReader zone = objectItem(item, "image zone");
        document::SavedImageZone saved;
        saved.name = zone.string("name", "");
        saved.start = readZone(zone.object("start"));
        if (zone.has("end")) {
            saved.end = readZone(zone.object("end"));
        }
        saved.brightness = static_cast<int8_t>(zone.integer("brightness", -128, 127, 0));
        saved.contrast = static_cast<int8_t>(zone.integer("contrast", -128, 127, 0));
        saved.blur = static_cast<uint8_t>(zone.integer("blur", 0, 255, 0));
        snapshot.imageZones.push_back(std::move(saved));
    }
}

void readLightsJson(const JsonValue& value, ProjectSnapshot& snapshot)
{
    const JsonObjectReader lights(value, "speaker lights");
    for (const JsonValue& item : lights.optionalArray("effects")) {
        const JsonObjectReader effect = objectItem(item, "light effect");
        document::LightEffect light;
        light.id = static_cast<uint16_t>(effect.integer("id", 0, UINT16_MAX));
        light.name = effect.string("name", "");
        light.mode = static_cast<uint8_t>(effect.integer("mode", 0, 255));
        light.options = static_cast<uint8_t>(effect.integer("options", 0, 255, 0));
        light.speed = static_cast<uint16_t>(effect.integer("speed", 0, UINT16_MAX));
        light.durationMs = static_cast<uint32_t>(effect.integer("durationMs", 0, UINT32_MAX, 0));
        const std::vector<JsonValue>& colors = effect.array("colors");
        for (std::size_t c = 0; c < colors.size() && c < light.colors.size(); ++c) {
            const std::optional<int64_t> color = colors[c].intValue();
            if (!color || *color < 0 || *color > UINT32_MAX) {
                throw FormatError("a light effect has a damaged color");
            }
            light.colors[c] = static_cast<uint32_t>(*color);
        }
        light.brightness = static_cast<uint8_t>(effect.integer("brightness", 0, 255, 255));
        snapshot.speakerLights.effects.push_back(std::move(light));
    }
    const std::vector<JsonValue>& defaults = lights.optionalArray("defaults");
    for (std::size_t c = 0; c < defaults.size() && c < document::kLightChannels; ++c) {
        snapshot.speakerLights.defaults[c] = readAction(objectItem(defaults[c], "light default"));
    }
}

document::RevisionMaps readRevisions(Cursor& cursor)
{
    document::RevisionMaps maps;
    for (document::RevisionMap& map : maps) {
        const uint64_t count = cursor.integer(4);
        if (count > cursor.remaining() / 32) {
            cursor.fail();
        }
        for (uint64_t i = 0; i < count; ++i) {
            const ComponentId id = cursor.id();
            map[id] = cursor.id();
        }
    }
    return maps;
}

std::vector<std::string> readStrings(const JsonObjectReader& object, std::string_view key)
{
    std::vector<std::string> texts;
    for (const JsonValue& item : object.optionalArray(key)) {
        const std::string* text = item.stringValue();
        if (!text) {
            throw FormatError("the " + object.what() + " has a damaged list");
        }
        texts.push_back(*text);
    }
    return texts;
}

document::TeamState readTeamState(const JsonValue& value, std::string_view common)
{
    const JsonObjectReader state(value, "team state");
    document::TeamState team;
    team.repository = state.string("repository", "");
    team.ownerLogin = state.string("owner", "");
    team.members = readStrings(state, "members");
    team.snapshotId = parseId(state.string("snapshotId"), "team state");
    if (state.has("commonSettingsHash")) {
        team.commonSettingsHash = parseHex(state.string("commonSettingsHash"), "team state");
    }
    for (const JsonValue& item : state.optionalArray("sent")) {
        const JsonObjectReader sent = objectItem(item, "sent package");
        document::SentPackage package;
        package.id = parseId(sent.string("id"), "sent package");
        package.createdMs = sent.integer("created", INT64_MIN, INT64_MAX, 0);
        package.mergedBy = readStrings(sent, "mergedBy");
        team.sent.push_back(std::move(package));
    }
    for (const JsonValue& item : state.optionalArray("mergedPackages")) {
        const std::string* text = item.stringValue();
        if (!text) {
            throw FormatError("the team state has a damaged list of merged packages");
        }
        team.mergedPackages.push_back(parseId(*text, "team state"));
    }
    Cursor cursor(common, "the team's common state");
    team.common = readRevisions(cursor);
    const uint64_t order = cursor.integer(4);
    if (order > cursor.remaining() / 16) {
        cursor.fail();
    }
    for (uint64_t i = 0; i < order; ++i) {
        team.commonOrder.push_back(cursor.id());
    }
    const uint64_t sent = cursor.integer(4);
    if (sent != team.sent.size()) {
        cursor.fail();
    }
    for (document::SentPackage& package : team.sent) {
        package.components = readRevisions(cursor);
    }
    return team;
}

}  // namespace

bool hasProjectFileExtension(const std::string& path)
{
    constexpr std::string_view extension = ".cdmd";
    if (path.size() < extension.size()) {
        return false;
    }
    return std::equal(extension.begin(), extension.end(), path.end() - extension.size(),
                      [](char expected, char actual) {
                          return expected == std::tolower(static_cast<unsigned char>(actual));
                      });
}

void forEachProjectEntry(
    const ProjectSnapshot& snapshot,
    const ProjectFileOptions& options,
    const std::function<void(const std::string& name, const std::string& bytes)>& entry,
    std::vector<document::StampedMeta>* stamped)
{
    using document::ComponentKind;
    if (snapshot.projectId.isNull()) {
        throw std::invalid_argument("the snapshot has no project ID");
    }
    const References references = referencesOf(snapshot);
    entry("manifest.json", writeJson(manifestJson(snapshot, options)));
    entry("order.json", writeJson(orderJson(snapshot, references)));
    entry("editor.json", writeJson(editorJson(snapshot)));
    entry("lights.json", writeJson(lightsJson(snapshot.speakerLights)));
    if (!snapshot.palettes.empty()) {
        entry("palettes.json", writeJson(palettesJson(snapshot, options, stamped)));
    }
    if (snapshot.team) {
        entry("team/state.json", writeJson(teamStateJson(*snapshot.team)));
        entry("team/common.bin", teamCommonData(*snapshot.team));
    }
    for (const auto& [name, bytes] : options.extraEntries) {
        entry(name, bytes);
    }
    for (std::size_t i = 0; i < snapshot.unsupportedPayloads.size(); ++i) {
        const std::vector<uint8_t>& bytes = snapshot.unsupportedPayloads[i].bytes;
        entry("unsupported/" + std::to_string(i) + ".bin", std::string(bytes.begin(), bytes.end()));
    }
    for (std::size_t i = 0; i < snapshot.comparisonMasks.size(); ++i) {
        entry(entryName("masks", references.masks[i]),
              componentEntry(snapshot.comparisonMaskMeta[i], ComponentKind::ComparisonMask,
                             maskContent(snapshot, i), options, stamped));
    }
    for (std::size_t i = 0; i < snapshot.backgrounds.size(); ++i) {
        entry(entryName("backgrounds", references.backgrounds[i]),
              componentEntry(snapshot.backgrounds[i].meta, ComponentKind::Background,
                             backgroundContent(snapshot.backgrounds[i]), options, stamped));
    }
    for (std::size_t i = 0; i < snapshot.sprites.size(); ++i) {
        entry(entryName("sprites", references.sprites[i]),
              componentEntry(snapshot.sprites[i].meta, ComponentKind::Sprite,
                             spriteContent(snapshot, i, references), options, stamped));
    }
    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        entry(entryName("frames", references.frames[i]),
              componentEntry(snapshot.frames[i].meta, ComponentKind::Frame,
                             frameContent(snapshot, i, references), options, stamped));
    }
}

std::optional<ProjectSnapshot> readProjectEntries(
    const std::function<std::optional<std::string>(const std::string& name)>& read,
    std::string& errorMessage,
    legacy::LegacyIoProgress* progress,
    FileKind* kind)
{
    try {
        ProjectSnapshot snapshot;
        readManifest(readJsonEntry(read, "manifest.json", true), snapshot, kind);
        const JsonValue orderValue = readJsonEntry(read, "order.json", true);
        const JsonObjectReader order(orderValue, "frame order");
        const std::vector<ComponentId> frameIds = readIds(order, "frames");
        const std::vector<ComponentId> maskIds = readIds(order, "comparisonMasks");
        const std::vector<ComponentId> backgroundIds = readIds(order, "backgrounds");
        const std::vector<ComponentId> spriteIds = readIds(order, "sprites");
        if (maskIds.size() > document::kMaxComparisonMasks) {
            throw FormatError("the project has more comparison masks than possible");
        }
        ReadIndexes indexes;
        indexes.frames = indexOf(frameIds, "frame");
        indexes.masks = indexOf(maskIds, "comparison mask");
        indexes.backgrounds = indexOf(backgroundIds, "background");
        indexes.sprites = indexOf(spriteIds, "sprite");

        if (progress) {
            progress->setStage(legacy::LegacyIoStage::ReadingProjectFile);
            progress->done.store(0);
            progress->total.store(frameIds.size() + maskIds.size() + backgroundIds.size() +
                                  spriteIds.size());
        }
        const auto component = [&](const char* folder, const ComponentId& id,
                                   const char* kind) {
            if (progress && progress->cancel.load()) {
                throw Cancelled{};
            }
            const std::string name = entryName(folder, id);
            std::optional<std::string> bytes = read(name);
            if (!bytes) {
                throw FormatError("the project file lacks the " + std::string(kind) + " " +
                                  id.toString());
            }
            if (progress) {
                progress->done.fetch_add(1);
            }
            return std::move(*bytes);
        };
        const auto checkId = [](const ComponentMeta& meta, const ComponentId& id,
                                const std::string& what) {
            if (meta.id != id) {
                throw FormatError(what + " has another ID than its entry");
            }
        };

        document::EditorMetadata& tables = snapshot.editorMetadata;
        for (std::size_t m = 0; m < maskIds.size(); ++m) {
            const std::string bytes = component("masks", maskIds[m], "comparison mask");
            const Fields fields(bytes, "comparison mask " + std::to_string(m));
            ComponentMeta meta = readMeta(fields);
            checkId(meta, maskIds[m], fields.what());
            document::setFixedName(tables.maskNames, m, document::kMaskNameBytes,
                                   std::string(fields.get("NAME")));
            snapshot.comparisonMasks.push_back(readIndexed(fields, "PICT"));
            snapshot.comparisonMaskMeta.push_back(std::move(meta));
        }
        for (std::size_t b = 0; b < backgroundIds.size(); ++b) {
            const std::string bytes = component("backgrounds", backgroundIds[b], "background");
            const Fields fields(bytes, "background " + std::to_string(b));
            document::BackgroundRecord background;
            background.meta = readMeta(fields);
            checkId(background.meta, backgroundIds[b], fields.what());
            background.colorizedSd = readRgb565(fields, "COLS");
            background.colorizedHd = readOptionalRgb565(fields, "COLH");
            background.inactiveHd = readOptionalRgb565(fields, "INHD");
            snapshot.backgrounds.push_back(std::move(background));
        }
        for (std::size_t s = 0; s < spriteIds.size(); ++s) {
            const std::string bytes = component("sprites", spriteIds[s], "sprite");
            const Fields fields(bytes, "sprite " + std::to_string(s));
            document::SpriteRecord sprite = readSprite(fields, indexes, s, tables);
            checkId(sprite.meta, spriteIds[s], fields.what());
            snapshot.sprites.push_back(std::move(sprite));
        }
        std::string importedPalettes;
        bool palettesEnded = false;
        bool oneTriggerPerFrame = false;
        snapshot.frames.reserve(frameIds.size());
        for (std::size_t f = 0; f < frameIds.size(); ++f) {
            const std::string bytes = component("frames", frameIds[f], "frame");
            const Fields fields(bytes, "frame " + std::to_string(f));
            std::string palette;
            oneTriggerPerFrame = oneTriggerPerFrame || !fields.has("TRGS");
            snapshot.frames.push_back(readFrame(fields, indexes, &palette));
            checkId(snapshot.frames.back().meta, frameIds[f], fields.what());
            if (palette.empty()) {
                palettesEnded = true;
            } else if (palettesEnded) {
                throw FormatError(fields.what() +
                                  " has an imported palette, but a frame before it has none");
            } else {
                importedPalettes += palette;
            }
        }
        tables.importedPalettes.assign(importedPalettes.begin(), importedPalettes.end());

        const std::vector<JsonValue>& sections = order.optionalArray("sections");
        if (sections.size() > document::kMaxSections) {
            throw FormatError("the project has more sections than possible");
        }
        tables.sectionCount = static_cast<uint32_t>(sections.size());
        for (std::size_t s = 0; s < sections.size(); ++s) {
            const JsonObjectReader section = objectItem(sections[s], "section");
            document::setFixedName(tables.sectionNames, s, document::kSectionNameBytes,
                                   section.string("name", ""));
            tables.sectionFirstFrames[s] =
                section.has("first")
                    ? static_cast<uint32_t>(resolve(indexes.frames,
                                                    parseId(section.string("first"), "section"),
                                                    "a section"))
                    : static_cast<uint32_t>(section.integer("firstIndex", 0, UINT32_MAX));
        }

        const JsonValue palettesValue = readJsonEntry(read, "palettes.json", false);
        const JsonObjectReader palettes(palettesValue, "palettes");
        for (const JsonValue& item : palettes.optionalArray("palettes")) {
            const JsonObjectReader palette = objectItem(item, "palette");
            document::PaletteRecord record;
            record.meta = readMetaJson(palette.object("meta"));
            record.name = palette.string("name", "");
            record.colors = parseColorsHex(palette.string("colors"), "palette");
            snapshot.palettes.push_back(std::move(record));
        }
        if (!snapshot.palettes.empty() && snapshot.palettes.size() != document::kPaletteCount) {
            throw FormatError("the project has " + std::to_string(snapshot.palettes.size()) +
                              " palettes instead of " + std::to_string(document::kPaletteCount));
        }
        for (std::size_t p = 0; p < snapshot.palettes.size(); ++p) {
            document::setFixedName(tables.paletteNames, p, document::kPaletteNameBytes,
                                   snapshot.palettes[p].name);
        }
        readLightsJson(readJsonEntry(read, "lights.json", false), snapshot);
        readEditor(readJsonEntry(read, "editor.json", false), snapshot);
        if (oneTriggerPerFrame) {
            document::linkPupTriggerScenes(snapshot);
        }
        if (std::optional<std::string> common = read("team/common.bin")) {
            snapshot.team = readTeamState(readJsonEntry(read, "team/state.json", true), *common);
        }
        for (std::size_t i = 0; i < snapshot.unsupportedPayloads.size(); ++i) {
            const std::string name = "unsupported/" + std::to_string(i) + ".bin";
            const std::optional<std::string> bytes = read(name);
            if (!bytes) {
                throw FormatError("the project file has no " + name);
            }
            snapshot.unsupportedPayloads[i].bytes.assign(bytes->begin(), bytes->end());
        }

        if (!document::isValidProjectSnapshot(snapshot, &errorMessage)) {
            return std::nullopt;
        }
        return snapshot;
    } catch (const FormatError& error) {
        errorMessage = error.what();
    } catch (const JsonFormatError& error) {
        errorMessage = error.what();
    } catch (const Cancelled&) {
        errorMessage = legacy::kLegacyIoCancelledMessage;
    }
    return std::nullopt;
}

namespace {

// The common versions of a team project's components that changed since (team/base): kept
// from the file before, or taken from its entry there while that still is the common version.
bool writeCommonVersions(ZipWriter& zip,
                         const ProjectSnapshot& snapshot,
                         const std::vector<document::StampedMeta>& stamps,
                         const std::string& sourcePath,
                         int level)
{
    std::error_code error;
    if (!std::filesystem::exists(platform::pathFromUtf8(sourcePath), error)) {
        return true;
    }
    ZipReader source(sourcePath);
    if (!source.isOpen()) {
        return true;
    }
    // The revision each component has in the new file.
    document::RevisionMaps current;
    for (const auto& frame : snapshot.frames) {
        current[0][frame.meta.id] = frame.meta.revision;
    }
    for (const ComponentMeta& meta : snapshot.comparisonMaskMeta) {
        current[1][meta.id] = meta.revision;
    }
    for (const auto& background : snapshot.backgrounds) {
        current[2][background.meta.id] = background.meta.revision;
    }
    for (const auto& sprite : snapshot.sprites) {
        current[3][sprite.meta.id] = sprite.meta.revision;
    }
    for (const document::StampedMeta& stamp : stamps) {
        current[document::kindIndex(stamp.kind)][stamp.meta.id] = stamp.meta.revision;
    }
    for (const document::ComponentKind kind : kKinds) {
        const char* folder = folderOf(kind);
        if (!folder) {
            continue;
        }
        const document::RevisionMap& now = current[document::kindIndex(kind)];
        for (const auto& [id, commonRevision] : snapshot.team->common[document::kindIndex(kind)]) {
            const auto found = now.find(id);
            if (found != now.end() && found->second == commonRevision) {
                continue;
            }
            const std::string base = "team/base/" + std::string(folder) + "/" + id.toString() +
                                     ".bin";
            std::optional<std::string> bytes;
            if (source.contains(base)) {
                bytes = source.read(base);
            } else if (const std::string own = entryName(folder, id); source.contains(own)) {
                bytes = source.read(own);
                if (bytes && revisionOfEntry(*bytes) != commonRevision) {
                    bytes.reset();
                }
            }
            if (bytes && !zip.add(base, *bytes, level)) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

legacy::LegacySaveResult saveProjectFile(const std::string& path,
                                         const ProjectSnapshot& snapshot,
                                         const ProjectFileOptions& options,
                                         legacy::LegacyIoProgress* progress,
                                         std::vector<document::StampedMeta>* stamped)
{
    std::vector<document::StampedMeta> stamps;
    const std::string temporaryPath = path + ".saving";
    std::error_code ignored;
    const auto removeTemporary = [&]() {
        std::filesystem::remove(platform::pathFromUtf8(temporaryPath), ignored);
    };
    if (progress) {
        progress->setStage(legacy::LegacyIoStage::WritingProjectFile);
        progress->done.store(0);
        // The four JSON entries every file has, the palettes, the unsupported data and the
        // components.
        progress->total.store(4 + (snapshot.palettes.empty() ? 0 : 1) + (snapshot.team ? 2 : 0) +
                              options.extraEntries.size() +
                              snapshot.unsupportedPayloads.size() + snapshot.frames.size() +
                              snapshot.comparisonMasks.size() + snapshot.backgrounds.size() +
                              snapshot.sprites.size());
    }
    {
        ZipWriter zip(temporaryPath);
        if (!zip.isOpen()) {
            return legacy::makeSaveFailure(zip.error());
        }
        bool written = true;
        bool cancelled = false;
        try {
            forEachProjectEntry(snapshot, options,
                                [&](const std::string& name, const std::string& bytes) {
                                    if (!written || cancelled) {
                                        return;
                                    }
                                    if (progress && progress->cancel.load()) {
                                        cancelled = true;
                                        return;
                                    }
                                    written = zip.add(name, bytes, options.compressionLevel);
                                    if (progress) {
                                        progress->done.fetch_add(1);
                                    }
                                },
                                &stamps);
        } catch (const std::invalid_argument& error) {
            removeTemporary();
            return legacy::makeSaveFailure(error.what());
        }
        if (written && !cancelled && snapshot.team) {
            written = writeCommonVersions(zip, snapshot, stamps,
                                          options.baseSourcePath.empty() ? path
                                                                         : options.baseSourcePath,
                                          options.compressionLevel);
        }
        if (cancelled) {
            (void)zip.finish();
            removeTemporary();
            return legacy::makeSaveCancelled();
        }
        if (!written || !zip.finish()) {
            const std::string message = zip.error();
            removeTemporary();
            return legacy::makeSaveFailure(message);
        }
    }
    if (progress) {
        progress->setStage(legacy::LegacyIoStage::Replacing);
    }
    std::error_code error;
    std::filesystem::rename(platform::pathFromUtf8(temporaryPath), platform::pathFromUtf8(path),
                            error);
    if (error) {
        removeTemporary();
        return legacy::makeSaveFailure("could not replace the project file: " + error.message());
    }
    if (stamped) {
        *stamped = std::move(stamps);
    }
    return legacy::makeSaveSuccess();
}

namespace {

KindLoadResult loadArchive(const std::string& path,
                           legacy::LegacyIoProgress* progress,
                           FileKind expected,
                           const std::vector<std::string>& extraEntryNames)
{
    KindLoadResult result;
    ZipReader zip(path);
    if (!zip.isOpen()) {
        result.load = legacy::makeLoadFailure(zip.error());
        return result;
    }
    std::string errorMessage;
    FileKind kind = FileKind::Project;
    const auto read = [&zip](const std::string& name) -> std::optional<std::string> {
        if (!zip.contains(name)) {
            return std::nullopt;
        }
        std::optional<std::string> bytes = zip.read(name);
        if (!bytes) {
            throw FormatError(zip.error());
        }
        return bytes;
    };
    std::optional<ProjectSnapshot> snapshot;
    try {
        snapshot = readProjectEntries(read, errorMessage, progress, &kind);
        if (snapshot) {
            for (const std::string& name : extraEntryNames) {
                result.extraEntries.push_back(read(name));
            }
        }
    } catch (const FormatError& error) {
        snapshot.reset();
        errorMessage = error.what();
    }
    if (!snapshot) {
        result.load = errorMessage == legacy::kLegacyIoCancelledMessage
                          ? legacy::makeLoadCancelled()
                          : legacy::makeLoadFailure(errorMessage);
        return result;
    }
    if (kind != expected) {
        const char* names[] = {"a project", "a package", "a team member's changes"};
        result.load = legacy::makeLoadFailure(
            std::string("the file holds ") + names[static_cast<int>(kind)] + ", not " +
            names[static_cast<int>(expected)] +
            (kind == FileKind::Package ? ": import it into a project instead" : ""));
        return result;
    }
    result.load = legacy::makeLoadSuccess(std::move(*snapshot));
    return result;
}

}  // namespace

legacy::LegacyLoadResult loadProjectFile(const std::string& path,
                                         legacy::LegacyIoProgress* progress)
{
    return loadArchive(path, progress, FileKind::Project, {}).load;
}

legacy::LegacyLoadResult loadPackageFile(const std::string& path,
                                         legacy::LegacyIoProgress* progress)
{
    return loadArchive(path, progress, FileKind::Package, {}).load;
}

KindLoadResult loadFileOfKind(const std::string& path,
                              FileKind kind,
                              const std::vector<std::string>& extraEntryNames,
                              legacy::LegacyIoProgress* progress)
{
    return loadArchive(path, progress, kind, extraEntryNames);
}

std::string settingsContent(const ProjectSnapshot& snapshot)
{
    JsonValue root = JsonValue::object();
    JsonValue license = JsonValue::object();
    license.set("kind", kLicenseNames[static_cast<std::size_t>(snapshot.license.kind)]);
    license.set("text", snapshot.license.customText);
    license.set("attribution", snapshot.license.attribution);
    license.set("url", snapshot.license.url);
    root.set("license", std::move(license));
    // As it always was for a project for libserum 2.9, so that a team project's settings stay
    // the same.
    root.set("libserumTarget", kLibserumTargetName);
    // The manifest's colorization settings, without what names this editor.
    ProjectFileOptions options;
    const JsonValue manifest = manifestJson(snapshot, options);
    if (const JsonValue* colorization = manifest.find("colorization")) {
        root.set("colorization", *colorization);
    }
    root.set("lights", lightsJson(snapshot.speakerLights));
    std::vector<ComponentId> frames;
    for (const auto& frame : snapshot.frames) {
        frames.push_back(frame.meta.id);
    }
    References references;
    references.frames = frames;
    const JsonValue order = orderJson(snapshot, references);
    root.set("sections", *order.find("sections"));
    const JsonValue editor = editorJson(snapshot);
    root.set("helperLines", *editor.find("helperLines"));
    return writeJson(root);
}

bool hasPackageExtension(const std::string& path)
{
    constexpr std::string_view extension = ".cdmdpkg";
    if (path.size() < extension.size()) {
        return false;
    }
    return std::equal(extension.begin(), extension.end(), path.end() - extension.size(),
                      [](char expected, char actual) {
                          return expected == std::tolower(static_cast<unsigned char>(actual));
                      });
}

legacy::LegacyDocumentLoadResult loadProjectDocument(const std::string& path,
                                                     legacy::LegacyIoProgress* progress)
{
    legacy::LegacyDocumentLoadResult result;
    try {
        legacy::LegacyLoadResult loaded = loadProjectFile(path, progress);
        if (!loaded.ok) {
            result.error = std::move(loaded.error);
            result.cancelled = loaded.cancelled;
            return result;
        }
        result.document = document::ProjectDocument::fromSnapshot(std::move(loaded.snapshot));
        result.ok = true;
    } catch (const std::exception& ex) {
        result.document.reset();
        result.error.message = ex.what();
    }
    return result;
}

std::optional<FramePictures> readCommonFramePictures(const std::string& projectPath,
                                                    const ComponentId& frameId,
                                                    const ComponentId& commonRevision)
{
    ZipReader zip(projectPath);
    if (!zip.isOpen()) {
        return std::nullopt;
    }
    for (const std::string& name : {"team/base/frames/" + frameId.toString() + ".bin",
                                    entryName("frames", frameId)}) {
        if (!zip.contains(name)) {
            continue;
        }
        const std::optional<std::string> bytes = zip.read(name);
        if (!bytes || revisionOfEntry(*bytes) != commonRevision) {
            continue;
        }
        try {
            const Fields fields(*bytes, "frame");
            FramePictures pictures;
            pictures.original = readIndexed(fields, "ORIG");
            pictures.colorizedSd = readRgb565(fields, "COLS");
            pictures.colorizedHd = readOptionalRgb565(fields, "COLH");
            return pictures;
        } catch (const FormatError&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

}  // namespace colorizing::project
