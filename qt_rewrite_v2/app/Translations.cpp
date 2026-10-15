#include "Translations.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace colorizing::app {

namespace {

const QString kLanguageKey = QStringLiteral("appearance/language");

QString g_currentLanguage = QStringLiteral("en");

// Loads the file from the resources (see app/CMakeLists.txt) and installs it.
bool install(const QString& file)
{
    auto* translator = new QTranslator(QCoreApplication::instance());
    if (!translator->load(file, QStringLiteral(":/i18n"))) {
        delete translator;
        return false;
    }
    QCoreApplication::installTranslator(translator);
    return true;
}

}  // namespace

QStringList availableLanguages()
{
    return {QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("fr")};
}

QString languageName(const QString& language)
{
    const QLocale locale(language);
    QString name = locale.nativeLanguageName();
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

QString loadLanguage()
{
    const QString language = QSettings().value(kLanguageKey).toString();
    return availableLanguages().contains(language) ? language : QString();
}

void saveLanguage(const QString& language)
{
    if (language.isEmpty()) {
        QSettings().remove(kLanguageKey);
    } else {
        QSettings().setValue(kLanguageKey, language);
    }
}

void installTranslations()
{
    QString language = loadLanguage();
    if (language.isEmpty()) {
        // The first of the system's languages that there is; English otherwise.
        language = QStringLiteral("en");
        for (const QString& name : QLocale::system().uiLanguages()) {
            const QString code = name.left(2).toLower();
            if (availableLanguages().contains(code)) {
                language = code;
                break;
            }
        }
    }
    if (language == QStringLiteral("en")) {
        g_currentLanguage = language;
        return;
    }
    if (install(QStringLiteral("colorizingdmd_") + language)) {
        g_currentLanguage = language;
        // Qt's own texts; built in when the Qt used for the build has them.
        install(QStringLiteral("qtbase_") + language);
    }
}

QString currentLanguage()
{
    return g_currentLanguage;
}

}  // namespace colorizing::app
