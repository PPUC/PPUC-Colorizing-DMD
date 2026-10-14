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
    return true;
}

uint64_t crpxByteCount(const document::ProjectSnapshot& snapshot)
{
    return kCrpxHeaderBytes + legacyCrpByteCount(snapshot);
}

CrpxProbe probeCrpx(LegacyByteSource& source, std::string& errorMessage)
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
    return CrpxProbe::Crpx;
}

}  // namespace colorizing::legacy
