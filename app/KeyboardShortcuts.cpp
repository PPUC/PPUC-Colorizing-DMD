#include "KeyboardShortcuts.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStringList>

#include <algorithm>

#include "KeyNames.h"

namespace colorizing::app {

namespace {

const QString kSettingsGroup = QStringLiteral("shortcuts");

QString settingsKey(const QString& id)
{
    return kSettingsGroup + QChar('/') + id;
}

// What the frame strip does with these keys comes before any menu.
bool isFrameStripKey(const QKeySequence& key)
{
    static const QList<QKeySequence> keys{
        QKeySequence(Qt::CTRL | Qt::Key_Left), QKeySequence(Qt::CTRL | Qt::Key_Right),
        QKeySequence(Qt::ALT | Qt::Key_Left),  QKeySequence(Qt::ALT | Qt::Key_Right),
        QKeySequence(Qt::ALT | Qt::Key_Up),    QKeySequence(Qt::ALT | Qt::Key_Down),
    };
    return keys.contains(key);
}

}  // namespace

std::optional<QString> shortcutKeyProblem(const QKeySequence& key,
                                          const QList<QKeySequence>& defaults)
{
    if (key.isEmpty() || defaults.contains(key)) {
        return std::nullopt;
    }
    const QKeyCombination combination = key[0];
    const Qt::KeyboardModifiers command =
        combination.keyboardModifiers() &
        (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const Qt::Key pressed = combination.key();
    if (command == Qt::NoModifier && (pressed < Qt::Key_F1 || pressed > Qt::Key_F35)) {
        //: %1: Ctrl (Cmd on macOS), %2: Alt (Option on macOS)
        return KeyboardShortcuts::tr("A key alone acts on the canvas and in text fields. Use it "
                                     "with %1 or %2, or use a function key.")
            .arg(ctrlKeyName(), altKeyName());
    }
    if (isFrameStripKey(key)) {
        return KeyboardShortcuts::tr("The frame strip uses this key.");
    }
    return std::nullopt;
}

std::optional<std::size_t> entryWithKey(const std::vector<ShortcutEntry>& entries,
                                        const QKeySequence& key,
                                        std::size_t except)
{
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (index != except && entries[index].current.contains(key)) {
            return index;
        }
    }
    return std::nullopt;
}

QString shortcutsText(const QList<QKeySequence>& keys)
{
    QStringList texts;
    for (const QKeySequence& key : keys) {
        texts << key.toString(QKeySequence::NativeText);
    }
    return texts.join(QStringLiteral(", "));
}

KeyboardShortcuts::KeyboardShortcuts(QMenuBar* menuBar)
{
    collect(menuBar->actions(), QString());
    // Keys the user chose win over a default that has them: a later version may give out a key
    // that someone has chosen before, and an ambiguous key does nothing.
    const QSettings settings;
    std::vector<ShortcutEntry> saved = entries();
    QList<QKeySequence> chosen;
    for (ShortcutEntry& entry : saved) {
        const QString key = settingsKey(entry.id);
        if (settings.contains(key)) {
            entry.current = QKeySequence::listFromString(settings.value(key).toString(),
                                                         QKeySequence::PortableText);
            chosen += entry.current;
        }
    }
    for (std::size_t index = 0; index < saved.size(); ++index) {
        if (!settings.contains(settingsKey(saved[index].id))) {
            saved[index].current.removeIf(
                [&chosen](const QKeySequence& key) { return chosen.contains(key); });
        }
        m_items[index].action->setShortcuts(saved[index].current);
    }
}

void KeyboardShortcuts::collect(const QList<QAction*>& actions, const QString& path)
{
    for (QAction* action : actions) {
        QString text = action->text();
        text.remove(QChar('&'));
        const QString title = path.isEmpty() ? text : path + QStringLiteral(" -> ") + text;
        if (QMenu* menu = action->menu()) {
            collect(menu->actions(), title);
        } else if (!action->isSeparator() && !action->objectName().isEmpty()) {
            m_items.push_back({action, action->shortcuts()});
            m_titles.push_back(title);
        }
    }
}

std::vector<ShortcutEntry> KeyboardShortcuts::entries() const
{
    std::vector<ShortcutEntry> result;
    result.reserve(m_items.size());
    for (std::size_t index = 0; index < m_items.size(); ++index) {
        const Item& item = m_items[index];
        result.push_back({item.action->objectName(), m_titles[index], item.defaults,
                          item.action->shortcuts()});
    }
    return result;
}

void KeyboardShortcuts::apply(const std::vector<ShortcutEntry>& entries)
{
    QSettings settings;
    for (const ShortcutEntry& entry : entries) {
        const auto item = std::find_if(m_items.begin(), m_items.end(), [&entry](const Item& it) {
            return it.action->objectName() == entry.id;
        });
        if (item == m_items.end()) {
            continue;
        }
        item->action->setShortcuts(entry.current);
        if (entry.current == item->defaults) {
            settings.remove(settingsKey(entry.id));
        } else {
            settings.setValue(settingsKey(entry.id),
                              QKeySequence::listToString(entry.current,
                                                         QKeySequence::PortableText));
        }
    }
}

}  // namespace colorizing::app
