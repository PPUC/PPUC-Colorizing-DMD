#include "legacy/CrpxFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

#include "legacy/CrpxFormat.h"
#include "legacy/LegacyByteSource.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::legacy {

namespace {

using namespace crpx;

uint16_t littleEndian16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

uint32_t littleEndian32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

bool parseScenes(LegacyByteSource& source,
                 uint64_t size,
                 document::ProjectSnapshot& snapshot,
                 std::string& errorMessage)
{
    const auto damaged = [&]() {
        errorMessage = "the scenes of the .cRPX are damaged";
        return false;
    };
    uint8_t head[8] = {};
    if (size < 8 || !source.readBytes(head, 8)) {
        return damaged();
    }
    uint64_t left = size - 8;
    document::ColorizationSettings settings;
    settings.scalingAlgorithm = head[0];
    settings.shadowOffset = head[1];
    const uint32_t sceneCount = littleEndian32(head + 4);
    if (left < static_cast<uint64_t>(sceneCount) * kSceneBytes) {
        return damaged();
    }
    for (uint32_t index = 0; index < sceneCount; ++index) {
        uint8_t bytes[kSceneBytes] = {};
        if (!source.readBytes(bytes, kSceneBytes)) {
            return damaged();
        }
        document::SceneDefinition scene;
        scene.sceneId = littleEndian16(bytes);
        scene.frameCount = littleEndian16(bytes + 2);
        scene.durationMs = littleEndian16(bytes + 4);
        scene.interruptable = bytes[6] != 0;
        scene.immediateStart = bytes[7] != 0;
        scene.repeat = bytes[8];
        scene.frameGroups = bytes[9];
        scene.random = bytes[10] != 0;
        scene.autoStartSeconds = bytes[11];
        scene.options = bytes[12];
        settings.scenes.push_back(scene);
    }
    left -= static_cast<uint64_t>(sceneCount) * kSceneBytes;
    uint32_t frameCount = 0;
    if (left < 4 || !source.readU32(frameCount)) {
        return damaged();
    }
    left -= 4;
    if (left < static_cast<uint64_t>(frameCount) * kSceneFrameBytes) {
        return damaged();
    }
    for (uint32_t index = 0; index < frameCount; ++index) {
        uint8_t bytes[kSceneFrameBytes] = {};
        if (!source.readBytes(bytes, kSceneFrameBytes)) {
            return damaged();
        }
        const uint32_t frame = littleEndian32(bytes);
        if (frame >= snapshot.frames.size()) {
            return damaged();
        }
        snapshot.frames[frame].scene =
            document::SceneFrameRef{littleEndian16(bytes + 4), bytes[6], littleEndian16(bytes + 8)};
    }
    left -= static_cast<uint64_t>(frameCount) * kSceneFrameBytes;
    snapshot.colorizationSettings = std::move(settings);
    // Room a later version uses for more is skipped.
    return source.skip(static_cast<std::size_t>(left)) || damaged();
}

bool parseFrameTags(LegacyByteSource& source,
                    uint64_t size,
                    document::ProjectSnapshot& snapshot,
                    std::string& errorMessage)
{
    const auto damaged = [&]() {
        errorMessage = "the frame tags of the .cRPX are damaged";
        return false;
    };
    uint64_t left = size;
    const auto readCount = [&](uint32_t& value) {
        if (left < 4 || !source.readU32(value)) {
            return false;
        }
        left -= 4;
        return true;
    };
    uint32_t nameCount = 0;
    if (!readCount(nameCount)) {
        return damaged();
    }
    std::vector<std::string> names;
    for (uint32_t index = 0; index < nameCount; ++index) {
        uint32_t bytes = 0;
        if (!readCount(bytes) || left < bytes) {
            return damaged();
        }
        std::string name(bytes, '\0');
        if (bytes > 0 && !source.readBytes(reinterpret_cast<uint8_t*>(name.data()), bytes)) {
            return damaged();
        }
        left -= bytes;
        names.push_back(std::move(name));
    }
    uint32_t taggedCount = 0;
    if (!readCount(taggedCount)) {
        return damaged();
    }
    for (uint32_t tagged = 0; tagged < taggedCount; ++tagged) {
        uint32_t frame = 0;
        uint32_t count = 0;
        if (!readCount(frame) || !readCount(count) || frame >= snapshot.frames.size()) {
            return damaged();
        }
        std::vector<std::string>& tags = snapshot.frames[frame].tags;
        tags.clear();
        for (uint32_t index = 0; index < count; ++index) {
            uint32_t name = 0;
            if (!readCount(name) || name >= names.size()) {
                return damaged();
            }
            tags.push_back(names[name]);
        }
        std::sort(tags.begin(), tags.end());
        tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    }
    // Room a later version uses for more is skipped.
    return source.skip(static_cast<std::size_t>(left)) || damaged();
}


bool parseLights(LegacyByteSource& source,
                 uint64_t size,
                 document::ProjectSnapshot& snapshot,
                 std::string& errorMessage)
{
    const auto damaged = [&]() {
        errorMessage = "the speaker lights of the .cRPX are damaged";
        return false;
    };
    uint64_t left = size;
    const auto read = [&](uint8_t* bytes, std::size_t count) {
        if (left < count || !source.readBytes(bytes, count)) {
            return false;
        }
        left -= count;
        return true;
    };
    const auto readAction = [&](document::LightAction& action) {
        uint8_t bytes[kLightActionBytes] = {};
        if (!read(bytes, kLightActionBytes) ||
            bytes[0] > static_cast<uint8_t>(document::LightActionKind::Off)) {
            return false;
        }
        action.kind = static_cast<document::LightActionKind>(bytes[0]);
        action.automatic = (bytes[1] & 1) != 0;
        action.effectId = littleEndian16(bytes + 2);
        action.color = littleEndian32(bytes + 4);
        return true;
    };
    document::SpeakerLightSettings lights;
    uint8_t countBytes[4] = {};
    if (!read(countBytes, 4)) {
        return damaged();
    }
    const uint32_t effectCount = littleEndian32(countBytes);
    if (left < uint64_t{effectCount} * (kLightEffectBytes + 4)) {
        return damaged();
    }
    for (uint32_t index = 0; index < effectCount; ++index) {
        uint8_t bytes[kLightEffectBytes + 4] = {};
        if (!read(bytes, sizeof(bytes))) {
            return damaged();
        }
        document::LightEffect effect;
        effect.id = littleEndian16(bytes);
        effect.mode = bytes[2];
        effect.options = bytes[3];
        effect.speed = littleEndian16(bytes + 4);
        effect.durationMs = littleEndian32(bytes + 8);
        for (std::size_t i = 0; i < 3; ++i) {
            effect.colors[i] = littleEndian32(bytes + 12 + 4 * i);
        }
        effect.brightness = bytes[24];
        const uint32_t nameBytes = littleEndian32(bytes + kLightEffectBytes);
        if (left < nameBytes) {
            return damaged();
        }
        effect.name.assign(nameBytes, '\0');
        if (nameBytes > 0 &&
            !read(reinterpret_cast<uint8_t*>(effect.name.data()), nameBytes)) {
            return damaged();
        }
        lights.effects.push_back(std::move(effect));
    }
    for (document::LightAction& action : lights.defaults) {
        if (!readAction(action)) {
            return damaged();
        }
    }
    if (!read(countBytes, 4)) {
        return damaged();
    }
    const uint32_t frameCount = littleEndian32(countBytes);
    for (uint32_t index = 0; index < frameCount; ++index) {
        uint8_t frameBytes[4] = {};
        if (!read(frameBytes, 4)) {
            return damaged();
        }
        const uint32_t frame = littleEndian32(frameBytes);
        if (frame >= snapshot.frames.size()) {
            return damaged();
        }
        for (document::LightAction& action : snapshot.frames[frame].lights) {
            if (!readAction(action)) {
                return damaged();
            }
        }
    }
    snapshot.speakerLights = std::move(lights);
    // Room a later version uses for more is skipped.
    return source.skip(static_cast<std::size_t>(left)) || damaged();
}

bool parseOriginalOnlySets(LegacyByteSource& source,
                           uint64_t size,
                           document::ProjectSnapshot& snapshot,
                           std::string& errorMessage)
{
    const auto damaged = [&errorMessage]() {
        errorMessage = "the dynamic sets of the .cRPX are damaged";
        return false;
    };
    uint8_t countBytes[4] = {};
    if (size < 4 || !source.readBytes(countBytes, 4)) {
        return damaged();
    }
    const uint32_t count = littleEndian32(countBytes);
    if ((size - 4) / 8 < count) {
        return damaged();
    }
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t bytes[8] = {};
        if (!source.readBytes(bytes, 8)) {
            return damaged();
        }
        const uint32_t frame = littleEndian32(bytes);
        if (frame >= snapshot.frames.size()) {
            return damaged();
        }
        snapshot.frames[frame].originalOnlyDynamicSets = littleEndian32(bytes + 4);
    }
    // Room a later version uses for more is skipped.
    return source.skip(static_cast<std::size_t>(size - 4 - 8 * uint64_t{count})) || damaged();
}

bool parseLicense(LegacyByteSource& source,
                  uint64_t size,
                  document::ProjectLicense& license,
                  std::string& errorMessage)
{
    const auto damaged = [&errorMessage]() {
        errorMessage = "the license of the .cRPX is damaged";
        return false;
    };
    uint8_t kind[4] = {};
    if (size < 4 || !source.readBytes(kind, 4)) {
        return damaged();
    }
    uint64_t left = size - 4;
    document::ProjectLicense read;
    // A kind of a later version: its terms are not known here, the texts are kept.
    read.kind = kind[0] <= document::kLastLicenseKind ? static_cast<document::LicenseKind>(kind[0])
                                                      : document::LicenseKind::Custom;
    for (std::string* text : {&read.customText, &read.attribution, &read.url}) {
        uint32_t length = 0;
        if (left < 4 || !source.readU32(length) || left - 4 < length) {
            return damaged();
        }
        left -= 4 + uint64_t{length};
        text->assign(length, '\0');
        if (length > 0 && !source.readBytes(reinterpret_cast<uint8_t*>(text->data()), length)) {
            return damaged();
        }
    }
    license = std::move(read);
    // Room a later version uses for more is skipped.
    return source.skip(static_cast<std::size_t>(left)) || damaged();
}

bool parseMoreSprites(LegacyByteSource& source,
                      uint64_t size,
                      document::ProjectSnapshot& snapshot,
                      std::string& errorMessage)
{
    const auto damaged = [&]() {
        errorMessage = "the sprites of the .cRPX are damaged";
        return false;
    };
    uint32_t first = 0;
    uint32_t count = 0;
    if (size < 8 || !source.readU32(first) || !source.readU32(count)) {
        return damaged();
    }
    const uint64_t left = size - 8;
    if (first != document::kCrpSpriteEntries || left < uint64_t{count} * kMoreSpriteBytes ||
        uint64_t{first} + count > snapshot.sprites.size()) {
        return damaged();
    }
    document::EditorMetadata& tables = snapshot.editorMetadata;
    document::ensureSpriteTableEntries(tables, uint64_t{first} + count);
    for (uint32_t index = 0; index < count; ++index) {
        const std::size_t sprite = first + index;
        uint8_t bytes[kMoreSpriteBytes] = {};
        if (!source.readBytes(bytes, kMoreSpriteBytes)) {
            return damaged();
        }
        std::copy_n(bytes,
                    document::kSpriteNameBytes,
                    tables.spriteNames.begin() +
                        static_cast<std::ptrdiff_t>(sprite * document::kSpriteNameBytes));
        const uint8_t* values = bytes + document::kSpriteNameBytes;
        tables.spriteSourceFrames[sprite] = littleEndian32(values);
        for (std::size_t i = 0; i < 4; ++i) {
            tables.spriteSourceRects[sprite * 4 + i] = littleEndian16(values + 4 + 2 * i);
        }
        for (std::size_t i = 0; i < 2; ++i) {
            tables.spriteSourceMirrors[sprite * 2 + i] = littleEndian32(values + 12 + 4 * i);
        }
        snapshot.sprites[sprite].name =
            document::fixedName(tables.spriteNames, sprite, document::kSpriteNameBytes);
    }
    // Room a later version uses for more is skipped.
    const uint64_t rest = left - uint64_t{count} * kMoreSpriteBytes;
    return source.skip(static_cast<std::size_t>(rest)) || damaged();
}

bool parseImageZones(LegacyByteSource& source,
                     uint64_t size,
                     std::vector<document::SavedImageZone>& zones,
                     std::string& errorMessage)
{
    const auto damaged = [&]() {
        errorMessage = "the saved image zones of the .cRPX are damaged";
        return false;
    };
    uint32_t count = 0;
    if (size < 4 || !source.readU32(count)) {
        return damaged();
    }
    uint64_t left = size - 4;
    zones.clear();
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t flags[4] = {};
        uint32_t values[8] = {};
        uint32_t nameBytes = 0;
        if (left < 40 || !source.readBytes(flags, 4)) {
            return damaged();
        }
        for (uint32_t& value : values) {
            if (!source.readU32(value)) {
                return damaged();
            }
        }
        if (!source.readU32(nameBytes) || left - 40 < nameBytes) {
            return damaged();
        }
        left -= 40 + nameBytes;
        std::string name(nameBytes, '\0');
        if (nameBytes > 0 &&
            !source.readBytes(reinterpret_cast<uint8_t*>(name.data()), nameBytes)) {
            return damaged();
        }
        document::SavedImageZone zone;
        zone.name = std::move(name);
        zone.brightness = static_cast<int8_t>(flags[1]);
        zone.contrast = static_cast<int8_t>(flags[2]);
        zone.blur = flags[3];
        zone.start = document::PictureZone{values[0], values[1], values[2], values[3]};
        if (flags[0] & 1) {
            zone.end = document::PictureZone{values[4], values[5], values[6], values[7]};
        }
        zones.push_back(std::move(zone));
    }
    // Room a later version uses for more is skipped.
    return source.skip(static_cast<std::size_t>(left)) || damaged();
}

bool parseHelperLines(LegacyByteSource& source,
                      uint64_t size,
                      std::vector<document::HelperLine>& lines,
                      std::string& errorMessage)
{
    const auto damaged = [&]() {
        errorMessage = "the helper lines of the .cRPX are damaged";
        return false;
    };
    uint32_t count = 0;
    if (size < 4 || !source.readU32(count)) {
        return damaged();
    }
    uint64_t left = size - 4;
    lines.clear();
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t flags[4] = {};
        uint32_t position = 0;
        uint32_t nameBytes = 0;
        if (left < 12 || !source.readBytes(flags, 4) || !source.readU32(position) ||
            !source.readU32(nameBytes) || left - 12 < nameBytes) {
            return damaged();
        }
        left -= 12 + nameBytes;
        std::string name(nameBytes, '\0');
        if (nameBytes > 0 &&
            !source.readBytes(reinterpret_cast<uint8_t*>(name.data()), nameBytes)) {
            return damaged();
        }
        document::HelperLine line;
        line.vertical = flags[0] != 0;
        line.plane = flags[1] != 0 ? render::Resolution::Hd : render::Resolution::Sd;
        line.visible = flags[2] != 0;
        line.position = position;
        line.name = std::move(name);
        lines.push_back(std::move(line));
    }
    // Room a later version uses for more is skipped.
    return source.skip(static_cast<std::size_t>(left)) || damaged();
}

bool parsePupPack(LegacyByteSource& source,
                  uint64_t size,
                  document::EditorMetadata& metadata,
                  std::string& errorMessage)
{
    uint32_t hasPack = 0;
    uint32_t nameBytes = 0;
    if (size < 8 || !source.readU32(hasPack) || !source.readU32(nameBytes) ||
        size - 8 < nameBytes) {
        errorMessage = "the PuP pack of the .cRPX is damaged";
        return false;
    }
    std::string name(nameBytes, '\0');
    if ((nameBytes > 0 &&
         !source.readBytes(reinterpret_cast<uint8_t*>(name.data()), nameBytes)) ||
        !source.skip(static_cast<std::size_t>(size - 8 - nameBytes))) {
        errorMessage = "the PuP pack of the .cRPX is damaged";
        return false;
    }
    metadata.hasPupPack = hasPack;
    metadata.pupPackName = std::move(name);
    return true;
}

}  // namespace

bool hasCrpxExtension(const std::string& path)
{
    constexpr std::string_view extension = ".crpx";
    if (path.size() < extension.size()) {
        return false;
    }
    return std::equal(extension.begin(), extension.end(), path.end() - extension.size(),
                      [](char expected, char actual) {
                          return expected ==
                                 std::tolower(static_cast<unsigned char>(actual));
                      });
}

bool readCrpxChunks(LegacyByteSource& source,
                    uint64_t chunksStart,
                    document::ProjectSnapshot& snapshot,
                    std::string& errorMessage)
{
    if (chunksStart > source.size() || !source.skip(chunksStart - source.offset())) {
        errorMessage = "the .cRPX ends before its chunks";
        return false;
    }
    while (source.remaining() > 0) {
        char tag[4] = {};
        uint32_t reserved = 0;
        uint32_t sizeLow = 0;
        uint32_t sizeHigh = 0;
        if (source.remaining() < kChunkHeaderBytes ||
            !source.readBytes(reinterpret_cast<uint8_t*>(tag), 4) || !source.readU32(reserved) ||
            !source.readU32(sizeLow) || !source.readU32(sizeHigh)) {
            errorMessage = "a chunk of the .cRPX is incomplete";
            return false;
        }
        const uint64_t size = (static_cast<uint64_t>(sizeHigh) << 32) | sizeLow;
        if (size > source.remaining()) {
            errorMessage = "a chunk of the .cRPX is longer than the file";
            return false;
        }
        if (std::memcmp(tag, kHelperLinesTag.data(), 4) == 0) {
            if (!parseHelperLines(source, size, snapshot.helperLines, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kScenesTag.data(), 4) == 0) {
            if (!parseScenes(source, size, snapshot, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kImageZonesTag.data(), 4) == 0) {
            if (!parseImageZones(source, size, snapshot.imageZones, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kFrameTagsTag.data(), 4) == 0) {
            if (!parseFrameTags(source, size, snapshot, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kLightsTag.data(), 4) == 0) {
            if (!parseLights(source, size, snapshot, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kOriginalOnlySetsTag.data(), 4) == 0) {
            if (!parseOriginalOnlySets(source, size, snapshot, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kLicenseTag.data(), 4) == 0) {
            if (!parseLicense(source, size, snapshot.license, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kMoreSpritesTag.data(), 4) == 0) {
            if (!parseMoreSprites(source, size, snapshot, errorMessage)) {
                return false;
            }
        } else if (std::memcmp(tag, kPupPackTag.data(), 4) == 0) {
            if (!parsePupPack(source, size, snapshot.editorMetadata, errorMessage)) {
                return false;
            }
        } else if (!source.skip(static_cast<std::size_t>(size))) {
            errorMessage = "a chunk of the .cRPX is incomplete";
            return false;
        }
    }
    return true;
}

CrpxProbe probeCrpx(LegacyByteSource& source, std::string& errorMessage, uint64_t* chunksStart)
{
    std::size_t available = 0;
    const uint8_t* start = source.peek(kMagic.size(), available);
    if (available < kMagic.size() || std::memcmp(start, kMagic.data(), kMagic.size()) != 0) {
        return CrpxProbe::LegacyCrp;
    }
    uint8_t magic[kMagic.size()];
    uint32_t version = 0;
    uint32_t headerBytes = 0;
    uint32_t crpLow = 0;
    uint32_t crpHigh = 0;
    if (!source.readBytes(magic, sizeof(magic)) || !source.readU32(version) ||
        !source.readU32(headerBytes) || !source.readU32(crpLow) || !source.readU32(crpHigh)) {
        errorMessage = "the .cRPX header is incomplete";
        return CrpxProbe::Invalid;
    }
    if (version > kCrpxVersion) {
        errorMessage = "the .cRPX was written by a newer version of the editor (format " +
                       std::to_string(version) + ")";
        return CrpxProbe::Invalid;
    }
    const uint64_t crpBytes = (static_cast<uint64_t>(crpHigh) << 32) | crpLow;
    if (version == 0 || headerBytes < kCrpxHeaderBytes || headerBytes > source.size() ||
        crpBytes > source.size() - headerBytes) {
        errorMessage = "the .cRPX header is damaged";
        return CrpxProbe::Invalid;
    }
    if (!source.skip(headerBytes - kCrpxHeaderBytes)) {
        errorMessage = "the .cRPX header is incomplete";
        return CrpxProbe::Invalid;
    }
    source.limitTo(headerBytes + crpBytes);
    if (chunksStart) {
        *chunksStart = headerBytes + crpBytes;
    }
    return CrpxProbe::Crpx;
}

}  // namespace colorizing::legacy
