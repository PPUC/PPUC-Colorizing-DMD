#include "AuthorName.h"

#include <QSettings>

namespace colorizing::app {

namespace {

const QString kAuthorNameKey = QStringLiteral("author/name");

}  // namespace

QString loadAuthorName()
{
    return QSettings().value(kAuthorNameKey).toString();
}

void saveAuthorName(const QString& name)
{
    QSettings().setValue(kAuthorNameKey, name.trimmed());
}

}  // namespace colorizing::app
