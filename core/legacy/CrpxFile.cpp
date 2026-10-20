#include "legacy/CrpxFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

#include "legacy/LegacyByteSource.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::legacy {

namespace {

constexpr std::array<char, 8> kMagic = {'C', 'D', 'M', 'D', 'c', 'R', 'P', 'X'};

void putLittleEndian(uint8_t* out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i) {
        out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
}

void appendLittleEndian(std::string& out, uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

constexpr std::array<char, 4> kHelperLinesTag = {'H', 'L', 'I', 'N'};
constexpr std::array<char, 4> kPupPackTag = {'P', 'U', 'P', 'P'};
constexpr std::array<char, 4> kScenesTag = {'S', 'C', 'E', 'N'};
constexpr std::array<char, 4> kImageZonesTag = {'I', 'Z', 'O', 'N'};
constexpr std::size_t kSceneBytes = 16;
constexpr std::size_t kSceneFrameBytes = 12;
constexpr std::size_t kChunkHeaderBytes = 16;

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
    return chunks;
}

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
