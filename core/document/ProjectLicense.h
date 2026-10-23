#pragma once

#include <cstdint>
#include <string>

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
};

inline constexpr uint8_t kLastLicenseKind = static_cast<uint8_t>(LicenseKind::Custom);

struct ProjectLicense {
    LicenseKind kind = LicenseKind::Unspecified;
    // The terms of a custom license; kept, but without meaning, for the other kinds.
    std::string customText;
    // Who to name when the colorization is used, e.g. its authors.
    std::string attribution;
    // Where the colorization or its license can be found; may be empty.
    std::string url;

    // Whether anything is set: a project without it writes no license.
    [[nodiscard]] bool isSet() const
    {
        return kind != LicenseKind::Unspecified || !customText.empty() || !attribution.empty() ||
               !url.empty();
    }

    bool operator==(const ProjectLicense&) const = default;
};

// The license's usual short name ("CC BY-SA 4.0"); empty for Unspecified and Custom, which
// have none. Not translated: these are the names the licenses go by.
[[nodiscard]] std::string licenseShortName(LicenseKind kind);
// The address of a Creative Commons license's deed; empty for the other kinds.
[[nodiscard]] std::string licenseDeedUrl(LicenseKind kind);

}  // namespace colorizing::document
