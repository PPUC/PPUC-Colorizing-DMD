#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace colorizing::legacy {

struct LegacyIoProgress;

// Sequential little-endian reader over a cROM or cRP, either in memory or streamed from a file
// through a small buffer, so that a load never holds a whole file next to what it is parsed
// into. Reports how far it got to a LegacyIoProgress and stops (every read fails) once that
// progress asks to cancel.
class LegacyByteSource {
public:
    // Reads the caller's buffer, which must outlive the source. progressBase is added to the
    // offset in what is reported, for a load that reads several files.
    LegacyByteSource(const uint8_t* bytes,
                     std::size_t size,
                     LegacyIoProgress* progress = nullptr,
                     uint64_t progressBase = 0);
    // Opens the file (UTF-8 path); isOpen() says whether that worked.
    explicit LegacyByteSource(const std::string& utf8Path,
                              LegacyIoProgress* progress = nullptr,
                              uint64_t progressBase = 0);

    LegacyByteSource(const LegacyByteSource&) = delete;
    LegacyByteSource& operator=(const LegacyByteSource&) = delete;

    [[nodiscard]] bool isOpen() const { return m_open; }
    [[nodiscard]] std::size_t size() const { return m_size; }
    [[nodiscard]] std::size_t offset() const { return m_windowOffset + m_position; }
    [[nodiscard]] std::size_t remaining() const { return m_size - offset(); }
    // The file could not be read although it is long enough.
    [[nodiscard]] bool readFailed() const { return m_readFailed; }
    [[nodiscard]] bool cancelled() const { return m_cancelled; }

    // Up to `count` bytes from the current position without consuming them (fewer at the end
    // of the data). Valid until the next call.
    [[nodiscard]] const uint8_t* peek(std::size_t count, std::size_t& available);

    [[nodiscard]] bool skip(std::size_t count);
    [[nodiscard]] bool readBytes(uint8_t* out, std::size_t count);
    [[nodiscard]] bool readU16(uint16_t& out);
    [[nodiscard]] bool readU32(uint32_t& out);
    [[nodiscard]] bool readU16s(uint16_t* out, std::size_t count);
    [[nodiscard]] bool readU32s(uint32_t* out, std::size_t count);

private:
    // Makes at least `count` bytes (or all that are left) available in the window.
    bool fill(std::size_t count);
    void reportProgress();
    [[nodiscard]] std::size_t available() const { return m_windowSize - m_position; }

    const uint8_t* m_window = nullptr;
    std::size_t m_windowOffset = 0;
    std::size_t m_windowSize = 0;
    std::size_t m_position = 0;
    std::size_t m_size = 0;
    bool m_open = false;
    bool m_readFailed = false;
    bool m_cancelled = false;
    std::ifstream m_file;
    std::vector<uint8_t> m_buffer;
    LegacyIoProgress* m_progress = nullptr;
    uint64_t m_progressBase = 0;
    std::size_t m_nextReport = 0;
};

}  // namespace colorizing::legacy
