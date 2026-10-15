# Translations

The user interface is translated with Qt Linguist. Each language has one file here,
`colorizingdmd_<language>.ts`; the build compiles them and builds them into the editor
(see `app/CMakeLists.txt` and `app/Translations.cpp`). Settings -> Appearance -> Language
chooses the language, by default the operating system's.

## Workflow

1. New or changed texts in the sources: run lupdate through the build,

       cmake --build <build dir> --target update_translations

   It adds new texts to every `.ts` file as unfinished and marks texts that no longer exist
   as vanished.
2. Translate with Qt Linguist (`linguist translations/colorizingdmd_de.ts`) or in any text
   editor, and mark each finished text as done (remove `type="unfinished"`).
3. Build: the translations are compiled into the editor.

In the sources, texts shown to the user are wrapped in `tr()` (classes have
`Q_DECLARE_TR_FUNCTIONS(ClassName)`) or `QCoreApplication::translate("ClassName", ...)`
outside a class. Keys, file names and log output stay untranslated.

A new language: add its `.ts` file to `qt_add_translations` in `app/CMakeLists.txt`, its code
to `availableLanguages()` in `app/Translations.cpp`, and Qt's own `qtbase_<language>.qm` to
the list there; handbook chapters go to `handbook/<language>/`.

## Glossary

Keep these terms the same throughout a language. Terms of the VPinball / Serum world that
authors know in English stay English.

| English | Deutsch | Français |
| --- | --- | --- |
| frame | Frame | frame |
| original frame | Original-Frame | frame d'origine |
| colorized frame | kolorierter Frame | frame colorisée |
| dump | Dump | dump |
| project | Projekt | projet |
| section | Abschnitt | section |
| comparison mask | Vergleichsmaske | masque de comparaison |
| mask | Maske | masque |
| background | Hintergrund | arrière-plan |
| sprite | Sprite | sprite |
| dynamic mask / dynamic set | dynamische Maske / Dynamik-Set | masque dynamique / jeu dynamique |
| color rotation | Farbrotation | rotation de couleurs |
| color set | Farbset | jeu de couleurs |
| palette | Palette | palette |
| plane (bit plane) | Ebene | plan |
| canvas | Zeichenfläche | canevas |
| frame list / timeline | Frame-Liste | liste des frames |
| trigger | Trigger | déclencheur |
| hash | Hash | hash |
| detection area | Erkennungsbereich | zone de détection |
| selection | Auswahl | sélection |
| undo / redo | Rückgängig / Wiederholen | Annuler / Rétablir |
| zoom | Zoom | zoom |
| handbook | Handbuch | manuel |
| settings | Einstellungen | Réglages |
| DMD, cROM, cRP, cRPX, PNG, VPX, Serum | unchanged | unchanged |

German uses the formal "Sie" sparingly; prefer impersonal phrasing as in the English texts.
French uses "vous". Keep keyboard mnemonics (`&`) on letters of the translated word, unique
within a menu.
