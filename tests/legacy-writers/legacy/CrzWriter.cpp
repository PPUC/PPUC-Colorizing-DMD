#include "legacy/CrzWriter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

#include <miniz/miniz.h>

#include "platform/Utf8Path.h"

namespace colorizing::legacy {

namespace {

// Deflates what is written into it straight into the archive file, and counts and checksums
// it.
class DeflateBuffer final : public std::streambuf {
public:
    DeflateBuffer(std::ostream& file, int level)
        : m_file(file)
        , m_compressor(std::make_unique<tdefl_compressor>())
        , m_buffer(std::size_t{1} << 20)
    {
        const mz_uint flags = tdefl_create_comp_flags_from_zip_params(
            level, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);
        m_ok = tdefl_init(m_compressor.get(), &DeflateBuffer::put, this,
                          static_cast<int>(flags)) == TDEFL_STATUS_OKAY;
        setp(m_buffer.data(), m_buffer.data() + m_buffer.size());
    }

    // Ends the deflate stream; false when anything failed.
    bool finish()
    {
        m_ok = m_ok && deflatePending() &&
               tdefl_compress_buffer(m_compressor.get(), nullptr, 0, TDEFL_FINISH) ==
                   TDEFL_STATUS_DONE;
        return m_ok && m_file.good();
    }

    [[nodiscard]] uint64_t uncompressed() const { return m_uncompressed; }
    [[nodiscard]] uint64_t compressed() const { return m_compressed; }
    [[nodiscard]] uint32_t crc() const { return m_crc; }

protected:
    int_type overflow(int_type c) override
    {
        if (!deflatePending()) {
            return traits_type::eof();
        }
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(c);
            pbump(1);
        }
        return traits_type::not_eof(c);
    }

    int sync() override { return deflatePending() ? 0 : -1; }

private:
    static mz_bool put(const void* data, int length, void* user)
    {
        auto* self = static_cast<DeflateBuffer*>(user);
        self->m_file.write(static_cast<const char*>(data), length);
        self->m_compressed += static_cast<uint64_t>(length);
        return self->m_file.good() ? MZ_TRUE : MZ_FALSE;
    }

    bool deflatePending()
    {
        const std::size_t length = static_cast<std::size_t>(pptr() - pbase());
        if (m_ok && length > 0) {
            m_crc = static_cast<uint32_t>(
                mz_crc32(m_crc, reinterpret_cast<const unsigned char*>(pbase()), length));
            m_uncompressed += length;
            m_ok = tdefl_compress_buffer(m_compressor.get(), pbase(), length, TDEFL_NO_FLUSH) ==
                   TDEFL_STATUS_OKAY;
        }
        setp(m_buffer.data(), m_buffer.data() + m_buffer.size());
        return m_ok;
    }

    std::ostream& m_file;
    std::unique_ptr<tdefl_compressor> m_compressor;
    std::vector<char> m_buffer;
    bool m_ok = false;
    uint64_t m_uncompressed = 0;
    uint64_t m_compressed = 0;
    uint32_t m_crc = static_cast<uint32_t>(MZ_CRC32_INIT);
};

void put16(std::ostream& out, uint32_t value)
{
    const char bytes[2] = {static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff)};
    out.write(bytes, 2);
}

void put32(std::ostream& out, uint32_t value)
{
    put16(out, value & 0xffff);
    put16(out, value >> 16);
}

void put64(std::ostream& out, uint64_t value)
{
    put32(out, static_cast<uint32_t>(value));
    put32(out, static_cast<uint32_t>(value >> 32));
}

constexpr uint32_t kMax32 = 0xffffffffu;

}  // namespace

bool writeCrzArchive(const std::string& crzPath,
                     const std::string& entryName,
                     uint64_t cromSize,
                     const std::function<bool(std::ostream&)>& write,
                     int level,
                     std::string& errorMessage,
                     bool forceZip64)
{
    if (entryName.empty() || entryName.size() > 0xffff ||
        entryName.find_first_of("/\\:") != std::string::npos) {
        errorMessage = "the archive entry name is not a plain file name";
        return false;
    }
    std::ofstream file(platform::pathFromUtf8(crzPath), std::ios::binary | std::ios::trunc);
    if (!file) {
        errorMessage = "could not create .cRZ archive";
        return false;
    }
    const bool zip64 = forceZip64 || cromSize >= kMax32;
    const bool ascii = std::all_of(entryName.begin(), entryName.end(),
                                   [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    const uint32_t flags = ascii ? 0 : 0x0800;
    const uint32_t version = zip64 ? 45 : 20;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const uint32_t dosTime = static_cast<uint32_t>((local.tm_hour << 11) | (local.tm_min << 5) |
                                                   (local.tm_sec / 2));
    const uint32_t dosDate = static_cast<uint32_t>(((std::max(local.tm_year, 80) - 80) << 9) |
                                                   ((local.tm_mon + 1) << 5) | local.tm_mday);
    const uint32_t extraLength = zip64 ? 20 : 0;

    // The local header, its checksum and sizes filled in once the entry is written.
    put32(file, 0x04034b50);
    put16(file, version);
    put16(file, flags);
    put16(file, 8);  // deflate
    put16(file, dosTime);
    put16(file, dosDate);
    put32(file, 0);
    put32(file, zip64 ? kMax32 : 0);
    put32(file, zip64 ? kMax32 : 0);
    put16(file, static_cast<uint32_t>(entryName.size()));
    put16(file, extraLength);
    file.write(entryName.data(), static_cast<std::streamsize>(entryName.size()));
    const std::streamoff zip64Sizes = file.tellp() + std::streamoff{4};
    if (zip64) {
        put16(file, 0x0001);
        put16(file, 16);
        put64(file, 0);
        put64(file, 0);
    }

    DeflateBuffer deflate(file, level);
    {
        std::ostream entry(&deflate);
        if (!write(entry) || !entry.flush()) {
            errorMessage = "could not write the cROM into the .cRZ archive";
            return false;
        }
    }
    if (!deflate.finish()) {
        errorMessage = "could not compress the cROM into the .cRZ archive";
        return false;
    }
    if (deflate.uncompressed() != cromSize) {
        errorMessage = "the cROM written into the .cRZ archive has not the expected size";
        return false;
    }
    const uint64_t compressed = deflate.compressed();
    if (!zip64 && compressed >= kMax32) {
        errorMessage = "the compressed cROM is too large for a .cRZ archive without zip64";
        return false;
    }
    const std::streamoff dataEnd = file.tellp();
    file.seekp(14);
    put32(file, deflate.crc());
    if (zip64) {
        file.seekp(zip64Sizes);
        put64(file, cromSize);
        put64(file, compressed);
    } else {
        put32(file, static_cast<uint32_t>(compressed));
        put32(file, static_cast<uint32_t>(cromSize));
    }
    file.seekp(dataEnd);

    // The central directory, with the one entry at offset 0.
    const uint64_t directoryOffset = static_cast<uint64_t>(dataEnd);
    put32(file, 0x02014b50);
    put16(file, version);
    put16(file, version);
    put16(file, flags);
    put16(file, 8);
    put16(file, dosTime);
    put16(file, dosDate);
    put32(file, deflate.crc());
    put32(file, zip64 ? kMax32 : static_cast<uint32_t>(compressed));
    put32(file, zip64 ? kMax32 : static_cast<uint32_t>(cromSize));
    put16(file, static_cast<uint32_t>(entryName.size()));
    put16(file, zip64 ? 20 : 0);
    put16(file, 0);  // comment
    put16(file, 0);  // disk
    put16(file, 0);  // internal attributes
    put32(file, 0);  // external attributes
    put32(file, 0);  // local header offset
    file.write(entryName.data(), static_cast<std::streamsize>(entryName.size()));
    if (zip64) {
        put16(file, 0x0001);
        put16(file, 16);
        put64(file, cromSize);
        put64(file, compressed);
    }
    const uint64_t directorySize = static_cast<uint64_t>(file.tellp()) - directoryOffset;
    if (zip64) {
        const uint64_t zip64End = static_cast<uint64_t>(file.tellp());
        put32(file, 0x06064b50);
        put64(file, 44);
        put16(file, version);
        put16(file, version);
        put32(file, 0);
        put32(file, 0);
        put64(file, 1);
        put64(file, 1);
        put64(file, directorySize);
        put64(file, directoryOffset);
        put32(file, 0x07064b50);
        put32(file, 0);
        put64(file, zip64End);
        put32(file, 1);
    }
    put32(file, 0x06054b50);
    put16(file, 0);
    put16(file, 0);
    put16(file, 1);
    put16(file, 1);
    put32(file, static_cast<uint32_t>(directorySize));
    put32(file, directoryOffset >= kMax32 ? kMax32 : static_cast<uint32_t>(directoryOffset));
    put16(file, 0);
    file.close();
    if (!file) {
        errorMessage = "could not write .cRZ archive";
        return false;
    }
    return true;
}

}  // namespace colorizing::legacy
