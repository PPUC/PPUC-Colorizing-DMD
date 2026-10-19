#include "legacy/LegacyProjectFileWriter.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "legacy/CrpxFile.h"
#include "legacy/LegacyCrzArchive.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"

namespace colorizing::legacy {

namespace {

using WriteFunction = bool (*)(std::ostream&,
                               const document::ProjectSnapshot&,
                               std::string&,
                               LegacySectionOffsets*,
                               LegacyIoProgress*);

// Paths are UTF-8 (see platform/Utf8Path.h) until a file is opened, renamed or removed.
std::string temporaryPathFor(const std::string& path)
{
    return path + ".saving";
}

bool writeTemporaryFile(const std::string& path,
                        const char* label,
                        WriteFunction write,
                        const document::ProjectSnapshot& snapshot,
                        LegacyIoProgress* progress,
                        std::string& errorMessage)
{
    std::ofstream stream(platform::pathFromUtf8(temporaryPathFor(path)),
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
        errorMessage = "could not open " + std::string(label) + " file for writing: " + path;
        return false;
    }
    if (!write(stream, snapshot, errorMessage, nullptr, progress)) {
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
            std::filesystem::remove(platform::pathFromUtf8(temporaryPathFor(*path)), ignored);
        }
    }
}

bool isCancelled(const LegacyIoProgress* progress)
{
    return progress && progress->cancel.load();
}

}  // namespace

LegacySaveResult LegacyProjectFileWriter::save(const LegacyProjectPaths& paths,
                                               const document::ProjectSnapshot& snapshot,
                                               LegacyIoProgress* progress) const
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

    // The editor file is a .cRPX unless a .cRP of the Win32 editor is asked for by name.
    const bool crpx = hasCrpxExtension(paths.crpPath);

    // All files are written completely beside their targets first, so a failed or interrupted
    // save never leaves a truncated project behind. The archive is built from the temporary
    // .cROM, so it holds exactly the bytes that become the .cROM.
    // A cancel is honoured between the steps and while the files are written; libserum's
    // compression runs to its end, and the cancel takes effect after it. Until the renames
    // nothing but the temporary files has been touched.
    if (progress) {
        progress->done.store(0);
        progress->total.store(legacyCromByteCount(snapshot) +
                              (crpx ? crpxByteCount(snapshot) : legacyCrpByteCount(snapshot)));
        progress->setStage(LegacyIoStage::Writing);
    }
    std::string errorMessage;
    bool written = !isCancelled(progress) &&
                   writeTemporaryFile(paths.cromPath, ".cROM", writeLegacyCrom, snapshot,
                                      progress, errorMessage) &&
                   writeTemporaryFile(paths.crpPath, crpx ? ".cRPX" : ".cRP",
                                      crpx ? writeCrpx : writeLegacyCrp, snapshot, progress,
                                      errorMessage);
    if (written && !paths.crzPath.empty() && !isCancelled(progress)) {
        if (progress) {
            progress->setStage(LegacyIoStage::Compressing);
        }
        std::string detail;
        written = writeCrzArchive(temporaryPathFor(paths.crzPath),
                                  crzEntryNameFor(snapshot.romName),
                                  temporaryPathFor(paths.cromPath),
                                  detail);
        if (!written) {
            errorMessage = "could not write .cRZ file: " + paths.crzPath + " (" + detail + ")";
        }
    }
    if (isCancelled(progress)) {
        removeTemporaryFiles(paths);
        return makeSaveCancelled();
    }
    if (!written) {
        removeTemporaryFiles(paths);
        return makeSaveFailure(errorMessage);
    }
    if (progress) {
        progress->setStage(LegacyIoStage::Replacing);
    }

    // std::filesystem::rename replaces an existing target on every platform: POSIX rename(),
    // and MoveFileExW(MOVEFILE_REPLACE_EXISTING) in both MSVC's and MinGW's libstdc++.
    std::error_code error;
    const auto replace = [&error](const std::string& target) {
        std::filesystem::rename(platform::pathFromUtf8(temporaryPathFor(target)),
                                platform::pathFromUtf8(target), error);
    };
    replace(paths.cromPath);
    if (!error) {
        replace(paths.crpPath);
    }
    if (!error && !paths.crzPath.empty()) {
        replace(paths.crzPath);
    }
    if (error) {
        removeTemporaryFiles(paths);
        return makeSaveFailure("could not replace project files: " + error.message());
    }
    return makeSaveSuccess();
}

}  // namespace colorizing::legacy
