#include "runtime/CromcExport.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <optional>
#include <system_error>

#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"
#include "serum-decode.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace colorizing::runtime {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::optional<fs::path> findCaseInsensitive(const fs::path& directory, const std::string& name)
{
    std::error_code error;
    if (directory.empty() || !fs::is_directory(directory, error)) {
        return std::nullopt;
    }
    const std::string wanted = lower(name);
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        if (entry.is_regular_file(error) &&
            lower(platform::utf8FromPath(entry.path().filename())) == wanted) {
            return entry.path();
        }
    }
    return std::nullopt;
}

// The folder as libserum's char paths take it. On Windows those are in the ANSI code page, so
// a folder with other characters (a user name in the temp path) is given by its short name.
std::string libserumPath(const fs::path& directory)
{
#ifdef _WIN32
    const std::wstring wide = directory.wstring();
    const DWORD length = GetShortPathNameW(wide.c_str(), nullptr, 0);
    if (length > 0) {
        std::wstring shortPath(length, L'\0');
        const DWORD written = GetShortPathNameW(wide.c_str(), shortPath.data(), length);
        if (written > 0 && written < length) {
            shortPath.resize(written);
            return fs::path(shortPath).string();
        }
    }
    return directory.string();
#else
    return platform::utf8FromPath(directory);
#endif
}

void logLine(const char* format, va_list args, const void* userData)
{
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    auto* log = static_cast<std::vector<std::string>*>(const_cast<void*>(userData));
    log->emplace_back(buffer);
}

}  // namespace

CromcExportResult exportCromc(const document::ProjectSnapshot& snapshot,
                              const fs::path& workDirectory,
                              const fs::path& sidecarDirectory,
                              const fs::path& target,
                              legacy::LegacyIoProgress* progress)
{
    CromcExportResult result;
    const std::string rom = snapshot.romName;
    if (rom.empty()) {
        result.error = "the project has no ROM name";
        return result;
    }
    std::error_code error;
    fs::remove_all(workDirectory, error);
    const fs::path folder = workDirectory / platform::pathFromUtf8(rom);
    if (!fs::create_directories(folder, error)) {
        result.error = "cannot create " + platform::utf8FromPath(folder) + ": " + error.message();
        return result;
    }
    struct Cleanup {
        fs::path directory;
        ~Cleanup()
        {
            std::error_code ignored;
            fs::remove_all(directory, ignored);
        }
    } cleanup{workDirectory};

    {
        std::ofstream crom(folder / platform::pathFromUtf8(rom + ".cROM"), std::ios::binary);
        if (!crom) {
            result.error = "cannot write the cROM into " + platform::utf8FromPath(folder);
            return result;
        }
        if (progress) {
            progress->total = legacy::legacyCromByteCount(snapshot);
            progress->setStage(legacy::LegacyIoStage::Writing);
        }
        std::string message;
        if (!legacy::writeLegacyCrom(crom, snapshot, message, nullptr, progress)) {
            result.cancelled = message == legacy::kLegacyIoCancelledMessage;
            result.error = message;
            return result;
        }
        crom.close();
        if (!crom) {
            result.error = "cannot write the cROM into " + platform::utf8FromPath(folder);
            return result;
        }
    }
    for (const std::string& name : {rom + ".pup.csv", std::string("scaling.txt")}) {
        if (const std::optional<fs::path> sidecar = findCaseInsensitive(sidecarDirectory, name)) {
            if (!fs::copy_file(*sidecar, folder / sidecar->filename(), error)) {
                result.error = "cannot copy " + platform::utf8FromPath(*sidecar) + ": " +
                               error.message();
                return result;
            }
            result.sidecars.push_back(platform::utf8FromPath(sidecar->filename()));
        }
    }

    if (progress) {
        if (progress->cancel.load()) {
            result.cancelled = true;
            result.error = legacy::kLegacyIoCancelledMessage;
            return result;
        }
        progress->setStage(legacy::LegacyIoStage::Generating);
    }
    std::vector<std::string> log;
    Serum_SetLogCallback(logLine, &log);
    Serum_SetGenerateCRomC(true);
    const Serum_Frame_Struc* loaded =
        Serum_Load(libserumPath(workDirectory).c_str(), rom.c_str(), 0);
    const char* lastError = Serum_GetLastErrorMessage();
    const std::string serumError = lastError ? lastError : "";
    Serum_Dispose();
    Serum_SetLogCallback(nullptr, nullptr);
    const std::optional<fs::path> generated = findCaseInsensitive(folder, rom + ".cROMc");
    if (!loaded || !generated) {
        result.error = !serumError.empty() ? serumError
                       : loaded            ? "libserum wrote no cROMc"
                                           : "libserum could not load the project";
        if (!log.empty()) {
            result.error += " (" + log.back() + ")";
        }
        return result;
    }

    // Replaced only once the new file is complete beside it.
    const fs::path partial = fs::path(target).concat(".saving");
    fs::remove(partial, error);
    if (!fs::copy_file(*generated, partial, error)) {
        result.error = "cannot write " + platform::utf8FromPath(partial) + ": " + error.message();
        return result;
    }
    fs::rename(partial, target, error);
    if (error) {
        fs::remove(partial, error);
        result.error = "cannot replace " + platform::utf8FromPath(target) + ": " + error.message();
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace colorizing::runtime
