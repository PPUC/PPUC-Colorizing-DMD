#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace colorizing::project {

// Zip archives of the project file (ProjectFile.h), through miniz. Paths are UTF-8
// (platform/Utf8Path.h); the files are opened through std::filesystem, so that every name works
// on Windows too. Entry names are data and never used to build a path.

class ZipWriter {
public:
    // Creates (or truncates) the file.
    explicit ZipWriter(const std::string& path);
    ~ZipWriter();
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    [[nodiscard]] bool isOpen() const;
    // Deflated at the level (0 stores it, 1 is fast, 9 small). Switches to zip64 by itself.
    [[nodiscard]] bool add(const std::string& name, const std::string& bytes, int level);
    // Writes the central directory and closes the file.
    [[nodiscard]] bool finish();
    [[nodiscard]] const std::string& error() const { return m_error; }

private:
    struct State;
    std::unique_ptr<State> m_state;
    std::string m_error;
};

class ZipReader {
public:
    explicit ZipReader(const std::string& path);
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] std::vector<std::string> entryNames() const;
    [[nodiscard]] bool contains(const std::string& name) const;
    // The entry unpacked; nothing for an entry that is missing, damaged or larger than
    // maxBytes, error() then says which.
    [[nodiscard]] std::optional<std::string> read(const std::string& name,
                                                  std::size_t maxBytes = kMaxEntryBytes);
    [[nodiscard]] const std::string& error() const { return m_error; }

    static constexpr std::size_t kMaxEntryBytes = std::size_t{1} << 30;

private:
    struct State;
    std::unique_ptr<State> m_state;
    std::string m_error;
};

}  // namespace colorizing::project
