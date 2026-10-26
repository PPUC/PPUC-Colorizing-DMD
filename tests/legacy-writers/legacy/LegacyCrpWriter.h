#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectIo.h"
#include "legacy/LegacyProjectSerializer.h"

// DEPRECATED test support (see tests/legacy-writers/README.md): the editor writes no .cRP.
// To be removed together with the tests that still write legacy files.

namespace colorizing::legacy {

// The .cRP of the snapshot in the Win32 editor's layout. With a progress, every byte written is
// added to its `done`, and a set `cancel` stops the write (it returns false with
// kLegacyIoCancelledMessage; what was written so far stays in the stream).
[[nodiscard]] bool writeLegacyCrp(std::ostream& stream,
                                  const document::ProjectSnapshot& snapshot,
                                  std::string& errorMessage,
                                  LegacySectionOffsets* sections = nullptr,
                                  LegacyIoProgress* progress = nullptr);

// The cRP part of a .cRPX (CrpxFile.h): writeLegacyCrp() with an empty PuP pack field, as the
// .cRPX keeps the pack in a chunk of its own. As many bytes as writeLegacyCrp().
[[nodiscard]] bool writeCrpxCrpPart(std::ostream& stream,
                                    const document::ProjectSnapshot& snapshot,
                                    std::string& errorMessage,
                                    LegacySectionOffsets* sections = nullptr,
                                    LegacyIoProgress* progress = nullptr);

// How many bytes writeLegacyCrp() writes for a valid snapshot, without writing them.
[[nodiscard]] uint64_t legacyCrpByteCount(const document::ProjectSnapshot& snapshot);

}  // namespace colorizing::legacy
