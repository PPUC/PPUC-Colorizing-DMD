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
#include "runtime/Scenes.h"
#include "runtime/SnapshotSource.h"
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
// one, a folder of the same name in %ProgramData%\PPUC-Colorizing-DMD (ASCII, and users may create
// folders there). Created empty; the error says why none could be used.
std::optional<fs::path> makeWorkDirectory(const fs::path& requested, std::string& error)
{
    std::vector<fs::path> candidates{requested};
#ifdef _WIN32
    if (const wchar_t* programData = _wgetenv(L"ProgramData")) {
        candidates.push_back(fs::path(programData) / L"PPUC-Colorizing-DMD" / requested.filename());
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

std::string libserumVersion()
{
    return Serum_GetVersion();
}

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
    result.error.clear();

    // A project without colorization settings (one of the Win32 editor) takes them from the
    // files beside it, as libserum would: the scenes of "<rom>.pup.csv" and "scaling.txt".
    document::ProjectSnapshot exported = snapshot;
    if (!exported.colorizationSettings && !sidecarDirectory.empty()) {
        const std::optional<fs::path> csv = findCaseInsensitive(sidecarDirectory, rom + ".pup.csv");
        const std::optional<fs::path> scaling =
            findCaseInsensitive(sidecarDirectory, "scaling.txt");
        if (csv || scaling) {
            document::ColorizationSettings settings;
            if (scaling) {
                if (const auto read = readScalingFile(*scaling)) {
                    settings = *read;
                }
                result.sidecars.push_back(platform::utf8FromPath(scaling->filename()));
            }
            if (csv) {
                if (const auto scenes = readSceneCsv(*csv)) {
                    settings.scenes = *scenes;
                }
                result.sidecars.push_back(platform::utf8FromPath(csv->filename()));
            }
            exported.colorizationSettings = std::move(settings);
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
    // No cROM: libserum builds the colorization from the project (Serum_ConcentrateFromSource())
    // and writes the cROMc into the work folder, whose path it can open on every platform.
    const fs::path generated = *work / platform::pathFromUtf8(rom + ".cROMc");
    std::vector<std::string> log;
    Serum_SetLogCallback(logLine, &log);
    bool written = false;
    std::string sourceError;
    {
        SnapshotSource source(std::move(exported));
        const std::optional<std::string> path = libserumPath(*work);
        written = path && source.writeConcentrate(*path + "/" + rom + ".cROMc");
        sourceError = source.error();
    }
    const char* lastError = Serum_GetLastErrorMessage();
    const std::string serumError = lastError ? lastError : "";
    Serum_SetLogCallback(nullptr, nullptr);
    if (!written || !sourceError.empty() || !fs::exists(generated, error)) {
        result.error = !sourceError.empty() ? sourceError
                       : !serumError.empty() ? serumError
                                             : "libserum wrote no cROMc";
        if (!log.empty()) {
            result.error += " (" + log.back() + ")";
        }
        return result;
    }

    // Replaced only once the new file is complete beside it.
    const fs::path partial = fs::path(target).concat(".saving");
    fs::remove(partial, error);
    if (!fs::copy_file(generated, partial, error)) {
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
