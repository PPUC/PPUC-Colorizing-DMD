#pragma once

#include <QString>

namespace colorizing::app {

// What the application is called wherever a person reads it (the About dialog, the macOS menu
// bar, and with the version the window titles). The settings are kept under this name too (main.cpp).
inline const QString kApplicationName = QStringLiteral("PPUC Colorizing DMD");
// The version of the top-level CMakeLists.txt; raised before every push (3.99.x until 4.0.0).
inline const QString kApplicationVersion = QStringLiteral(COLORIZINGDMD_VERSION);
// The name with the version, for the window titles: a screenshot of the main window shows which
// version the person runs. Qt appends the display name (main.cpp) to window titles on Windows
// and Linux unless a title already ends with it, so both use this one string.
inline const QString kApplicationTitle = kApplicationName + QLatin1Char(' ') + kApplicationVersion;
inline const QString kProjectUrl = QStringLiteral("https://github.com/PPUC/ColorizingDMD");
inline const QString kOrganizationUrl = QStringLiteral("https://ppuc.org");

}  // namespace colorizing::app
