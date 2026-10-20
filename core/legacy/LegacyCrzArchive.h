#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace colorizing::legacy {

// A .cRZ is a zip archive holding one cROM. libserum reads it (this is the only place of the
// editor that talks to libserum's archive code); the editor writes it itself, compressing the
// cROM while it is written.

[[nodiscard]] bool hasCrzExtension(const std::string& path);

// The entry is "<romName>.cRom", the spelling the Win32 editor uses.
[[nodiscard]] std::string crzEntryNameFor(const std::string& romName);

[[nodiscard]] bool readCrzArchive(const std::string& crzPath,
                                  std::vector<uint8_t>& cromBytes,
                                  std::string& errorMessage);

// The cROM of an archive in the buffer libserum unpacked it into, without a copy; freed by
// release() or the destructor. libserum unpacks the whole entry at once (Serum_CRZ_Read), so
// the buffer is as large as the cROM.
class CrzContent {
public:
    CrzContent() = default;
    CrzContent(const CrzContent&) = delete;
    CrzContent& operator=(const CrzContent&) = delete;
    ~CrzContent();

    [[nodiscard]] const uint8_t* data() const { return m_data; }
    [[nodiscard]] std::size_t size() const { return m_size; }
    void release();

private:
    friend bool readCrzArchive(const std::string&, CrzContent&, std::string&);
    uint8_t* m_data = nullptr;
    std::size_t m_size = 0;
};

[[nodiscard]] bool readCrzArchive(const std::string& crzPath,
                                  CrzContent& content,
                                  std::string& errorMessage);

// The name is data from the archive and must never be used to build a path.
[[nodiscard]] bool readCrzEntryName(const std::string& crzPath,
                                    std::string& entryName,
                                    std::string& errorMessage);

// The deflate level the editor writes archives with: fast; a cROM compresses well anyway.
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
