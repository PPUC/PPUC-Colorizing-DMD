#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <string>

namespace colorizing::importing {

// A frame dump file opened for reading: a dump as it is, or one compressed as gzip (.gz) or in
// a zip archive (.zip), decompressed while it is read. Which it is is told by the first bytes,
// not by the name. Of a zip the first entry whose name ends in ".txt" is read, or its first
// file if none does.
struct DumpFile {
    std::unique_ptr<std::istream> stream;
    // The bytes the stream gives (the uncompressed dump), for a progress display; 0 when it is
    // not known.
    uint64_t sizeBytes = 0;
    // The zip entry read; empty otherwise.
    std::string entryName;
    // Why the file cannot be read; empty when stream is set.
    std::string error;

    [[nodiscard]] bool ok() const { return stream != nullptr; }
};

[[nodiscard]] DumpFile openDumpFile(const std::string& path);

}  // namespace colorizing::importing
