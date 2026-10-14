#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::runtime {

struct CromcExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    // The files of sidecarDirectory that went into the cROMc ("<rom>.pup.csv", "scaling.txt").
    std::vector<std::string> sidecars;
};

// Writes the project's .cROMc to target as libserum makes it on a machine: the project is
// written as <rom>.cROM into <workDirectory>/<rom>/ (an altcolor folder of its own) with the
// <rom>.pup.csv and scaling.txt of sidecarDirectory, if it has them (names in any case), and
// libserum loads it with cROMc generation on. sidecarDirectory may be empty for none.
//
// target is replaced only when everything worked. workDirectory is created and removed again.
// libserum holds one colorization per process: this loads and releases one, so whatever
// RuntimePreview had loaded is gone (close it first; it loads again when asked). progress
// covers writing the cROM; libserum's part cannot be cancelled.
[[nodiscard]] CromcExportResult exportCromc(const document::ProjectSnapshot& snapshot,
                                            const std::filesystem::path& workDirectory,
                                            const std::filesystem::path& sidecarDirectory,
                                            const std::filesystem::path& target,
                                            legacy::LegacyIoProgress* progress = nullptr);

}  // namespace colorizing::runtime
