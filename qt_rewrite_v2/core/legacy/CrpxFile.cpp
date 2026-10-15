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
constexpr std::size_t kChunkHeaderBytes = 16;

// The chunks after the cRP part, each with its header.
std::string chunkBytes(const document::ProjectSnapshot& snapshot)
{
    std::string chunks;
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
        chunks.append(kHelperLinesTag.data(), kHelperLinesTag.size());
        appendLittleEndian(chunks, 0, 4);
        appendLittleEndian(chunks, data.size(), 8);
        chunks += data;
    }
    return chunks;
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
    if (!writeLegacyCrp(stream, snapshot, errorMessage, sections, progress)) {
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
