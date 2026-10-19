#include "import/DumpFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <optional>
#include <streambuf>
#include <vector>

#include <miniz/miniz.h>

#include "platform/Utf8Path.h"

namespace colorizing::importing {

namespace {

constexpr std::size_t kBufferBytes = 1u << 16;

// The streams below own what they read from.
class OwningStream final : public std::istream {
public:
    explicit OwningStream(std::unique_ptr<std::streambuf> buffer)
        : std::istream(buffer.get()),
          m_buffer(std::move(buffer))
    {
    }

private:
    std::unique_ptr<std::streambuf> m_buffer;
};

// A gzip file (RFC 1952), its deflate data inflated as it is read. Only the first member is
// read, as gzip writes it.
class GzipBuffer final : public std::streambuf {
public:
    GzipBuffer(std::unique_ptr<std::ifstream> file, std::string& error) : m_file(std::move(file))
    {
        if (!skipHeader()) {
            error = "the gzip file is damaged";
            return;
        }
        std::memset(&m_stream, 0, sizeof(m_stream));
        if (mz_inflateInit2(&m_stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            error = "the gzip file cannot be read";
            return;
        }
        m_open = true;
    }
    ~GzipBuffer() override
    {
        if (m_open) {
            mz_inflateEnd(&m_stream);
        }
    }
    GzipBuffer(const GzipBuffer&) = delete;
    GzipBuffer& operator=(const GzipBuffer&) = delete;

    [[nodiscard]] bool isOpen() const { return m_open; }

protected:
    int_type underflow() override
    {
        if (gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        while (m_open && !m_finished) {
            if (m_stream.avail_in == 0) {
                m_file->read(m_in.data(), static_cast<std::streamsize>(m_in.size()));
                m_stream.next_in = reinterpret_cast<const unsigned char*>(m_in.data());
                m_stream.avail_in = static_cast<unsigned int>(m_file->gcount());
            }
            m_stream.next_out = reinterpret_cast<unsigned char*>(m_out.data());
            m_stream.avail_out = static_cast<unsigned int>(m_out.size());
            const int status = mz_inflate(&m_stream, MZ_NO_FLUSH);
            const std::size_t produced = m_out.size() - m_stream.avail_out;
            if (status == MZ_STREAM_END) {
                m_finished = true;
            } else if (status != MZ_OK && status != MZ_BUF_ERROR) {
                m_finished = true;
            } else if (produced == 0 && m_stream.avail_in == 0 && !*m_file) {
                // The file ended before the deflate data did.
                m_finished = true;
            }
            if (produced > 0) {
                setg(m_out.data(), m_out.data(), m_out.data() + produced);
                return traits_type::to_int_type(*gptr());
            }
        }
        return traits_type::eof();
    }

private:
    bool skipHeader()
    {
        unsigned char header[10] = {};
        if (!m_file->read(reinterpret_cast<char*>(header), 10) || header[0] != 0x1f ||
            header[1] != 0x8b || header[2] != 8) {
            return false;
        }
        const unsigned char flags = header[3];
        if (flags & 0x04) {  // FEXTRA
            unsigned char length[2] = {};
            if (!m_file->read(reinterpret_cast<char*>(length), 2)) {
                return false;
            }
            m_file->ignore(length[0] | (length[1] << 8));
        }
        for (const unsigned char text : {0x08, 0x10}) {  // FNAME, FCOMMENT: NUL-terminated
            if (flags & text) {
                char c = 0;
                do {
                    if (!m_file->get(c)) {
                        return false;
                    }
                } while (c != '\0');
            }
        }
        if (flags & 0x02) {  // FHCRC
            m_file->ignore(2);
        }
        return static_cast<bool>(*m_file);
    }

    std::unique_ptr<std::ifstream> m_file;
    mz_stream m_stream{};
    std::array<char, kBufferBytes> m_in{};
    std::array<char, kBufferBytes> m_out{};
    bool m_open = false;
    bool m_finished = false;
};

// One entry of a zip file, extracted as it is read.
class ZipEntryBuffer final : public std::streambuf {
public:
    explicit ZipEntryBuffer(std::unique_ptr<std::ifstream> file, uint64_t fileSize)
        : m_file(std::move(file))
    {
        mz_zip_zero_struct(&m_zip);
        m_zip.m_pRead = &ZipEntryBuffer::read;
        m_zip.m_pIO_opaque = this;
        m_zipOpen = mz_zip_reader_init(&m_zip, fileSize, 0);
    }
    ~ZipEntryBuffer() override
    {
        if (m_entry) {
            mz_zip_reader_extract_iter_free(m_entry);
        }
        if (m_zipOpen) {
            mz_zip_reader_end(&m_zip);
        }
    }
    ZipEntryBuffer(const ZipEntryBuffer&) = delete;
    ZipEntryBuffer& operator=(const ZipEntryBuffer&) = delete;

    // Opens the first ".txt" entry, or the first file; false when there is none.
    bool openEntry(std::string& name, uint64_t& size, std::string& error)
    {
        if (!m_zipOpen) {
            error = "the zip file cannot be read";
            return false;
        }
        const mz_uint count = mz_zip_reader_get_num_files(&m_zip);
        std::optional<mz_uint> chosen;
        for (mz_uint index = 0; index < count; ++index) {
            if (mz_zip_reader_is_file_a_directory(&m_zip, index)) {
                continue;
            }
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&m_zip, index, &stat)) {
                continue;
            }
            std::string entry = stat.m_filename;
            std::string lower = entry;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool text = lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".txt") == 0;
            if (!chosen || text) {
                chosen = index;
                name = entry;
                size = stat.m_uncomp_size;
            }
            if (text) {
                break;
            }
        }
        if (!chosen) {
            error = "the zip file holds no dump";
            return false;
        }
        m_entry = mz_zip_reader_extract_iter_new(&m_zip, *chosen, 0);
        if (!m_entry) {
            error = "the dump in the zip file cannot be read";
            return false;
        }
        return true;
    }

protected:
    int_type underflow() override
    {
        if (gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        if (!m_entry) {
            return traits_type::eof();
        }
        const std::size_t produced =
            mz_zip_reader_extract_iter_read(m_entry, m_out.data(), m_out.size());
        if (produced == 0) {
            return traits_type::eof();
        }
        setg(m_out.data(), m_out.data(), m_out.data() + produced);
        return traits_type::to_int_type(*gptr());
    }

private:
    static size_t read(void* opaque, mz_uint64 offset, void* buffer, size_t bytes)
    {
        auto* self = static_cast<ZipEntryBuffer*>(opaque);
        self->m_file->clear();
        self->m_file->seekg(static_cast<std::streamoff>(offset));
        self->m_file->read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
        return static_cast<size_t>(self->m_file->gcount());
    }

    std::unique_ptr<std::ifstream> m_file;
    mz_zip_archive m_zip{};
    bool m_zipOpen = false;
    mz_zip_reader_extract_iter_state* m_entry = nullptr;
    std::array<char, kBufferBytes> m_out{};
};

}  // namespace

DumpFile openDumpFile(const std::string& path)
{
    DumpFile result;
    auto file = std::make_unique<std::ifstream>(platform::pathFromUtf8(path), std::ios::binary);
    if (!*file) {
        result.error = "the file cannot be opened";
        return result;
    }
    file->seekg(0, std::ios::end);
    const std::streamoff fileSize = file->tellg();
    file->seekg(0, std::ios::beg);
    unsigned char magic[4] = {};
    file->read(reinterpret_cast<char*>(magic), 4);
    const std::streamsize got = file->gcount();
    file->clear();
    file->seekg(0, std::ios::beg);

    if (got >= 2 && magic[0] == 0x1f && magic[1] == 0x8b) {
        // The uncompressed size, modulo 2^32, closes the file.
        if (fileSize >= 18) {
            unsigned char trailer[4] = {};
            file->seekg(fileSize - 4);
            file->read(reinterpret_cast<char*>(trailer), 4);
            result.sizeBytes = static_cast<uint64_t>(trailer[0]) |
                               (static_cast<uint64_t>(trailer[1]) << 8) |
                               (static_cast<uint64_t>(trailer[2]) << 16) |
                               (static_cast<uint64_t>(trailer[3]) << 24);
            file->clear();
            file->seekg(0, std::ios::beg);
        }
        auto buffer = std::make_unique<GzipBuffer>(std::move(file), result.error);
        if (!buffer->isOpen()) {
            return result;
        }
        result.stream = std::make_unique<OwningStream>(std::move(buffer));
        return result;
    }
    // A local file header, or the end record of a zip without entries.
    if (got == 4 && magic[0] == 'P' && magic[1] == 'K' &&
        ((magic[2] == 3 && magic[3] == 4) || (magic[2] == 5 && magic[3] == 6))) {
        auto buffer =
            std::make_unique<ZipEntryBuffer>(std::move(file), static_cast<uint64_t>(fileSize));
        if (!buffer->openEntry(result.entryName, result.sizeBytes, result.error)) {
            return result;
        }
        result.stream = std::make_unique<OwningStream>(std::move(buffer));
        return result;
    }
    result.sizeBytes = fileSize > 0 ? static_cast<uint64_t>(fileSize) : 0;
    result.stream = std::move(file);
    return result;
}

}  // namespace colorizing::importing
