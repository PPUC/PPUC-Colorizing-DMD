#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace colorizing::document {

// The license a colorization is published under (File -> Project Settings). The numbers are
// stored in the .cRPX (chunk LICN); new kinds are added at the end.
enum class LicenseKind : uint8_t {
    // Not chosen yet.
    Unspecified = 0,
    AllRightsReserved = 1,
    // The Creative Commons licenses, version 4.0 (CC0 1.0).
    Cc0 = 2,
    CcBy = 3,
    CcBySa = 4,
    CcByNc = 5,
    CcByNcSa = 6,
    // The terms are the project's own text (ProjectLicense::customText).
    Custom = 7,
    // The Creative Commons licenses that allow no changes, version 4.0: the ones the editor
    // offers (the kinds above that allow changes are kept for the projects that have them).
    CcByNd = 8,
    CcByNcNd = 9,
    // The PPUC Colorization License 1.0: the editor's own terms, put together from the
    // project's choices (ProjectLicense::commercialWithPpucDmd, realMachinesOnlyPpucDmd).
    PpucColorization = 10,
};

inline constexpr uint8_t kLastLicenseKind =
    static_cast<uint8_t>(LicenseKind::PpucColorization);

// Whether the license lets others change the colorization: the kinds the editor no longer
// offers.
[[nodiscard]] bool licenseAllowsChanges(LicenseKind kind);

struct ProjectLicense {
    LicenseKind kind = LicenseKind::Unspecified;
    // The terms of a custom license; kept, but without meaning, for the other kinds.
    std::string customText;
    // Who to name when the colorization is used, e.g. its authors.
    std::string attribution;
    // Where the colorization or its license can be found; may be empty.
    std::string url;
    // How to reach the author, for example an e-mail address, a Discord name or a link; may be
    // empty. The cROMc names it (colorizationText()).
    std::string contact;
    // With CcByNcNd (as an additional permission, "CC+") and PpucColorization: commercial use
    // is allowed on a real pinball machine with a PPUC DMD (ppuc.org), with no other display.
    bool commercialWithPpucDmd = false;
    // With PpucColorization: on a real pinball machine the colorization may be used with a
    // PPUC DMD only, also when not commercially.
    bool realMachinesOnlyPpucDmd = false;

    // Whether anything is set: a project without it writes no license.
    [[nodiscard]] bool isSet() const
    {
        return kind != LicenseKind::Unspecified || !customText.empty() || !attribution.empty() ||
               !url.empty() || !contact.empty() || commercialWithPpucDmd ||
               realMachinesOnlyPpucDmd;
    }

    bool operator==(const ProjectLicense&) const = default;
};

// The license's usual short name ("CC BY-SA 4.0"); empty for Unspecified and Custom, which
// have none. Not translated: these are the names the licenses go by.
[[nodiscard]] std::string licenseShortName(LicenseKind kind);
// The address of a Creative Commons license's deed, or of PPUC; empty for the other kinds.
[[nodiscard]] std::string licenseDeedUrl(LicenseKind kind);

// What the license allows and forbids, in English sentences, one per entry: what the cROMc
// carries for anyone who opens it (colorizationText()). Not translated.
[[nodiscard]] std::vector<std::string> licenseTerms(const ProjectLicense& license);

// The text the exported cROMc keeps readable in its header (libserum's
// Serum_GetColorizationText()): the ROM, the author (the license's attribution; "not named"
// without one) and how to contact them, the license
// and its terms, that the file must not be changed nor its signature removed, and that changes
// are made together with the author in the Serum community. Plain text with "Key: value"
// lines first, for people and for tools, AI tools among them: it states facts about the work
// and gives tools no orders, which they take for injected instructions. Not translated.
// contributors: the others who worked on it (team members, the authors of changed components),
// named in a line of their own.
[[nodiscard]] std::string colorizationText(const std::string& romName,
                                           const ProjectLicense& license,
                                           const std::vector<std::string>& contributors = {});

}  // namespace colorizing::document
