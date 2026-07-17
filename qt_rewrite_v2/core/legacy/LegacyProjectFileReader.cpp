#include "legacy/LegacyProjectFileReader.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacyCrpOriginals.h"
#include "legacy/LegacySnapshotBuilder.h"

namespace colorizing::legacy {

namespace {

struct FileReadResult {
    std::vector<uint8_t> bytes;
    std::string errorMessage;
    bool ok = false;
};

FileReadResult readFileBytes(const std::string& path, const char* label)
{
    if (path.empty()) {
        return FileReadResult{{}, std::string(label) + " path is empty", false};
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return FileReadResult{{}, "could not open " + std::string(label) + " file: " + path, false};
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    if (!stream.eof() && stream.fail()) {
        return FileReadResult{{}, "could not read " + std::string(label) + " file: " + path, false};
    }
    return FileReadResult{std::move(bytes), "", true};
}

}  // namespace

LegacyLoadResult LegacyProjectFileReader::load(const LegacyProjectPaths& paths) const
{
    const FileReadResult cromFile = readFileBytes(paths.cromPath, ".cROM");
    if (!cromFile.ok) {
        return makeLoadFailure(cromFile.errorMessage);
    }

    const LegacyCromFrameParseResult cromFrames =
        parseModernLegacyCromFrames(cromFile.bytes);
    if (!cromFrames.ok) {
        return makeLoadFailure(cromFrames.errorMessage);
    }

    const FileReadResult crpFile = readFileBytes(paths.crpPath, ".cRP");
    if (!crpFile.ok) {
        return makeLoadFailure(crpFile.errorMessage);
    }

    const LegacyCrpOriginalParseResult crpOriginals =
        parseLegacyCrpOriginalFrames(crpFile.bytes, cromFrames.frameData.metadata);
    if (!crpOriginals.ok) {
        return makeLoadFailure(crpOriginals.errorMessage);
    }

    const LegacySnapshotBuildResult snapshot =
        buildProjectSnapshot(cromFrames.frameData, crpOriginals.originalData);
    if (!snapshot.ok) {
        return makeLoadFailure(snapshot.errorMessage);
    }

    return makeLoadSuccess(snapshot.snapshot);
}

}  // namespace colorizing::legacy

