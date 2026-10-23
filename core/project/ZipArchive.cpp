#include "project/ZipArchive.h"

#include <fstream>

#include <miniz/miniz.h>

#include "platform/Utf8Path.h"

namespace colorizing::project {

struct ZipWriter::State {
    std::ofstream file;
    mz_zip_archive zip{};
    bool initialized = false;

    static size_t write(void* opaque, mz_uint64 offset, const void* data, size_t size)
    {
        auto* self = static_cast<State*>(opaque);
        if (static_cast<mz_uint64>(self->file.tellp()) != offset) {
            self->file.seekp(static_cast<std::streamoff>(offset));
        }
        self->file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return self->file.good() ? size : 0;
    }
};

ZipWriter::ZipWriter(const std::string& path)
    : m_state(std::make_unique<State>())
{
    m_state->file.open(platform::pathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!m_state->file) {
        m_error = "could not open the file for writing: " + path;
        return;
    }
    mz_zip_zero_struct(&m_state->zip);
    m_state->zip.m_pWrite = &State::write;
    m_state->zip.m_pIO_opaque = m_state.get();
    if (!mz_zip_writer_init_v2(&m_state->zip, 0, 0)) {
        m_error = "could not start the archive: " + path;
        return;
    }
    m_state->initialized = true;
}

ZipWriter::~ZipWriter()
{
    if (m_state->initialized) {
        mz_zip_writer_end(&m_state->zip);
    }
}

bool ZipWriter::isOpen() const
{
    return m_state->initialized;
}

bool ZipWriter::add(const std::string& name, const std::string& bytes, int level)
{
    if (!m_state->initialized) {
        return false;
    }
    if (!mz_zip_writer_add_mem(&m_state->zip, name.c_str(), bytes.data(), bytes.size(),
                               static_cast<mz_uint>(level))) {
        m_error = "could not write " + name + ": " +
                  mz_zip_get_error_string(mz_zip_get_last_error(&m_state->zip));
        return false;
    }
    return true;
}

bool ZipWriter::finish()
{
    if (!m_state->initialized) {
        return false;
    }
    const bool finalized = mz_zip_writer_finalize_archive(&m_state->zip);
    mz_zip_writer_end(&m_state->zip);
    m_state->initialized = false;
    m_state->file.close();
    if (!finalized || !m_state->file) {
        m_error = "could not finish the archive";
        return false;
    }
    return true;
}

struct ZipReader::State {
    std::ifstream file;
    mz_zip_archive zip{};
    bool initialized = false;

    static size_t read(void* opaque, mz_uint64 offset, void* data, size_t size)
    {
        auto* self = static_cast<State*>(opaque);
        self->file.clear();
        self->file.seekg(static_cast<std::streamoff>(offset));
        self->file.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        return static_cast<size_t>(self->file.gcount());
    }
};

ZipReader::ZipReader(const std::string& path)
    : m_state(std::make_unique<State>())
{
    m_state->file.open(platform::pathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!m_state->file) {
        m_error = "could not open the file: " + path;
        return;
    }
    const auto size = static_cast<mz_uint64>(m_state->file.tellg());
    mz_zip_zero_struct(&m_state->zip);
    m_state->zip.m_pRead = &State::read;
    m_state->zip.m_pIO_opaque = m_state.get();
    if (!mz_zip_reader_init(&m_state->zip, size, 0)) {
        m_error = "the file is no zip archive: " + path;
        return;
    }
    m_state->initialized = true;
}

ZipReader::~ZipReader()
{
    if (m_state->initialized) {
        mz_zip_reader_end(&m_state->zip);
    }
}

bool ZipReader::isOpen() const
{
    return m_state->initialized;
}

std::vector<std::string> ZipReader::entryNames() const
{
    std::vector<std::string> names;
    if (!m_state->initialized) {
        return names;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&m_state->zip);
    names.reserve(count);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&m_state->zip, index, &stat) && !stat.m_is_directory) {
            names.emplace_back(stat.m_filename);
        }
    }
    return names;
}

bool ZipReader::contains(const std::string& name) const
{
    return m_state->initialized &&
           mz_zip_reader_locate_file(&m_state->zip, name.c_str(), nullptr, 0) >= 0;
}

std::optional<std::string> ZipReader::read(const std::string& name, std::size_t maxBytes)
{
    if (!m_state->initialized) {
        return std::nullopt;
    }
    const int index = mz_zip_reader_locate_file(&m_state->zip, name.c_str(), nullptr, 0);
    mz_zip_archive_file_stat stat;
    if (index < 0 ||
        !mz_zip_reader_file_stat(&m_state->zip, static_cast<mz_uint>(index), &stat)) {
        m_error = "the archive has no entry " + name;
        return std::nullopt;
    }
    if (stat.m_uncomp_size > maxBytes) {
        m_error = "the entry " + name + " is too large";
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&m_state->zip, static_cast<mz_uint>(index), bytes.data(),
                                      bytes.size(), 0)) {
        m_error = "the entry " + name + " is damaged";
        return std::nullopt;
    }
    return bytes;
}

}  // namespace colorizing::project
