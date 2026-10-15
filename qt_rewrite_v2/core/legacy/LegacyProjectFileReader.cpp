#include "legacy/LegacyProjectFileReader.h"

#include <filesystem>
#include <exception>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "legacy/CrpxFile.h"
#include "legacy/LegacyByteSource.h"
#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacyCrzArchive.h"
#include "legacy/LegacyCrpOriginals.h"
#include "legacy/LegacySnapshotBuilder.h"
#include "platform/Utf8Path.h"

namespace colorizing::legacy {

namespace {

bool isCancelled(const LegacyIoProgress* progress)
{
    return progress && progress->cancel.load();
}

uint64_t fileSize(const std::string& path)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(platform::pathFromUtf8(path), error);
    return error ? 0 : static_cast<uint64_t>(size);
}

}  // namespace

// The files are parsed as they are read, block by block, straight into the records of the
// snapshot (see LegacyByteSource): no file is held in memory as a whole, except the cROM of
// a .cRZ, which libserum unpacks into one buffer; that buffer is freed as soon as it is parsed.
// The parsed blocks are then moved, not copied, into the snapshot.
LegacyLoadResult LegacyProjectFileReader::load(const LegacyProjectPaths& paths,
                                               LegacyIoProgress* progress) const
{
    if (paths.cromPath.empty()) {
        return makeLoadFailure(".cROM path is empty");
    }
    if (isCancelled(progress)) {
        return makeLoadCancelled();
    }
    const uint64_t crpSize = paths.crpPath.empty() ? 0 : fileSize(paths.crpPath);

    LegacyCromFrameParseResult cromFrames;
    if (hasCrzExtension(paths.cromPath)) {
        if (progress) {
            progress->setStage(LegacyIoStage::Decompressing);
        }
        CrzContent content;
        std::string detail;
        if (!readCrzArchive(paths.cromPath, content, detail)) {
            return makeLoadFailure("could not read .cRZ file: " + paths.cromPath + " (" + detail +
                                   ")");
        }
        if (isCancelled(progress)) {
            return makeLoadCancelled();
        }
        if (progress) {
            progress->total.store(content.size() + crpSize);
            progress->setStage(LegacyIoStage::Reading);
        }
        LegacyByteSource source(content.data(), content.size(), progress);
        cromFrames = parseModernLegacyCromFrames(source);
        if (source.cancelled()) {
            return makeLoadCancelled();
        }
    } else {
        LegacyByteSource source(paths.cromPath, progress);
        if (!source.isOpen()) {
            return makeLoadFailure("could not open .cROM file: " + paths.cromPath);
        }
        if (progress) {
            progress->total.store(source.size() + crpSize);
            progress->setStage(LegacyIoStage::Reading);
        }
        cromFrames = parseModernLegacyCromFrames(source);
        if (source.cancelled()) {
            return makeLoadCancelled();
        }
        if (source.readFailed()) {
            return makeLoadFailure("could not read .cROM file: " + paths.cromPath);
        }
    }
    if (!cromFrames.ok) {
        return makeLoadFailure(cromFrames.errorMessage);
    }

    if (paths.crpPath.empty()) {
        return makeLoadFailure(".cRP path is empty");
    }
    const uint64_t cromBytes = progress ? progress->total.load() - crpSize : 0;
    LegacyByteSource crpSource(paths.crpPath, progress, cromBytes);
    if (!crpSource.isOpen()) {
        return makeLoadFailure("could not open .cRP file: " + paths.crpPath);
    }
    std::string crpxError;
    uint64_t chunksStart = 0;
    const CrpxProbe probe = probeCrpx(crpSource, crpxError, &chunksStart);
    if (probe == CrpxProbe::Invalid) {
        return makeLoadFailure(crpxError + ": " + paths.crpPath);
    }
    LegacyCrpOriginalParseResult crpOriginals =
        parseLegacyCrpOriginalFrames(crpSource, cromFrames.frameData.metadata);
    if (crpSource.cancelled()) {
        return makeLoadCancelled();
    }
    if (crpSource.readFailed()) {
        return makeLoadFailure("could not read .cRP file: " + paths.crpPath);
    }
    if (!crpOriginals.ok) {
        return makeLoadFailure(crpOriginals.errorMessage);
    }

    LegacySnapshotBuildResult snapshot =
        buildProjectSnapshot(std::move(cromFrames.frameData), std::move(crpOriginals.originalData));
    if (!snapshot.ok) {
        return makeLoadFailure(snapshot.errorMessage);
    }
    // What a .cRPX holds after its cRP part (the helper lines), from the file again: the
    // source above ends with the cRP part.
    if (probe == CrpxProbe::Crpx) {
        LegacyByteSource chunkSource(paths.crpPath);
        if (!chunkSource.isOpen() ||
            !readCrpxChunks(chunkSource, chunksStart, snapshot.snapshot, crpxError)) {
            return makeLoadFailure((crpxError.empty() ? std::string("could not read the .cRPX")
                                                      : crpxError) +
                                   ": " + paths.crpPath);
        }
    }
    if (isCancelled(progress)) {
        return makeLoadCancelled();
    }
    if (progress) {
        progress->done.store(progress->total.load());
    }
    return makeLoadSuccess(std::move(snapshot.snapshot));
}

LegacyDocumentLoadResult loadLegacyProjectDocument(const LegacyProjectPaths& paths,
                                                   LegacyIoProgress* progress)
{
    LegacyDocumentLoadResult result;
    try {
        LegacyLoadResult loaded = LegacyProjectFileReader().load(paths, progress);
        if (!loaded.ok) {
            result.error = std::move(loaded.error);
            result.cancelled = loaded.cancelled;
            return result;
        }
        result.document = document::ProjectDocument::fromSnapshot(std::move(loaded.snapshot));
        result.ok = true;
    } catch (const std::exception& ex) {
        result.document.reset();
        result.error.message = ex.what();
    }
    return result;
}

}  // namespace colorizing::legacy
