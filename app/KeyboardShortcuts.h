#pragma once

#include <QAction>
#include <QCoreApplication>
#include <QKeySequence>
#include <QList>
#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

class QMenuBar;

namespace colorizing::app {

// A menu entry whose keys can be changed in the settings, under an ID that is never translated
// and stays when the entry's text changes: the settings keep the keys by it. The ID is the
// action's object name.
inline QAction* withShortcutId(QAction* action, const char* id)
{
    action->setObjectName(QLatin1String(id));
    return action;
}

struct ShortcutEntry {
    QString id;
    // Where it is in the menus, as shown: "Edit -> Copy".
    QString title;
    QList<QKeySequence> defaults;
    QList<QKeySequence> current;
};

// Why a key cannot be given to an entry; nothing when it can. defaults: the entry's own, which it
// may always have. A key alone (without Ctrl or Alt) acts on the canvas and in text fields, and on
// macOS a menu would take it from them, so only function keys go without. A few keys the frame
// strip handles itself.
[[nodiscard]] std::optional<QString> shortcutKeyProblem(const QKeySequence& key,
                                                        const QList<QKeySequence>& defaults);
// The entry other than `except` that has the key.
[[nodiscard]] std::optional<std::size_t> entryWithKey(const std::vector<ShortcutEntry>& entries,
                                                      const QKeySequence& key,
                                                      std::size_t except);

// The menu bar's entries with an ID. The keys they have when this is made are their defaults; what
// the user set instead is kept in the settings, and applied here.
class KeyboardShortcuts final {
    Q_DECLARE_TR_FUNCTIONS(KeyboardShortcuts)

public:
    explicit KeyboardShortcuts(QMenuBar* menuBar);

    // In the order of the menus.
    [[nodiscard]] std::vector<ShortcutEntry> entries() const;
    // Gives the actions these keys and keeps those that differ from the defaults.
    void apply(const std::vector<ShortcutEntry>& entries);

private:
    struct Item {
        QAction* action = nullptr;
        QList<QKeySequence> defaults;
    };
    void collect(const QList<QAction*>& actions, const QString& path);

    std::vector<Item> m_items;
    std::vector<QString> m_titles;
};

// A shortcut as the platform writes it, several separated by commas; empty for none.
[[nodiscard]] QString shortcutsText(const QList<QKeySequence>& keys);

}  // namespace colorizing::app
