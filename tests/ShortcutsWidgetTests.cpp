// The keyboard shortcuts the user can change: the keys kept in the settings, what can be given,
// and the Settings page.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>

#include "KeyboardShortcuts.h"
#include "dialogs/SettingsDialog.h"
#include "dialogs/ShortcutsSettingsPage.h"

namespace app = colorizing::app;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: shortcuts: " << message << '\n';
        std::exit(1);
    }
}

// File: Save (Ctrl+S); Edit: Copy (Ctrl+C), Select None (two keys), Grow -> Left (no key), and an
// entry without an ID, which is not offered.
struct Menus {
    QMenuBar bar;
    QAction* save = nullptr;
    QAction* copy = nullptr;
    QAction* selectNone = nullptr;
    QAction* left = nullptr;
    QAction* unnamed = nullptr;

    Menus()
    {
        QMenu* file = bar.addMenu(QStringLiteral("&File"));
        save = app::withShortcutId(file->addAction(QStringLiteral("&Save")), "save");
        save->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
        unnamed = file->addAction(QStringLiteral("recent.cdmd"));
        file->addSeparator();
        QMenu* edit = bar.addMenu(QStringLiteral("&Edit"));
        copy = app::withShortcutId(edit->addAction(QStringLiteral("&Copy")), "copy");
        copy->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_C));
        selectNone = app::withShortcutId(edit->addAction(QStringLiteral("Select &None")), "none");
        selectNone->setShortcuts(
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), QKeySequence(Qt::CTRL | Qt::Key_D)});
        left = app::withShortcutId(edit->addMenu(QStringLiteral("&Grow"))
                                       ->addAction(QStringLiteral("Left")),
                                   "grow-left");
    }
};

void testRules()
{
    const auto problem = [](const QKeySequence& key) {
        return app::shortcutKeyProblem(key, {}).has_value();
    };
    expect(!problem(QKeySequence(Qt::Key_F2)) && !problem(QKeySequence(Qt::SHIFT | Qt::Key_F12)),
           "function keys go without Ctrl or Alt");
    expect(!problem(QKeySequence(Qt::CTRL | Qt::Key_J)) &&
               !problem(QKeySequence(Qt::ALT | Qt::Key_J)),
           "any key goes with Ctrl or Alt");
    expect(problem(QKeySequence(Qt::Key_J)) && problem(QKeySequence(Qt::SHIFT | Qt::Key_J)) &&
               problem(QKeySequence(Qt::Key_Left)) && problem(QKeySequence(Qt::Key_Escape)) &&
               problem(QKeySequence(Qt::Key_Return)) && problem(QKeySequence(Qt::Key_Delete)),
           "a key alone belongs to the canvas and text fields");
    const QKeySequence stripKey(Qt::CTRL | Qt::Key_Left);
    expect(problem(stripKey), "the keys the frame strip handles itself cannot be given");
    expect(!app::shortcutKeyProblem(stripKey, {stripKey}),
           "an entry may always have its own default key");
    expect(!problem(QKeySequence()), "no key at all is always fine");
}

void testKeptInSettings()
{
    QSettings().clear();
    const QKeySequence f5(Qt::Key_F5);
    {
        Menus menus;
        app::KeyboardShortcuts shortcuts(&menus.bar);
        const std::vector<app::ShortcutEntry> entries = shortcuts.entries();
        expect(entries.size() == 4, "the entries with an ID, not the others or the separators");
        expect(entries[0].id == QStringLiteral("save") &&
                   entries[0].title == QStringLiteral("File -> Save"),
               "an entry is listed by its way through the menus");
        expect(entries[3].title == QStringLiteral("Edit -> Grow -> Left") &&
                   entries[3].current.isEmpty(),
               "also from a submenu, also without a key");

        std::vector<app::ShortcutEntry> changed = entries;
        changed[0].current = {f5};
        changed[1].current = {};
        shortcuts.apply(changed);
        expect(menus.save->shortcut() == f5 && menus.copy->shortcut().isEmpty(),
               "the actions get the new keys at once");
        expect(!QSettings().contains(QStringLiteral("shortcuts/none")),
               "only what differs from the defaults is kept");
    }
    {
        Menus menus;
        const app::KeyboardShortcuts shortcuts(&menus.bar);
        expect(menus.save->shortcut() == f5, "a chosen key is back at the next start");
        expect(menus.copy->shortcut().isEmpty(), "and so is an entry left without a key");
        expect(menus.selectNone->shortcuts().size() == 2, "the others keep all their default keys");
        expect(shortcuts.entries()[0].defaults ==
                   QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_S)},
               "the defaults are known whatever is set");
    }
    {
        // The default again removes what was kept.
        Menus menus;
        app::KeyboardShortcuts shortcuts(&menus.bar);
        std::vector<app::ShortcutEntry> entries = shortcuts.entries();
        for (app::ShortcutEntry& entry : entries) {
            entry.current = entry.defaults;
        }
        shortcuts.apply(entries);
        QSettings settings;
        settings.beginGroup(QStringLiteral("shortcuts"));
        expect(menus.copy->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_C) &&
                   settings.childKeys().isEmpty(),
               "back to the defaults, nothing stays in the settings");
    }
    {
        // A later version gives out a key someone had chosen: an ambiguous key would do nothing.
        QSettings().setValue(QStringLiteral("shortcuts/grow-left"), QStringLiteral("Ctrl+S"));
        Menus menus;
        const app::KeyboardShortcuts shortcuts(&menus.bar);
        expect(menus.left->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S) &&
                   menus.save->shortcut().isEmpty(),
               "a chosen key wins over a default that has it");
    }
    QSettings().clear();
}

void press(QWidget* field, int key, Qt::KeyboardModifiers modifiers, const QString& text = {})
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(field, &event);
}

void testPage()
{
    Menus menus;
    const app::KeyboardShortcuts shortcuts(&menus.bar);
    app::ShortcutsSettingsPage page;
    page.setEntries(shortcuts.entries());
    auto* list = page.findChild<QTreeWidget*>();
    auto* field = page.findChild<QLineEdit*>(QStringLiteral("shortcutKeyField"));
    auto* note = page.findChild<QLabel*>(QStringLiteral("shortcutNote"));
    expect(list && field && note && list->topLevelItemCount() == 4, "the page lists the entries");

    press(field, Qt::Key_F2, Qt::NoModifier);
    expect(page.entries()[0].current.size() == 1 &&
               page.entries()[0].current[0] == QKeySequence(Qt::CTRL | Qt::Key_S),
           "without a selected entry, a key changes nothing");

    list->setCurrentItem(list->topLevelItem(0));
    expect(field->text() == app::shortcutsText(page.entries()[0].current),
           "the field shows the selected entry's keys");
    press(field, Qt::Key_F2, Qt::NoModifier);
    expect(page.entries()[0].current == QList<QKeySequence>{QKeySequence(Qt::Key_F2)} &&
               list->topLevelItem(0)->text(1) ==
                   QKeySequence(Qt::Key_F2).toString(QKeySequence::NativeText),
           "a function key goes to the selected entry");
    expect(menus.save->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S),
           "the actions keep their keys until the settings are accepted");

    press(field, Qt::Key_Shift, Qt::ShiftModifier);
    press(field, Qt::Key_Control, Qt::ControlModifier);
    expect(page.entries()[0].current == QList<QKeySequence>{QKeySequence(Qt::Key_F2)} &&
               note->text().isEmpty(),
           "the modifiers alone are no key yet");

    press(field, Qt::Key_J, Qt::NoModifier, QStringLiteral("j"));
    expect(page.entries()[0].current == QList<QKeySequence>{QKeySequence(Qt::Key_F2)} &&
               !note->text().isEmpty(),
           "a key alone is refused, and the page says why");

    press(field, Qt::Key_C, Qt::ControlModifier);
    expect(page.entries()[0].current == QList<QKeySequence>{QKeySequence(Qt::Key_F2)} &&
               page.entries()[1].current ==
                   QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_C)} &&
               note->text().contains(QStringLiteral("Edit -> Copy")),
           "a key of another entry is not taken from it, and the page names that entry");

    press(field, Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("J"));
    expect(page.entries()[0].current ==
                   QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J)} &&
               note->text().isEmpty(),
           "Ctrl+Shift with a letter; a key that goes clears the note");

    // A shifted symbol is the key itself.
    press(field, Qt::Key_Exclam, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("!"));
    expect(page.entries()[0].current ==
               QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Exclam)},
           "Shift is left out where it only types the symbol");

    const auto button = [&page](const QString& text) {
        for (QToolButton* candidate : page.findChildren<QToolButton*>()) {
            if (candidate->text() == text) {
                return candidate;
            }
        }
        expect(false, "a button is missing");
        return static_cast<QToolButton*>(nullptr);
    };
    button(QStringLiteral("None"))->click();
    expect(page.entries()[0].current.isEmpty() && list->topLevelItem(0)->text(1).isEmpty(),
           "None leaves the entry without a key");
    button(QStringLiteral("Default"))->click();
    expect(page.entries()[0].current == page.entries()[0].defaults, "Default gives its key back");
    expect(!button(QStringLiteral("Default"))->isEnabled(), "and is not needed then");

    // Its default key went to another entry meanwhile: not two entries with one key.
    list->setCurrentItem(list->topLevelItem(1));
    button(QStringLiteral("None"))->click();
    list->setCurrentItem(list->topLevelItem(3));
    press(field, Qt::Key_C, Qt::ControlModifier);
    list->setCurrentItem(list->topLevelItem(1));
    button(QStringLiteral("Default"))->click();
    expect(page.entries()[1].current.isEmpty() && note->text().contains(QStringLiteral("Grow")),
           "Default does not take the key from the entry that has it now");

    button(QStringLiteral("Restore All Defaults"))->click();
    bool allDefaults = true;
    for (const app::ShortcutEntry& entry : page.entries()) {
        allDefaults = allDefaults && entry.current == entry.defaults;
    }
    expect(allDefaults, "Restore All Defaults");

    auto* filter = page.findChildren<QLineEdit*>().front();
    filter->setText(QStringLiteral("grow"));
    int shown = 0;
    for (int row = 0; row < list->topLevelItemCount(); ++row) {
        shown += list->topLevelItem(row)->isHidden() ? 0 : 1;
    }
    expect(shown == 1, "the search field narrows the list");
    filter->clear();

    // And through the Settings dialog.
    app::SettingsDialog dialog(app::Theme::System, {}, {}, false);
    dialog.setShortcuts(shortcuts.entries());
    dialog.showPage(app::SettingsDialog::Page::Shortcuts);
    expect(dialog.shortcuts().size() == 4, "the Settings dialog holds the entries");
}

}  // namespace

void runShortcutsWidgetTests()
{
    QCoreApplication::setOrganizationName(QStringLiteral("ShortcutsWidgetTests"));
    QSettings().clear();
    testRules();
    testKeptInSettings();
    testPage();
    QSettings().clear();
    QCoreApplication::setOrganizationName(QString());
}
