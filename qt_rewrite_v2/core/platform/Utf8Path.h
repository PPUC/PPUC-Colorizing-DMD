#pragma once

#include <filesystem>
#include <string>

namespace colorizing::platform {

// File paths cross the core as UTF-8 in a std::string (QString::toStdString() is UTF-8, and
// libserum's Serum_CRZ_* functions take UTF-8). On Windows a std::string handed straight to
// std::ifstream, std::ofstream or std::filesystem::path is read in the ANSI code page instead,
// so a path with a character outside that code page (a user name, a folder name) would not be
// found. Every place of the core that opens, renames or removes a file builds its path with
// pathFromUtf8(). On macOS and Linux both functions change nothing.

[[nodiscard]] inline std::filesystem::path pathFromUtf8(const std::string& utf8)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return std::filesystem::u8path(utf8);
#endif
}

[[nodiscard]] inline std::string utf8FromPath(const std::filesystem::path& path)
{
#if defined(__cpp_char8_t)
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
#else
    return path.u8string();
#endif
}

}  // namespace colorizing::platform
