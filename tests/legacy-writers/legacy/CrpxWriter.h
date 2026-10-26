#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectIo.h"
#include "legacy/LegacyProjectSerializer.h"

// DEPRECATED test support (see tests/legacy-writers/README.md): the editor writes no .cRPX.
// To be removed together with the tests that still write legacy files.

namespace colorizing::legacy {

// Writes the whole .cRPX of the snapshot (layout: core/legacy/CrpxFile.h). Same contract as
// writeLegacyCrp(); sections are the cRP part's, counted from the start of the cRP part.
[[nodiscard]] bool writeCrpx(std::ostream& stream,
                             const document::ProjectSnapshot& snapshot,
                             std::string& errorMessage,
                             LegacySectionOffsets* sections = nullptr,
                             LegacyIoProgress* progress = nullptr);
[[nodiscard]] uint64_t crpxByteCount(const document::ProjectSnapshot& snapshot);

}  // namespace colorizing::legacy
