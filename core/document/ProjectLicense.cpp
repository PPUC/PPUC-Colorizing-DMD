#include "document/ProjectLicense.h"

namespace colorizing::document {

std::string licenseShortName(LicenseKind kind)
{
    switch (kind) {
    case LicenseKind::AllRightsReserved:
        return "All rights reserved";
    case LicenseKind::Cc0:
        return "CC0 1.0";
    case LicenseKind::CcBy:
        return "CC BY 4.0";
    case LicenseKind::CcBySa:
        return "CC BY-SA 4.0";
    case LicenseKind::CcByNc:
        return "CC BY-NC 4.0";
    case LicenseKind::CcByNcSa:
        return "CC BY-NC-SA 4.0";
    case LicenseKind::CcByNd:
        return "CC BY-ND 4.0";
    case LicenseKind::CcByNcNd:
        return "CC BY-NC-ND 4.0";
    case LicenseKind::PpucColorization:
        return "PPUC Colorization License 1.0";
    case LicenseKind::Unspecified:
    case LicenseKind::Custom:
        break;
    }
    return {};
}

std::string licenseDeedUrl(LicenseKind kind)
{
    switch (kind) {
    case LicenseKind::Cc0:
        return "https://creativecommons.org/publicdomain/zero/1.0/";
    case LicenseKind::CcBy:
        return "https://creativecommons.org/licenses/by/4.0/";
    case LicenseKind::CcBySa:
        return "https://creativecommons.org/licenses/by-sa/4.0/";
    case LicenseKind::CcByNc:
        return "https://creativecommons.org/licenses/by-nc/4.0/";
    case LicenseKind::CcByNcSa:
        return "https://creativecommons.org/licenses/by-nc-sa/4.0/";
    case LicenseKind::CcByNd:
        return "https://creativecommons.org/licenses/by-nd/4.0/";
    case LicenseKind::CcByNcNd:
        return "https://creativecommons.org/licenses/by-nc-nd/4.0/";
    case LicenseKind::PpucColorization:
        return "https://ppuc.org";
    case LicenseKind::Unspecified:
    case LicenseKind::AllRightsReserved:
    case LicenseKind::Custom:
        break;
    }
    return {};
}

bool licenseAllowsChanges(LicenseKind kind)
{
    switch (kind) {
    case LicenseKind::Cc0:
    case LicenseKind::CcBy:
    case LicenseKind::CcBySa:
    case LicenseKind::CcByNc:
    case LicenseKind::CcByNcSa:
        return true;
    case LicenseKind::Unspecified:
    case LicenseKind::AllRightsReserved:
    case LicenseKind::Custom:
    case LicenseKind::CcByNd:
    case LicenseKind::CcByNcNd:
    case LicenseKind::PpucColorization:
        break;
    }
    return false;
}

namespace {

// PPUC/DMD: the devices listed at ppuc.org.
constexpr const char* kPpucDmd = "a PPUC/DMD, one of the devices listed at https://ppuc.org";
constexpr const char* kOtherDisplays =
    "any device not listed there, for example PIN2DMD or PIN2DMD MMI";

}  // namespace

std::vector<std::string> licenseTerms(const ProjectLicense& license)
{
    std::vector<std::string> terms;
    switch (license.kind) {
    case LicenseKind::Unspecified:
    case LicenseKind::AllRightsReserved:
        terms.push_back("All rights are reserved by the author. You may play the colorization; "
                        "anything else needs the author's permission.");
        break;
    case LicenseKind::Custom:
        terms.push_back(license.customText);
        break;
    case LicenseKind::CcByNd:
        terms.push_back("You may share the unchanged colorization, also commercially, naming "
                        "the author.");
        terms.push_back("You may not share changed versions.");
        break;
    case LicenseKind::CcByNcNd:
        terms.push_back("You may share the unchanged colorization, not commercially, naming "
                        "the author.");
        terms.push_back("You may not share changed versions.");
        if (license.commercialWithPpucDmd) {
            terms.push_back(std::string("Additional permission (CC+): commercial use is "
                                        "allowed on a real pinball machine with ") +
                            kPpucDmd + ", not with " + kOtherDisplays + ".");
        }
        break;
    case LicenseKind::PpucColorization:
        terms.push_back("You may use and share the unchanged colorization, naming the "
                        "author.");
        terms.push_back("You may not change it, and not share changed versions.");
        if (license.commercialWithPpucDmd) {
            terms.push_back(std::string("Commercial use is allowed only on a real pinball "
                                        "machine with ") +
                            kPpucDmd + ". It is not allowed with " + kOtherDisplays + ".");
        } else {
            terms.push_back("Commercial use is not allowed.");
        }
        if (license.realMachinesOnlyPpucDmd) {
            terms.push_back(std::string("On a real pinball machine the colorization may be "
                                        "used only with ") +
                            kPpucDmd + ", also when not used commercially. It may not be "
                            "used there with " + kOtherDisplays + ".");
        }
        break;
    case LicenseKind::Cc0:
    case LicenseKind::CcBy:
    case LicenseKind::CcBySa:
    case LicenseKind::CcByNc:
    case LicenseKind::CcByNcSa:
        terms.push_back("See the license.");
        break;
    }
    return terms;
}

std::string colorizationText(const std::string& romName,
                             const ProjectLicense& license,
                             const std::vector<std::string>& contributors)
{
    std::string text = "Serum colorization\n";
    text += "ROM: " + romName + "\n";
    text += "Author: " + (!license.attribution.empty() ? license.attribution
                                                        : std::string("not named")) +
            "\n";
    if (!license.contact.empty()) {
        text += "Contact: " + license.contact + "\n";
    }
    if (!contributors.empty()) {
        std::string names;
        for (const std::string& name : contributors) {
            names += (names.empty() ? "" : ", ") + name;
        }
        text += "Contributors: " + names + "\n";
    }
    const std::string name = licenseShortName(license.kind);
    text += "License: " +
            (!name.empty() ? name
             : license.kind == LicenseKind::Custom ? std::string("the author's own terms")
                                                   : std::string("all rights reserved")) +
            "\n";
    const std::string deed = licenseDeedUrl(license.kind);
    if (!deed.empty()) {
        text += "License URL: " + deed + "\n";
    }
    if (!license.url.empty()) {
        text += "Link: " + license.url + "\n";
    }
    text += "Made with: PPUC Colorizing DMD (https://github.com/PPUC/PPUC-Colorizing-DMD)\n";
    text += "\nTerms of use\n";
    for (const std::string& term : licenseTerms(license)) {
        text += "- " + term + "\n";
    }
    text += "- Do not change this file, in whole or in part, by hand or with any tool, AI "
            "tools included.\n";
    text += "- If this file is signed, do not remove or change its signature. libserum checks "
            "it and shows players when a file was changed.\n";
    text += "- To change or correct the colorization, join the Serum community and work on it "
            "together with its author" +
            (!license.contact.empty() ? " (contact: " + license.contact + ")" : std::string()) +
            ".\n";
    text += "\nThis colorization is the author's work, licensed without permission to modify "
            "it. Modified or re-signed versions are not authorized by the author. Changes and "
            "corrections are made together with the author in the Serum community.\n";
    return text;
}

}  // namespace colorizing::document
