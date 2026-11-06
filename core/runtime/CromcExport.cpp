#include "runtime/CromcExport.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
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
                              legacy::LegacyIoProgress* progress,
                              const CromcSigning& signing,
                              const std::vector<std::string>& contributors)
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

    // The runtime never looks at what no frame names; the file is smaller without it.
    result.dropped = edit::dropUnusedAssets(exported);

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
        const std::string text =
            document::colorizationText(exported.romName, exported.license, contributors);
        SnapshotSource source(std::move(exported), &text);
        const std::optional<std::string> path = libserumPath(*work);
        Serum_Signing_Key key{};
        const bool withKey = signing.seed && !signing.certificate.empty();
        if (withKey) {
            key.seed = signing.seed->data();
            key.certificate = signing.certificate.c_str();
        }
        written = path && source.writeConcentrate(*path + "/" + rom + ".cROMc",
                                                  withKey ? &key : nullptr);
        result.isSigned = withKey;
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

    if (signing.modifyAfterSigning) {
        if (!markCromcModified(generated)) {
            result.error = "cannot change the cROMc after signing it";
            return result;
        }
        result.modified = true;
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

bool markCromcModified(const fs::path& cromc)
{
    std::fstream file(cromc, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    // A signed file ends with the block's length and "SSIG" (libserum's SerumSigning.h).
    char tail[8] = {};
    if (size >= 8) {
        file.seekg(size - 8);
        file.read(tail, 8);
    }
    if (size >= 84 && std::string_view(tail + 4, 4) == "SSIG") {
        // A byte of the signature, 64 bytes before the block's end; the colorization in front
        // of it stays loadable.
        const std::streamoff at = size - 8 - 20;
        char byte = 0;
        file.seekg(at);
        file.read(&byte, 1);
        byte = static_cast<char>(byte ^ 0x5a);
        file.seekp(at);
        file.write(&byte, 1);
        return static_cast<bool>(file);
    }
    // No signature: a block with no certificate and an empty signature.
    std::string block = "SSIG";
    block += std::string("\x01\x00\x00\x00", 4);
    block += std::string(4, '\0');  // no certificate chain
    block += std::string(64, '\0');
    const uint32_t length = static_cast<uint32_t>(block.size() + 8);
    for (int i = 0; i < 4; ++i) {
        block += static_cast<char>((length >> (8 * i)) & 0xff);
    }
    block += "SSIG";
    file.seekp(0, std::ios::end);
    file.write(block.data(), static_cast<std::streamsize>(block.size()));
    return static_cast<bool>(file);
}

namespace {

SignatureCheck checkOf(bool called, const Serum_Authenticity& authenticity)
{
    SignatureCheck check;
    if (!called) {
        check.status = SignatureCheck::Status::Modified;
        check.reason = "not checked";
        return check;
    }
    check.status = authenticity.status == SERUM_AUTHENTICITY_SIGNED ? SignatureCheck::Status::Signed
                   : authenticity.status == SERUM_AUTHENTICITY_MODIFIED
                       ? SignatureCheck::Status::Modified
                       : SignatureCheck::Status::Unsigned;
    check.name = authenticity.name;
    check.identity = authenticity.identity;
    check.reason = authenticity.reason;
    return check;
}

}  // namespace

std::array<uint8_t, 32> signingPublicKey(const std::array<uint8_t, 32>& seed)
{
    std::array<uint8_t, 32> key{};
    (void)Serum_Signing_PublicKey(seed.data(), key.data());
    return key;
}

SignatureCheck checkSigningCertificate(const std::string& certificate,
                                       const std::optional<std::array<uint8_t, 32>>& seed)
{
    Serum_Authenticity authenticity{};
    authenticity.size = sizeof(authenticity);
    std::array<uint8_t, 32> key{};
    if (seed) {
        key = signingPublicKey(*seed);
    }
    Serum_Signing_CheckCertificate(certificate.c_str(), seed ? key.data() : nullptr,
                                   &authenticity);
    return checkOf(true, authenticity);
}

SignatureCheck checkCromcSignature(const fs::path& cromc)
{
    Serum_Authenticity authenticity{};
    authenticity.size = sizeof(authenticity);
    const std::optional<std::string> path = libserumPath(cromc);
    const bool called =
        path && Serum_CheckConcentrateSignature(path->c_str(), &authenticity);
    return checkOf(called, authenticity);
}

}  // namespace colorizing::runtime
