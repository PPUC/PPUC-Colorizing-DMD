#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectSnapshot.h"
#include "edit/AssetUse.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::runtime {

// The version of the libserum the editor is built with, e.g. "2.8.2".
[[nodiscard]] std::string libserumVersion();

// The file of the folder with that name in any case, as libserum looks for its sidecars
// ("<rom>.pup.csv", "scaling.txt"); nothing without one.
[[nodiscard]] std::optional<std::filesystem::path> findCaseInsensitive(
    const std::filesystem::path& directory,
    const std::string& name);
// A path as libserum's char paths take it: on Windows in the ANSI code page, by its short name
// for other characters; nothing if it cannot be given. The file or folder must exist.
[[nodiscard]] std::optional<std::string> libserumPath(const std::filesystem::path& path);

// How the exported cROMc is signed (libserum's Serum_ConcentrateFromSourceSigned()).
struct CromcSigning {
    // The author's secret key and the certificate for its public key (text form); a seed
    // without a certificate signs nothing.
    std::optional<std::array<uint8_t, 32>> seed;
    std::string certificate;
    // For testing what players see: the file is changed after it is signed (a byte of the
    // signature), so that libserum takes it for a modified one. Without a key it gets a
    // signature block no key made.
    bool modifyAfterSigning = false;
};

struct CromcExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    // The files of sidecarDirectory that went into the cROMc ("<rom>.pup.csv", "scaling.txt").
    std::vector<std::string> sidecars;
    // The comparison masks, backgrounds and sprites no frame uses, left out of the cROMc.
    edit::DroppedAssets dropped;
    // Whether the cROMc is signed, and changed after (CromcSigning::modifyAfterSigning).
    bool isSigned = false;
    bool modified = false;
};

// Writes the project's .cROMc to target as libserum makes it on a machine: the project is
// written as <rom>.cROM into <workDirectory>/<rom>/ (an altcolor folder of its own) with the
// <rom>.pup.csv and scaling.txt of sidecarDirectory, if it has them (names in any case), and
// libserum loads it with cROMc generation on. sidecarDirectory may be empty for none. A
// project with colorization settings (document/Scene.h) carries its scenes and scaling
// settings in the cROM, and its sidecars are not taken. The comparison masks, backgrounds and
// sprites no frame uses are left out (edit::dropUnusedAssets()), the others renumbered.
//
// target is replaced only when everything worked. workDirectory is created and removed again.
// On Windows libserum opens files by ANSI paths: when it cannot open files in workDirectory
// (characters outside the code page, no short name), a folder of the same name in
// %ProgramData%\PPUC-Colorizing-DMD is used instead.
// libserum holds one colorization per process: this loads and releases one, so whatever
// RuntimePreview had loaded is gone (close it first; it loads again when asked). progress
// covers writing the cROM; libserum's part cannot be cancelled. With a key and certificate in
// signing the cROMc is signed.
[[nodiscard]] CromcExportResult exportCromc(const document::ProjectSnapshot& snapshot,
                                            const std::filesystem::path& workDirectory,
                                            const std::filesystem::path& sidecarDirectory,
                                            const std::filesystem::path& target,
                                            legacy::LegacyIoProgress* progress = nullptr,
                                            const CromcSigning& signing = {});

// The public key of a signing key's secret (libserum's Serum_Signing_PublicKey()).
[[nodiscard]] std::array<uint8_t, 32> signingPublicKey(const std::array<uint8_t, 32>& seed);

// What libserum says of a certificate or of a cROMc's signature.
struct SignatureCheck {
    enum class Status { Unsigned, Signed, Modified };
    Status status = Status::Unsigned;
    std::string name;
    std::string identity;
    std::string reason;
};
// A certificate (its text form), for the key of the seed or, without one, for any key: Signed
// when a trusted key vouches for it (Serum_Signing_CheckCertificate()), else Modified with the
// reason.
[[nodiscard]] SignatureCheck checkSigningCertificate(
    const std::string& certificate,
    const std::optional<std::array<uint8_t, 32>>& seed);
// The signature of a cROMc (Serum_CheckConcentrateSignature()).
[[nodiscard]] SignatureCheck checkCromcSignature(const std::filesystem::path& cromc);

// Changes the cROMc so that libserum takes it for modified: a byte of its signature, or, for an
// unsigned file, a signature block no key made is appended. False when the file cannot be
// changed.
[[nodiscard]] bool markCromcModified(const std::filesystem::path& cromc);

}  // namespace colorizing::runtime
