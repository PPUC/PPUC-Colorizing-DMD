#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

// DEPRECATED test support (see tests/legacy-writers/README.md): the editor writes no .cRZ.
// To be removed together with the tests that still write legacy files.

namespace colorizing::legacy {

// The deflate level the archives are written with: fast; a cROM compresses well anyway.
constexpr int kCrzCompressionLevel = 1;

// Writes the archive with the cROM that write() puts into the stream (exactly cromSize bytes),
// compressed while it is written: the cROM is never on disk or in memory as a whole. One
// entry, the sizes in its local header, zip64 only for a cROM of 4 GiB or more (or when
// forceZip64, for tests), as libserum reads it. A failed write() or a size that differs fails
// the archive; the file is left for the caller to remove.
[[nodiscard]] bool writeCrzArchive(const std::string& crzPath,
                                   const std::string& entryName,
                                   uint64_t cromSize,
                                   const std::function<bool(std::ostream&)>& write,
                                   int level,
                                   std::string& errorMessage,
                                   bool forceZip64 = false);

}  // namespace colorizing::legacy
