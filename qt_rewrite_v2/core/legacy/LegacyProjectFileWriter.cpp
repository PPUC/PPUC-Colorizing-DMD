#include "legacy/LegacyProjectFileWriter.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::legacy {

namespace {

using WriteFunction = bool (*)(std::ostream&,
                               const document::ProjectSnapshot&,
                               std::string&,
                               LegacySectionOffsets*);

std::filesystem::path temporaryPathFor(const std::string& path)
{
    return std::filesystem::path(path + ".saving");
}

bool writeTemporaryFile(const std::string& path,
                        const char* label,
                        WriteFunction write,
                        const document::ProjectSnapshot& snapshot,
                        std::string& errorMessage)
{
    const std::filesystem::path temporaryPath = temporaryPathFor(path);
    std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!stream) {
        errorMessage = "could not open " + std::string(label) + " file for writing: " + path;
        return false;
    }
    if (!write(stream, snapshot, errorMessage, nullptr)) {
        return false;
    }
    stream.close();
    if (!stream) {
        errorMessage = "could not write " + std::string(label) + " file: " + path;
        return false;
    }
    return true;
}

void removeTemporaryFiles(const LegacyProjectPaths& paths)
{
    std::error_code ignored;
    std::filesystem::remove(temporaryPathFor(paths.cromPath), ignored);
    std::filesystem::remove(temporaryPathFor(paths.crpPath), ignored);
}

}  // namespace

LegacySaveResult LegacyProjectFileWriter::save(const LegacyProjectPaths& paths,
                                               const document::ProjectSnapshot& snapshot) const
{
    if (paths.cromPath.empty() || paths.crpPath.empty()) {
        return makeSaveFailure("both .cROM and .cRP paths are required");
    }

    // Both files are written completely beside their targets first, so a failed or interrupted
    // save never leaves a truncated project behind.
    std::string errorMessage;
    if (!writeTemporaryFile(paths.cromPath, ".cROM", writeLegacyCrom, snapshot, errorMessage) ||
        !writeTemporaryFile(paths.crpPath, ".cRP", writeLegacyCrp, snapshot, errorMessage)) {
        removeTemporaryFiles(paths);
        return makeSaveFailure(errorMessage);
    }

    std::error_code error;
    std::filesystem::rename(temporaryPathFor(paths.cromPath), paths.cromPath, error);
    if (!error) {
        std::filesystem::rename(temporaryPathFor(paths.crpPath), paths.crpPath, error);
    }
    if (error) {
        removeTemporaryFiles(paths);
        return makeSaveFailure("could not replace project files: " + error.message());
    }
    return makeSaveSuccess();
}

}  // namespace colorizing::legacy
