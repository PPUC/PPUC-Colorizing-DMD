#include "runtime/CromcExport.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

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

// Where libserum can work: the folder asked for, or on Windows, when libserum cannot open that
// one, a folder of the same name in %ProgramData%\ColorizingDMD (ASCII, and users may create
// folders there). Created empty; the error says why none could be used.
std::optional<fs::path> makeWorkDirectory(const fs::path& requested, std::string& error)
{
    std::vector<fs::path> candidates{requested};
#ifdef _WIN32
    if (const wchar_t* programData = _wgetenv(L"ProgramData")) {
        candidates.push_back(fs::path(programData) / L"ColorizingDMD" / requested.filename());
    }
#endif
    for (const fs::path& candidate : candidates) {
        std::error_code code;
        fs::remove_all(candidate, code);
        if (!fs::create_directories(candidate, code)) {
            error = "cannot create " + platform::utf8FromPath(candidate) + ": " + code.message();
            continue;
        }
        if (libserumPath(candidate)) {
            return candidate;
        }
        fs::remove_all(candidate, code);
        error = "libserum cannot open files in " + platform::utf8FromPath(candidate) +
                ": its name has characters outside the system code page";
    }
    return std::nullopt;
}

void logLine(const char* format, va_list args, const void* userData)
{
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    auto* log = static_cast<std::vector<std::string>*>(const_cast<void*>(userData));
    log->emplace_back(buffer);
}

}  // namespace

std::optional<std::filesystem::path> findCaseInsensitive(const std::filesystem::path& directory,
                                                         const std::string& name)
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

// The path as libserum's char paths take it, or nothing if it cannot take it. On Windows
// those paths are in the ANSI code page: a folder with other characters (a user name in the
// temp path) is given by its short name, and without one (volumes can have short names off)
// it cannot be used. The file or folder must exist.
std::optional<std::string> libserumPath(const std::filesystem::path& directory)
{
#ifdef _WIN32
    std::wstring wide = directory.wstring();
    const DWORD length = GetShortPathNameW(wide.c_str(), nullptr, 0);
    if (length > 0) {
        std::wstring shortPath(length, L'\0');
        const DWORD written = GetShortPathNameW(wide.c_str(), shortPath.data(), length);
        if (written > 0 && written < length) {
            shortPath.resize(written);
            wide = shortPath;
        }
    }
    // With UTF-8 as the system code page every name can be given, and Windows refuses the
    // "lossy" check for it.
    const bool utf8 = GetACP() == CP_UTF8;
    const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL lossy = FALSE;
    const int bytes = WideCharToMultiByte(CP_ACP, flags, wide.c_str(),
                                          static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                          utf8 ? nullptr : &lossy);
    if (bytes <= 0 || lossy) {
        return std::nullopt;
    }
    std::string narrow(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_ACP, flags, wide.c_str(), static_cast<int>(wide.size()),
                        narrow.data(), bytes, nullptr, nullptr);
    return narrow;
#else
    return platform::utf8FromPath(directory);
#endif
}


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
    const std::optional<fs::path> work = makeWorkDirectory(workDirectory, result.error);
    if (!work) {
        return result;
    }
    struct Cleanup {
        fs::path directory;
        ~Cleanup()
        {
            std::error_code ignored;
            fs::remove_all(directory, ignored);
        }
    } cleanup{*work};
    const fs::path folder = *work / platform::pathFromUtf8(rom);
    if (!fs::create_directories(folder, error)) {
        result.error = "cannot create " + platform::utf8FromPath(folder) + ": " + error.message();
        return result;
    }
    result.error.clear();

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
        Serum_Load(libserumPath(*work).value_or(std::string()).c_str(), rom.c_str(), 0);
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
