#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace colorizing::legacy {

// A .cRZ is a zip archive holding one cROM. The archive code itself lives in libserum; this is
// the only place of the editor that talks to it.

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

[[nodiscard]] bool writeCrzArchive(const std::string& crzPath,
                                   const std::string& entryName,
                                   const std::string& cromPath,
                                   std::string& errorMessage);

}  // namespace colorizing::legacy
