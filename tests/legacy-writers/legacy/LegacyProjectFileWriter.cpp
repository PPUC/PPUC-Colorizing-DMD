#include "legacy/LegacyProjectFileWriter.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "legacy/CrpxFile.h"
#include "legacy/CrpxWriter.h"
#include "legacy/CrzWriter.h"
#include "legacy/LegacyCrpWriter.h"
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
    if (paths.crpPath.empty() || (paths.cromPath.empty() && paths.crzPath.empty())) {
        return makeSaveFailure("a .cRZ or .cROM path and the .cRP path are required");
    }
    // Raw cROM bytes under an archive name would be unreadable for everything that opens it.
    if (hasCrzExtension(paths.cromPath) || hasCrzExtension(paths.crpPath)) {
        return makeSaveFailure("a .cRZ archive cannot be the .cROM or .cRP target");
    }
    if (paths.crzPath == paths.crpPath ||
        (!paths.cromPath.empty() && paths.crzPath == paths.cromPath)) {
        return makeSaveFailure("the .cRZ path must differ from the .cROM and .cRP paths");
    }

    // The editor file is a .cRPX unless a .cRP of the Win32 editor is asked for by name.
    const bool crpx = hasCrpxExtension(paths.crpPath);

    // All files are written completely beside their targets first, so a failed or interrupted
    // save never leaves a truncated project behind. The archive gets the cROM compressed while
    // it is written: no .cROM is written for it. A cancel is honoured while the files are
    // written; until the renames nothing but the temporary files has been touched.
    const uint64_t cromBytes = legacyCromByteCount(snapshot);
    if (progress) {
        progress->done.store(0);
        progress->total.store((paths.cromPath.empty() ? 0 : cromBytes) +
                              (paths.crzPath.empty() ? 0 : cromBytes) +
                              (crpx ? crpxByteCount(snapshot) : legacyCrpByteCount(snapshot)));
        progress->setStage(LegacyIoStage::Writing);
    }
    std::string errorMessage;
    bool written = !isCancelled(progress) &&
                   (paths.cromPath.empty() ||
                    writeTemporaryFile(paths.cromPath, ".cROM", writeLegacyCrom, snapshot,
                                       progress, errorMessage)) &&
                   writeTemporaryFile(paths.crpPath, crpx ? ".cRPX" : ".cRP",
                                      crpx ? writeCrpx : writeLegacyCrp, snapshot, progress,
                                      errorMessage);
    if (written && !paths.crzPath.empty() && !isCancelled(progress)) {
        std::string detail;
        written = writeCrzArchive(
            temporaryPathFor(paths.crzPath), crzEntryNameFor(snapshot.romName), cromBytes,
            [&](std::ostream& stream) {
                return writeLegacyCrom(stream, snapshot, detail, nullptr, progress);
            },
            kCrzCompressionLevel, detail);
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
        if (!error && !target.empty()) {
            std::filesystem::rename(platform::pathFromUtf8(temporaryPathFor(target)),
                                    platform::pathFromUtf8(target), error);
        }
    };
    replace(paths.cromPath);
    replace(paths.crpPath);
    replace(paths.crzPath);
    if (error) {
        removeTemporaryFiles(paths);
        return makeSaveFailure("could not replace project files: " + error.message());
    }
    return makeSaveSuccess();
}

}  // namespace colorizing::legacy
