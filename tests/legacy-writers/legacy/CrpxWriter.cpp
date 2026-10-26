#include "legacy/CrpxWriter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include "legacy/CrpxFile.h"
#include "legacy/CrpxFormat.h"
#include "legacy/LegacyCrpWriter.h"

namespace colorizing::legacy {

namespace {

using namespace crpx;

void appendChunk(std::string& chunks, const std::array<char, 4>& tag, const std::string& data)
{
    chunks.append(tag.data(), tag.size());
    appendLittleEndian(chunks, 0, 4);
    appendLittleEndian(chunks, data.size(), 8);
    chunks += data;
}

// The chunks after the cRP part, each with its header.
std::string chunkBytes(const document::ProjectSnapshot& snapshot)
{
    std::string chunks;
    const document::EditorMetadata& metadata = snapshot.editorMetadata;
    if (metadata.hasPupPack != 0 || !metadata.pupPackName.empty()) {
        std::string data;
        appendLittleEndian(data, metadata.hasPupPack, 4);
        appendLittleEndian(data, metadata.pupPackName.size(), 4);
        data += metadata.pupPackName;
        appendChunk(chunks, kPupPackTag, data);
    }
    if (!snapshot.helperLines.empty()) {
        std::string data;
        appendLittleEndian(data, snapshot.helperLines.size(), 4);
        for (const document::HelperLine& line : snapshot.helperLines) {
            data.push_back(line.vertical ? 1 : 0);
            data.push_back(line.plane == render::Resolution::Hd ? 1 : 0);
            data.push_back(line.visible ? 1 : 0);
            data.push_back(0);
            appendLittleEndian(data, line.position, 4);
            appendLittleEndian(data, line.name.size(), 4);
            data += line.name;
        }
        appendChunk(chunks, kHelperLinesTag, data);
    }
    if (!snapshot.imageZones.empty()) {
        // Per zone: flags (1: an end zone), brightness, contrast, blur, the start and the end
        // zone (four u32 each, millionths; the end all 0 without one), the name.
        std::string data;
        appendLittleEndian(data, snapshot.imageZones.size(), 4);
        for (const document::SavedImageZone& zone : snapshot.imageZones) {
            data.push_back(zone.end ? 1 : 0);
            data.push_back(static_cast<char>(zone.brightness));
            data.push_back(static_cast<char>(zone.contrast));
            data.push_back(static_cast<char>(zone.blur));
            const document::PictureZone none{0, 0, 0, 0};
            for (const document::PictureZone& part : {zone.start, zone.end.value_or(none)}) {
                appendLittleEndian(data, part.x, 4);
                appendLittleEndian(data, part.y, 4);
                appendLittleEndian(data, part.width, 4);
                appendLittleEndian(data, part.height, 4);
            }
            appendLittleEndian(data, zone.name.size(), 4);
            data += zone.name;
        }
        appendChunk(chunks, kImageZonesTag, data);
    }
    if (snapshot.colorizationSettings) {
        const document::ColorizationSettings& settings = *snapshot.colorizationSettings;
        std::string data;
        data.push_back(static_cast<char>(settings.scalingAlgorithm));
        data.push_back(static_cast<char>(settings.shadowOffset));
        appendLittleEndian(data, 0, 2);
        appendLittleEndian(data, settings.scenes.size(), 4);
        for (const document::SceneDefinition& scene : settings.scenes) {
            appendLittleEndian(data, scene.sceneId, 2);
            appendLittleEndian(data, scene.frameCount, 2);
            appendLittleEndian(data, scene.durationMs, 2);
            data.push_back(scene.interruptable ? 1 : 0);
            data.push_back(scene.immediateStart ? 1 : 0);
            data.push_back(static_cast<char>(scene.repeat));
            data.push_back(static_cast<char>(scene.frameGroups));
            data.push_back(scene.random ? 1 : 0);
            data.push_back(static_cast<char>(scene.autoStartSeconds));
            data.push_back(static_cast<char>(scene.options));
            appendLittleEndian(data, 0, 3);
        }
        std::size_t sceneFrames = 0;
        for (const document::ProjectFrameSnapshot& frame : snapshot.frames) {
            sceneFrames += frame.scene ? 1 : 0;
        }
        appendLittleEndian(data, sceneFrames, 4);
        for (std::size_t index = 0; index < snapshot.frames.size(); ++index) {
            const std::optional<document::SceneFrameRef>& scene = snapshot.frames[index].scene;
            if (!scene) {
                continue;
            }
            appendLittleEndian(data, index, 4);
            appendLittleEndian(data, scene->sceneId, 2);
            data.push_back(static_cast<char>(scene->group));
            data.push_back(0);
            appendLittleEndian(data, scene->frameIndex, 2);
            appendLittleEndian(data, 0, 2);
        }
        appendChunk(chunks, kScenesTag, data);
    }
    // The frames' tags: the names once, then per tagged frame the numbers of its names.
    std::vector<std::string> names;
    for (const document::ProjectFrameSnapshot& frame : snapshot.frames) {
        names.insert(names.end(), frame.tags.begin(), frame.tags.end());
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    if (!names.empty()) {
        std::string data;
        appendLittleEndian(data, names.size(), 4);
        for (const std::string& name : names) {
            appendLittleEndian(data, name.size(), 4);
            data += name;
        }
        std::size_t tagged = 0;
        for (const document::ProjectFrameSnapshot& frame : snapshot.frames) {
            tagged += frame.tags.empty() ? 0 : 1;
        }
        appendLittleEndian(data, tagged, 4);
        for (std::size_t index = 0; index < snapshot.frames.size(); ++index) {
            const std::vector<std::string>& tags = snapshot.frames[index].tags;
            if (tags.empty()) {
                continue;
            }
            appendLittleEndian(data, index, 4);
            appendLittleEndian(data, tags.size(), 4);
            for (const std::string& tag : tags) {
                appendLittleEndian(
                    data,
                    static_cast<std::size_t>(
                        std::lower_bound(names.begin(), names.end(), tag) - names.begin()),
                    4);
            }
        }
        appendChunk(chunks, kFrameTagsTag, data);
    }
    // The speaker lights.
    {
        const document::SpeakerLightSettings& lights = snapshot.speakerLights;
        std::size_t framesWithLights = 0;
        for (const document::ProjectFrameSnapshot& frame : snapshot.frames) {
            framesWithLights += document::changesNothing(frame.lights) ? 0 : 1;
        }
        if (!lights.empty() || framesWithLights > 0) {
            std::string data;
            const auto putAction = [&data](const document::LightAction& action) {
                data.push_back(static_cast<char>(action.kind));
                data.push_back(action.automatic ? 1 : 0);
                appendLittleEndian(data, action.effectId, 2);
                appendLittleEndian(data, action.color, 4);
            };
            appendLittleEndian(data, lights.effects.size(), 4);
            for (const document::LightEffect& effect : lights.effects) {
                appendLittleEndian(data, effect.id, 2);
                data.push_back(static_cast<char>(effect.mode));
                data.push_back(static_cast<char>(effect.options));
                appendLittleEndian(data, effect.speed, 2);
                appendLittleEndian(data, 0, 2);
                appendLittleEndian(data, effect.durationMs, 4);
                for (uint32_t color : effect.colors) {
                    appendLittleEndian(data, color, 4);
                }
                data.push_back(static_cast<char>(effect.brightness));
                appendLittleEndian(data, 0, 3);
                appendLittleEndian(data, effect.name.size(), 4);
                data += effect.name;
            }
            for (const document::LightAction& action : lights.defaults) {
                putAction(action);
            }
            appendLittleEndian(data, framesWithLights, 4);
            for (std::size_t index = 0; index < snapshot.frames.size(); ++index) {
                if (document::changesNothing(snapshot.frames[index].lights)) {
                    continue;
                }
                appendLittleEndian(data, index, 4);
                for (const document::LightAction& action : snapshot.frames[index].lights) {
                    putAction(action);
                }
            }
            appendChunk(chunks, kLightsTag, data);
        }
    }
    // The sprite table entries the cRP part has no room for.
    if (snapshot.sprites.size() > document::kCrpSpriteEntries) {
        document::EditorMetadata tables = metadata;
        document::ensureSpriteTableEntries(tables, snapshot.sprites.size());
        const std::size_t first = document::kCrpSpriteEntries;
        const std::size_t count = snapshot.sprites.size() - first;
        std::string data;
        appendLittleEndian(data, first, 4);
        appendLittleEndian(data, count, 4);
        for (std::size_t sprite = first; sprite < snapshot.sprites.size(); ++sprite) {
            document::setFixedName(tables.spriteNames,
                                   sprite,
                                   document::kSpriteNameBytes,
                                   snapshot.sprites[sprite].name);
            data.append(reinterpret_cast<const char*>(tables.spriteNames.data()) +
                            sprite * document::kSpriteNameBytes,
                        document::kSpriteNameBytes);
            appendLittleEndian(data, tables.spriteSourceFrames[sprite], 4);
            for (std::size_t i = 0; i < 4; ++i) {
                appendLittleEndian(data, tables.spriteSourceRects[sprite * 4 + i], 2);
            }
            for (std::size_t i = 0; i < 2; ++i) {
                appendLittleEndian(data, tables.spriteSourceMirrors[sprite * 2 + i], 4);
            }
        }
        appendChunk(chunks, kMoreSpritesTag, data);
    }
    // The dynamic sets that color the SD picture only.
    {
        std::string data;
        std::size_t count = 0;
        for (std::size_t frame = 0; frame < snapshot.frames.size(); ++frame) {
            if (snapshot.frames[frame].originalOnlyDynamicSets != 0) {
                appendLittleEndian(data, frame, 4);
                appendLittleEndian(data, snapshot.frames[frame].originalOnlyDynamicSets, 4);
                ++count;
            }
        }
        if (count > 0) {
            std::string chunk;
            appendLittleEndian(chunk, count, 4);
            appendChunk(chunks, kOriginalOnlySetsTag, chunk + data);
        }
    }
    if (snapshot.license.isSet()) {
        const document::ProjectLicense& license = snapshot.license;
        std::string data;
        appendLittleEndian(data, static_cast<uint8_t>(license.kind), 4);
        for (const std::string* text : {&license.customText, &license.attribution, &license.url}) {
            appendLittleEndian(data, text->size(), 4);
            data += *text;
        }
        appendChunk(chunks, kLicenseTag, data);
    }
    return chunks;
}

}  // namespace

bool writeCrpx(std::ostream& stream,
               const document::ProjectSnapshot& snapshot,
               std::string& errorMessage,
               LegacySectionOffsets* sections,
               LegacyIoProgress* progress)
{
    std::array<uint8_t, kCrpxHeaderBytes> header{};
    std::memcpy(header.data(), kMagic.data(), kMagic.size());
    putLittleEndian(header.data() + 8, kCrpxVersion, 4);
    putLittleEndian(header.data() + 12, kCrpxHeaderBytes, 4);
    const uint64_t crpBytes = legacyCrpByteCount(snapshot);
    putLittleEndian(header.data() + 16, crpBytes, 8);
    stream.write(reinterpret_cast<const char*>(header.data()), header.size());
    if (progress) {
        progress->done.fetch_add(header.size());
    }
    const std::streampos crpStart = stream.tellp();
    if (!writeCrpxCrpPart(stream, snapshot, errorMessage, sections, progress)) {
        return false;
    }
    // The header announces the cRP part's size before it is written; a reader relies on it.
    if (crpStart != std::streampos(-1) &&
        static_cast<uint64_t>(stream.tellp() - crpStart) != crpBytes) {
        errorMessage = "the cRP part of the .cRPX has not the size its header says";
        return false;
    }
    const std::string chunks = chunkBytes(snapshot);
    stream.write(chunks.data(), static_cast<std::streamsize>(chunks.size()));
    if (progress) {
        progress->done.fetch_add(chunks.size());
    }
    if (!stream) {
        errorMessage = "the .cRPX could not be written";
        return false;
    }
    return true;
}

uint64_t crpxByteCount(const document::ProjectSnapshot& snapshot)
{
    return kCrpxHeaderBytes + legacyCrpByteCount(snapshot) + chunkBytes(snapshot).size();
}

}  // namespace colorizing::legacy
