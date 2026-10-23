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
    case LicenseKind::Unspecified:
    case LicenseKind::AllRightsReserved:
    case LicenseKind::Custom:
        break;
    }
    return {};
}

}  // namespace colorizing::document
