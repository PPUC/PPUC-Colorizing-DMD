#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstddef>
#include <vector>

#include "KeyboardShortcuts.h"

class QLabel;
class QLineEdit;
class QToolButton;
class QTreeWidget;

namespace colorizing::app {

// The Settings page of the keyboard shortcuts: the menu entries and their keys, a key pressed in
// the field below goes to the selected entry. A key that cannot be given says why and changes
// nothing; a key another entry has is not taken from it.
class ShortcutsSettingsPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ShortcutsSettingsPage)

public:
    explicit ShortcutsSettingsPage(QWidget* parent = nullptr);

    void setEntries(std::vector<ShortcutEntry> entries);
    [[nodiscard]] const std::vector<ShortcutEntry>& entries() const { return m_entries; }

private:
    void keyPressed(const QKeySequence& key);
    void setCurrentKeys(const QList<QKeySequence>& keys);
    void showRow(std::size_t index);
    void applyFilter();
    void updateControls();
    [[nodiscard]] int selectedEntry() const;

    std::vector<ShortcutEntry> m_entries;
    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_list = nullptr;
    QLineEdit* m_keyField = nullptr;
    QToolButton* m_noneButton = nullptr;
    QToolButton* m_defaultButton = nullptr;
    QLabel* m_note = nullptr;
};

}  // namespace colorizing::app
