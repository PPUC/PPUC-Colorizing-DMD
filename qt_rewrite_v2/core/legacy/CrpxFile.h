#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::legacy {

class LegacyByteSource;

// The editor file of a project saved by this editor: "<name>.cRPX" beside its .cROM, in place
// of the Win32 editor's "<name>.cRP". The Win32 editor (3.17.3) opens a .cROM with the .cRP of
// the same name only, and reads it without any check, so it never opens a project this editor
// saved; the editor file can be extended without regard to it. The .cROM stays what libserum
// reads.
//
// Layout, little endian:
//   char[8]  "CDMDcRPX"
//   uint32   format version (kCrpxVersion; a reader refuses a newer one)
//   uint32   header size in bytes, from the start of the file (24 for version 1)
//   uint64   size of the cRP part
//   the cRP part: what writeLegacyCrp() writes, as in a .cRP of the Win32 editor
//   chunks up to the end of the file: char[4] tag, uint32 0, uint64 size, the data. Version 1
//   writes none; a reader skips those it does not know.
inline constexpr uint32_t kCrpxVersion = 1;
inline constexpr uint32_t kCrpxHeaderBytes = 24;

// Whether the path ends in ".cRPX" (any case).
[[nodiscard]] bool hasCrpxExtension(const std::string& path);

// Writes the whole .cRPX of the snapshot. Same contract as writeLegacyCrp(); sections are the
// cRP part's, counted from the start of the cRP part.
[[nodiscard]] bool writeCrpx(std::ostream& stream,
                             const document::ProjectSnapshot& snapshot,
                             std::string& errorMessage,
                             LegacySectionOffsets* sections = nullptr,
                             LegacyIoProgress* progress = nullptr);
[[nodiscard]] uint64_t crpxByteCount(const document::ProjectSnapshot& snapshot);

enum class CrpxProbe {
    // No .cRPX header: a .cRP of the Win32 editor, the source is unchanged.
    LegacyCrp,
    // A .cRPX: the source is at the start of the cRP part and ends with it.
    Crpx,
    // A .cRPX that cannot be read; errorMessage says why.
    Invalid,
};

// Looks at the start of an editor file, which may be either kind.
[[nodiscard]] CrpxProbe probeCrpx(LegacyByteSource& source, std::string& errorMessage);

}  // namespace colorizing::legacy
