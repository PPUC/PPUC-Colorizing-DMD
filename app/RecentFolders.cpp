#include "RecentFolders.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace colorizing::app::RecentFolders {

namespace {

QString key(const QString& kind)
{
    return QStringLiteral("lastFolders/") + kind;
}

}  // namespace

QString folder(const QString& kind, const QString& fallback)
{
    const QString stored = QSettings().value(key(kind)).toString();
    return !stored.isEmpty() && QDir(stored).exists() ? stored : fallback;
}

void remember(const QString& kind, const QString& chosenFile)
{
    if (chosenFile.isEmpty()) {
        return;
    }
    QSettings().setValue(key(kind), QFileInfo(chosenFile).absolutePath());
}

}  // namespace colorizing::app::RecentFolders
