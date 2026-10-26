#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

#include "document/ProjectSnapshot.h"

namespace colorizing::legacy {

struct LegacyProjectPaths {
    // For loading this may name a .cRZ archive instead; the cROM is then read from it. For
    // saving: where a .cROM goes; empty writes none (the editor saves the .cRZ only).
    std::string cromPath;
    // The editor file. Loading reads a .cRPX (see CrpxFile.h) or a .cRP of the Win32 editor,
    // whatever the name. Saving writes a .cRPX unless the name ends in something else: a
    // Win32 .cRP is written only for comparisons with the Win32 editor's files.
    std::string crpPath;
    // Only used for saving: where the .cRZ archive goes, the cROM compressed while it is
    // written. Empty writes none.
    std::string crzPath = {};
};

struct LegacyIoError {
    std::string message;
};

// What a load or a save is doing, for a progress display.
enum class LegacyIoStage : int {
    Starting,
    // Reading and parsing the files; LegacyIoProgress::done/total count bytes.
    Reading,
    // libserum unpacks the .cRZ; no progress inside it.
    Decompressing,
    // Writing the .cRZ (the cROM compressed while it is written) and the editor file;
    // done/total count bytes.
    Writing,
    // Renaming the written files over the old ones.
    Replacing,
    // libserum loads the written .cROM and generates its .cROMc; no progress inside it.
    Generating,
    // Writing pictures of frames (an animated GIF); done/total count frames.
    ExportingFrames,
    // Reading or writing a project file (project/ProjectFile.h); done/total count its
    // entries.
    ReadingProjectFile,
    WritingProjectFile,
};

// Shared between a load or save running on a worker thread and whoever shows its progress.
// The worker writes stage, done and total; any thread may read them and set cancel. A
// cancelled load returns no project; a cancelled save leaves the existing files as they were.
// The libserum steps (Decompressing, Generating) cannot be interrupted: a cancel asked
// during them takes effect when they return.
struct LegacyIoProgress {
    std::atomic<int> stage{static_cast<int>(LegacyIoStage::Starting)};
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> total{0};
    std::atomic<bool> cancel{false};

    void setStage(LegacyIoStage value) { stage.store(static_cast<int>(value)); }
    [[nodiscard]] LegacyIoStage currentStage() const
    {
        return static_cast<LegacyIoStage>(stage.load());
    }
};

// The message of a load or save that stopped because LegacyIoProgress::cancel was set.
inline constexpr const char* kLegacyIoCancelledMessage = "cancelled";

struct LegacyLoadResult {
    document::ProjectSnapshot snapshot;
    LegacyIoError error;
    bool ok = false;
    bool cancelled = false;
};

struct LegacySaveResult {
    LegacyIoError error;
    bool ok = false;
    bool cancelled = false;
};

class ILegacyProjectReader {
public:
    virtual ~ILegacyProjectReader() = default;
    // progress may be null. The snapshot's blocks are compact (see document::CompactArray):
    // uniform blocks share one buffer, so it costs about what the project's real content costs.
    [[nodiscard]] virtual LegacyLoadResult load(const LegacyProjectPaths& paths,
                                                LegacyIoProgress* progress = nullptr) const = 0;
};

[[nodiscard]] LegacyLoadResult makeLoadFailure(std::string message);
[[nodiscard]] LegacySaveResult makeSaveFailure(std::string message);
[[nodiscard]] LegacyLoadResult makeLoadSuccess(document::ProjectSnapshot snapshot);
[[nodiscard]] LegacySaveResult makeSaveSuccess();
[[nodiscard]] LegacyLoadResult makeLoadCancelled();
[[nodiscard]] LegacySaveResult makeSaveCancelled();

}  // namespace colorizing::legacy

