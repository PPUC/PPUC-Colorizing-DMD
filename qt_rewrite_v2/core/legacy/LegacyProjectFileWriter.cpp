#include "legacy/LegacyProjectFileWriter.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "legacy/LegacyCrzArchive.h"
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
    for (const std::string* path : {&paths.cromPath, &paths.crpPath, &paths.crzPath}) {
        if (!path->empty()) {
            std::filesystem::remove(temporaryPathFor(*path), ignored);
        }
    }
}

}  // namespace

LegacySaveResult LegacyProjectFileWriter::save(const LegacyProjectPaths& paths,
                                               const document::ProjectSnapshot& snapshot) const
{
    if (paths.cromPath.empty() || paths.crpPath.empty()) {
        return makeSaveFailure("both .cROM and .cRP paths are required");
    }
    // Raw cROM bytes under an archive name would be unreadable for everything that opens it.
    if (hasCrzExtension(paths.cromPath) || hasCrzExtension(paths.crpPath)) {
        return makeSaveFailure("a .cRZ archive cannot be the .cROM or .cRP target");
    }
    if (paths.crzPath == paths.cromPath || paths.crzPath == paths.crpPath) {
        return makeSaveFailure("the .cRZ path must differ from the .cROM and .cRP paths");
    }

    // All files are written completely beside their targets first, so a failed or interrupted
    // save never leaves a truncated project behind. The archive is built from the temporary
    // .cROM, so it holds exactly the bytes that become the .cROM.
    std::string errorMessage;
    bool written =
        writeTemporaryFile(paths.cromPath, ".cROM", writeLegacyCrom, snapshot, errorMessage) &&
        writeTemporaryFile(paths.crpPath, ".cRP", writeLegacyCrp, snapshot, errorMessage);
    if (written && !paths.crzPath.empty()) {
        std::string detail;
        written = writeCrzArchive(temporaryPathFor(paths.crzPath).string(),
                                  crzEntryNameFor(snapshot.romName),
                                  temporaryPathFor(paths.cromPath).string(),
                                  detail);
        if (!written) {
            errorMessage = "could not write .cRZ file: " + paths.crzPath + " (" + detail + ")";
        }
    }
    if (!written) {
        removeTemporaryFiles(paths);
        return makeSaveFailure(errorMessage);
    }

    std::error_code error;
    std::filesystem::rename(temporaryPathFor(paths.cromPath), paths.cromPath, error);
    if (!error) {
        std::filesystem::rename(temporaryPathFor(paths.crpPath), paths.crpPath, error);
    }
    if (!error && !paths.crzPath.empty()) {
        std::filesystem::rename(temporaryPathFor(paths.crzPath), paths.crzPath, error);
    }
    if (error) {
        removeTemporaryFiles(paths);
        return makeSaveFailure("could not replace project files: " + error.message());
    }
    return makeSaveSuccess();
}

}  // namespace colorizing::legacy
