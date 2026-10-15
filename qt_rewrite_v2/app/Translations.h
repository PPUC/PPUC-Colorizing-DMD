#pragma once

#include <QString>
#include <QStringList>

namespace colorizing::app {

// The languages of the user interface: English, the language of the sources, and the
// translations built in from translations/colorizingdmd_<language>.ts (Qt Linguist).

// The languages there are, as "en", "de", "fr"; English first.
[[nodiscard]] QStringList availableLanguages();
// A language's own name: "Deutsch" for "de".
[[nodiscard]] QString languageName(const QString& language);

// The language chosen in the settings; empty to follow the operating system (the default).
[[nodiscard]] QString loadLanguage();
void saveLanguage(const QString& language);

// Installs the translations of the chosen language (or the system's first one there is), the
// editor's and Qt's own (standard buttons and dialogs). Once, before the first window; another
// choice takes effect at the next start.
void installTranslations();
// The language installTranslations() chose: "en" before it ran or without a translation.
[[nodiscard]] QString currentLanguage();

}  // namespace colorizing::app
