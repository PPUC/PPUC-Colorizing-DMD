#include "KeyNames.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace colorizing::app {

QString nativeShortcut(const QString& portable)
{
    return QKeySequence(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

QString ctrlKeyName()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Cmd");
#else
    //: The Control key as its keyboards name it ("Strg" in German)
    return QCoreApplication::translate("KeyNames", "Ctrl");
#endif
}

QString altKeyName()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Option");
#else
    //: The Alt key as its keyboards name it
    return QCoreApplication::translate("KeyNames", "Alt");
#endif
}

}  // namespace colorizing::app
