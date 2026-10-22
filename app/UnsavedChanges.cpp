#include "UnsavedChanges.h"

#include <QCoreApplication>
#include <QStringList>

namespace colorizing::app {

bool mayCloseProject(bool hasUnsavedChanges,
                     const std::function<UnsavedChangesAnswer()>& ask,
                     const std::function<bool()>& save)
{
    if (!hasUnsavedChanges) {
        return true;
    }
    switch (ask()) {
        case UnsavedChangesAnswer::Save:
            return save();
        case UnsavedChangesAnswer::Discard:
            return true;
        case UnsavedChangesAnswer::Cancel:
            break;
    }
    return false;
}

QString discardedExtraPlaneWarning(const document::DiscardedExtraPlane& discarded)
{
    if (!discarded.any()) {
        return QString();
    }
    QStringList pictures;
    if (discarded.frames > 0) {
        pictures << QCoreApplication::translate("UnsavedChanges", "frames: %1")
                        .arg(discarded.frames);
    }
    if (discarded.backgrounds > 0) {
        pictures << QCoreApplication::translate("UnsavedChanges", "backgrounds: %1")
                        .arg(discarded.backgrounds);
    }
    if (discarded.sprites > 0) {
        pictures << QCoreApplication::translate("UnsavedChanges", "sprites: %1")
                        .arg(discarded.sprites);
    }
    return QCoreApplication::translate(
               "UnsavedChanges",
               "The project's files have 96x32 pictures (%1). The editor does not keep the "
               "96x32 plane of a 192x64 project, so saving writes the project without them. "
               "Until then the files keep them.")
        .arg(pictures.join(QStringLiteral(", ")));
}

}  // namespace colorizing::app
