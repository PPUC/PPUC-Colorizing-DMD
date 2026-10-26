#pragma once

#include <QString>

namespace colorizing::app {

// The folder each kind of file was last opened from (frame dumps, images, movie clips,
// packages, projects), so that its file dialog starts there next time, also after a restart.
// Kept in QSettings ("lastFolders/<kind>").
namespace RecentFolders {

inline const QString kProjects = QStringLiteral("projects");
inline const QString kImages = QStringLiteral("images");
inline const QString kMovieClips = QStringLiteral("movieClips");
inline const QString kFrameDumps = QStringLiteral("frameDumps");
inline const QString kPackages = QStringLiteral("packages");

// The folder of the kind; fallback while none is stored or the stored one is gone.
[[nodiscard]] QString folder(const QString& kind, const QString& fallback);
// Notes the folder of the file chosen; an empty path (a dialog cancelled) changes nothing.
void remember(const QString& kind, const QString& chosenFile);

}  // namespace RecentFolders

}  // namespace colorizing::app
