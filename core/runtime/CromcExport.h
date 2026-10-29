#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectSnapshot.h"
#include "edit/AssetUse.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::runtime {

// The version of the libserum the editor is built with, e.g. "2.8.2".
[[nodiscard]] std::string libserumVersion();

// The file of the folder with that name in any case, as libserum looks for its sidecars
// ("<rom>.pup.csv", "scaling.txt"); nothing without one.
[[nodiscard]] std::optional<std::filesystem::path> findCaseInsensitive(
    const std::filesystem::path& directory,
    const std::string& name);
// A path as libserum's char paths take it: on Windows in the ANSI code page, by its short name
// for other characters; nothing if it cannot be given. The file or folder must exist.
[[nodiscard]] std::optional<std::string> libserumPath(const std::filesystem::path& path);

struct CromcExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    // The files of sidecarDirectory that went into the cROMc ("<rom>.pup.csv", "scaling.txt").
    std::vector<std::string> sidecars;
    // The comparison masks, backgrounds and sprites no frame uses, left out of the cROMc.
    edit::DroppedAssets dropped;
};

// Writes the project's .cROMc to target as libserum makes it on a machine: the project is
// written as <rom>.cROM into <workDirectory>/<rom>/ (an altcolor folder of its own) with the
// <rom>.pup.csv and scaling.txt of sidecarDirectory, if it has them (names in any case), and
// libserum loads it with cROMc generation on. sidecarDirectory may be empty for none. A
// project with colorization settings (document/Scene.h) carries its scenes and scaling
// settings in the cROM, and its sidecars are not taken. The comparison masks, backgrounds and
// sprites no frame uses are left out (edit::dropUnusedAssets()), the others renumbered.
//
// target is replaced only when everything worked. workDirectory is created and removed again.
// On Windows libserum opens files by ANSI paths: when it cannot open files in workDirectory
// (characters outside the code page, no short name), a folder of the same name in
// %ProgramData%\PPUC-Colorizing-DMD is used instead.
// libserum holds one colorization per process: this loads and releases one, so whatever
// RuntimePreview had loaded is gone (close it first; it loads again when asked). progress
// covers writing the cROM; libserum's part cannot be cancelled.
[[nodiscard]] CromcExportResult exportCromc(const document::ProjectSnapshot& snapshot,
                                            const std::filesystem::path& workDirectory,
                                            const std::filesystem::path& sidecarDirectory,
                                            const std::filesystem::path& target,
                                            legacy::LegacyIoProgress* progress = nullptr);

}  // namespace colorizing::runtime
