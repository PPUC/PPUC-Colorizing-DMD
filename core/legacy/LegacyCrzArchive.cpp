#include "legacy/LegacyCrzArchive.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

#include <miniz/miniz.h>

namespace colorizing::legacy {

namespace {

// An open archive, closed when it goes. miniz takes the path as UTF-8 on Windows and as the
// native bytes elsewhere, which is what the editor's paths are.
struct OpenArchive {
    mz_zip_archive zip{};
    bool open = false;

    explicit OpenArchive(const std::string& path)
    {
        mz_zip_zero_struct(&zip);
        open = mz_zip_reader_init_file(&zip, path.c_str(), 0) != 0;
    }
    ~OpenArchive()
    {
        if (open) {
            mz_zip_reader_end(&zip);
        }
    }
    OpenArchive(const OpenArchive&) = delete;
    OpenArchive& operator=(const OpenArchive&) = delete;

    [[nodiscard]] std::string lastError()
    {
        return mz_zip_get_error_string(mz_zip_get_last_error(&zip));
    }

    // The cROM is the first regular entry, as the Win32 editor and libserum took it. Entry
    // names are never used to find it: an archive is untrusted input.
    [[nodiscard]] bool findCrom(mz_uint& index, mz_zip_archive_file_stat& stat)
    {
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < count; ++i) {
            if (!mz_zip_reader_file_stat(&zip, i, &stat) ||
                mz_zip_reader_is_file_a_directory(&zip, i) ||
                !mz_zip_reader_is_file_supported(&zip, i) ||
                mz_zip_reader_is_file_encrypted(&zip, i)) {
                continue;
            }
            index = i;
            return true;
        }
        return false;
    }
};

// The cROM unpacked into a block of miniz's, released with mz_free(); nullptr on failure.
void* unpackCrom(const std::string& crzPath, std::size_t& size, std::string& errorMessage)
{
    OpenArchive archive(crzPath);
    if (!archive.open) {
        errorMessage = "could not open '" + crzPath + "' (" + archive.lastError() + ")";
        return nullptr;
    }
    mz_uint index = 0;
    mz_zip_archive_file_stat stat;
    if (!archive.findCrom(index, stat)) {
        errorMessage = "no cROM entry in '" + crzPath + "'";
        return nullptr;
    }
    void* data = mz_zip_reader_extract_to_heap(&archive.zip, index, &size, 0);
    if (!data) {
        errorMessage = "no cROM could be read from '" + crzPath + "' (" + archive.lastError() +
                       ")";
    }
    return data;
}

}  // namespace

bool hasCrzExtension(const std::string& path)
{
    static constexpr char kExtension[] = ".crz";
    constexpr std::size_t kLength = sizeof(kExtension) - 1;
    if (path.size() < kLength) {
        return false;
    }
    for (std::size_t i = 0; i < kLength; ++i) {
        const auto character = static_cast<unsigned char>(path[path.size() - kLength + i]);
        if (std::tolower(character) != kExtension[i]) {
            return false;
        }
    }
    return true;
}

std::string crzEntryNameFor(const std::string& romName)
{
    return romName + ".cRom";
}

bool readCrzArchive(const std::string& crzPath,
                    std::vector<uint8_t>& cromBytes,
                    std::string& errorMessage)
{
    std::size_t size = 0;
    auto* data = static_cast<uint8_t*>(unpackCrom(crzPath, size, errorMessage));
    if (!data) {
        return false;
    }
    try {
        cromBytes.assign(data, data + size);
    } catch (...) {
        mz_free(data);
        throw;
    }
    mz_free(data);
    return true;
}

CrzContent::~CrzContent()
{
    release();
}

void CrzContent::release()
{
    if (m_data) {
        mz_free(m_data);
    }
    m_data = nullptr;
    m_size = 0;
}

bool readCrzArchive(const std::string& crzPath, CrzContent& content, std::string& errorMessage)
{
    content.release();
    std::size_t size = 0;
    auto* data = static_cast<uint8_t*>(unpackCrom(crzPath, size, errorMessage));
    if (!data) {
        return false;
    }
    content.m_data = data;
    content.m_size = size;
    return true;
}

bool readCrzEntryName(const std::string& crzPath,
                      std::string& entryName,
                      std::string& errorMessage)
{
    OpenArchive archive(crzPath);
    if (!archive.open) {
        errorMessage = "could not open '" + crzPath + "' (" + archive.lastError() + ")";
        return false;
    }
    mz_uint index = 0;
    mz_zip_archive_file_stat stat;
    if (!archive.findCrom(index, stat)) {
        errorMessage = "no cROM entry in '" + crzPath + "'";
        return false;
    }
    entryName = stat.m_filename;
    return true;
}

}  // namespace colorizing::legacy
