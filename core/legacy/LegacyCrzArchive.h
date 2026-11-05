#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace colorizing::legacy {

// A .cRZ is a zip archive holding one cROM, read here through miniz; the editor writes none.

[[nodiscard]] bool hasCrzExtension(const std::string& path);

// The entry is "<romName>.cRom", the spelling the Win32 editor uses.
[[nodiscard]] std::string crzEntryNameFor(const std::string& romName);

[[nodiscard]] bool readCrzArchive(const std::string& crzPath,
                                  std::vector<uint8_t>& cromBytes,
                                  std::string& errorMessage);

// The cROM of an archive in the buffer miniz unpacked it into, without a copy; freed by
// release() or the destructor. The whole entry is unpacked at once, so the buffer is as large
// as the cROM.
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

}  // namespace colorizing::legacy
