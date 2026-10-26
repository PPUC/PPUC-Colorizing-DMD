#include "legacy/LegacyCrzArchive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <memory>

#include <miniz/miniz.h>
#include <serum-decode.h>

#include "platform/Utf8Path.h"

namespace colorizing::legacy {

namespace {

std::string libserumError(const char* fallback)
{
    const char* message = Serum_GetLastErrorMessage();
    return message != nullptr && message[0] != '\0' ? std::string(message)
                                                    : std::string(fallback);
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
    uint8_t* data = nullptr;
    std::size_t size = 0;
    if (!Serum_CRZ_Read(crzPath.c_str(), &data, &size)) {
        errorMessage = libserumError("could not read .cRZ archive");
        return false;
    }
    try {
        cromBytes.assign(data, data + size);
    } catch (...) {
        Serum_CRZ_Free(data);
        throw;
    }
    Serum_CRZ_Free(data);
    return true;
}

CrzContent::~CrzContent()
{
    release();
}

void CrzContent::release()
{
    if (m_data) {
        Serum_CRZ_Free(m_data);
    }
    m_data = nullptr;
    m_size = 0;
}

bool readCrzArchive(const std::string& crzPath, CrzContent& content, std::string& errorMessage)
{
    content.release();
    uint8_t* data = nullptr;
    std::size_t size = 0;
    if (!Serum_CRZ_Read(crzPath.c_str(), &data, &size)) {
        errorMessage = libserumError("could not read .cRZ archive");
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
    std::array<char, 512> name{};
    if (!Serum_CRZ_GetEntryName(crzPath.c_str(), name.data(), name.size())) {
        errorMessage = libserumError("could not read .cRZ archive");
        return false;
    }
    entryName = name.data();
    return true;
}

}  // namespace colorizing::legacy
