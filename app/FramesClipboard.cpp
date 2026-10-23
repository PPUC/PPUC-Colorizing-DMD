#include "FramesClipboard.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QTemporaryDir>

#include "AppInfo.h"
#include "project/ProjectFile.h"

namespace colorizing::app {

// The zip code writes and reads files, so the package goes through a file of a temporary
// folder on its way to and from the clipboard.

bool copyPackageToClipboard(const document::ProjectSnapshot& package, std::string& errorMessage)
{
    QTemporaryDir directory;
    if (!directory.isValid()) {
        errorMessage = "no temporary folder for the frames";
        return false;
    }
    const QString path = directory.filePath(QStringLiteral("frames.cdmdpkg"));
    project::ProjectFileOptions options;
    options.editorVersion = kApplicationVersion.toStdString();
    options.package = true;
    const legacy::LegacySaveResult saved =
        project::saveProjectFile(path.toStdString(), package, options);
    if (!saved.ok) {
        errorMessage = saved.error.message;
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = "the frames could not be read back";
        return false;
    }
    auto* data = new QMimeData;
    data->setData(kPackageMimeType, file.readAll());
    QGuiApplication::clipboard()->setMimeData(data);
    return true;
}

std::optional<QByteArray> packageOnClipboard()
{
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasFormat(kPackageMimeType)) {
        return std::nullopt;
    }
    return data->data(kPackageMimeType);
}

std::optional<document::ProjectSnapshot> readPackageFromClipboard(std::string& errorMessage)
{
    const std::optional<QByteArray> bytes = packageOnClipboard();
    if (!bytes) {
        errorMessage = "the clipboard holds no frames";
        return std::nullopt;
    }
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("frames.cdmdpkg"));
    QFile file(path);
    if (!directory.isValid() || !file.open(QIODevice::WriteOnly) ||
        file.write(*bytes) != bytes->size()) {
        errorMessage = "no temporary folder for the frames";
        return std::nullopt;
    }
    file.close();
    legacy::LegacyLoadResult loaded = project::loadPackageFile(path.toStdString());
    if (!loaded.ok) {
        errorMessage = loaded.error.message;
        return std::nullopt;
    }
    return std::move(loaded.snapshot);
}

}  // namespace colorizing::app
