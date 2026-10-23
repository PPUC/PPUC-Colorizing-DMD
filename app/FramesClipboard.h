#pragma once

#include <QByteArray>
#include <QString>

#include <optional>
#include <string>

#include "document/ProjectSnapshot.h"

namespace colorizing::app {

// Frames on the clipboard, for another editor window (Edit -> Copy Frames, Paste Frames): a
// package (edit/PackageImport.h) as the bytes of its file, under a type of its own, so that
// another program and the pixel Copy do not mix with it.
inline const QString kPackageMimeType =
    QStringLiteral("application/x-ppuc-colorizing-dmd-package");

// Puts the package on the clipboard; false with errorMessage when it could not be written.
[[nodiscard]] bool copyPackageToClipboard(const document::ProjectSnapshot& package,
                                          std::string& errorMessage);
// The package file's bytes on the clipboard, if there are any.
[[nodiscard]] std::optional<QByteArray> packageOnClipboard();
// The package on the clipboard, read; nothing (with errorMessage) when there is none or it is
// damaged.
[[nodiscard]] std::optional<document::ProjectSnapshot> readPackageFromClipboard(
    std::string& errorMessage);

}  // namespace colorizing::app
