#pragma once

#include <QString>

namespace colorizing::app {

// What the application is called wherever a person reads it (window titles, the About
// dialog, the macOS menu bar). The settings are kept under this name too (main.cpp).
inline const QString kApplicationName = QStringLiteral("PPUC Colorizing DMD");
// The version of the top-level CMakeLists.txt; raised before every push (3.99.x until 4.0.0).
inline const QString kApplicationVersion = QStringLiteral(COLORIZINGDMD_VERSION);
inline const QString kProjectUrl = QStringLiteral("https://github.com/PPUC/ColorizingDMD");
inline const QString kOrganizationUrl = QStringLiteral("https://ppuc.org");

}  // namespace colorizing::app
