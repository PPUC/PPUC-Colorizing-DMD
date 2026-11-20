#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <utility>
#include <vector>

#include "KeyboardShortcuts.h"

class QAction;
class QGridLayout;
class QToolButton;
class QVBoxLayout;

namespace colorizing::app {

// The Inspector's Quick Actions section: the menu entries the author uses most, as buttons that
// stay in view while the other sections are used. The author chooses them (Choose...) from the
// entries the keyboard shortcut settings list, by the same ids; the choice is the editor's, kept
// in the settings for every project, not in a project. A button runs its menu entry as the menu
// does and is enabled when the entry is.
//
// Two entries are groups: every direction of Edit -> Shadow Next To Selection or Grow Selection,
// as a square of direction buttons with Stern (the four sides without the corners) in the
// middle and All Around below, so that several directions are a click each.
class QuickActionsPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(QuickActionsPanel)

public:
    explicit QuickActionsPanel(QWidget* parent = nullptr);

    // The ids of the groups among the items.
    static constexpr const char* kShadowGroup = "group:shadow";
    static constexpr const char* kGrowGroup = "group:grow";

    // The menu entry with the id; nullptr for none. Set before setItems().
    std::function<QAction*(const QString& id)> actionFor;
    // The entries the author can choose from, as the keyboard shortcut settings list them.
    std::function<std::vector<ShortcutEntry>()> entries;

    // Shows these items (entry ids and groups) in this order; unknown ids are left out.
    void setItems(const QStringList& items);
    [[nodiscard]] const QStringList& items() const;
    // The buttons shown, for tests: each with the id of the menu entry it runs.
    [[nodiscard]] std::vector<std::pair<QString, QToolButton*>> buttons() const;

    // What a new editor shows, and what the settings keep.
    [[nodiscard]] static QStringList defaultItems();
    [[nodiscard]] static QStringList loadItems();
    static void saveItems(const QStringList& items);
    // What Choose... offers: the groups first, then every entry in the order of the menus,
    // without the single directions the groups hold; each as its item id and its label.
    [[nodiscard]] static std::vector<std::pair<QString, QString>> choices(
        const std::vector<ShortcutEntry>& entries);

private:
    void rebuild();
    void choose();
    QToolButton* actionButton(const QString& id, const QString& label, QWidget* parent);
    QWidget* groupWidget(const QString& group);

    QStringList m_items;
    QVBoxLayout* m_itemsLayout = nullptr;
    QWidget* m_itemsWidget = nullptr;
    QToolButton* m_chooseButton = nullptr;
    std::vector<std::pair<QString, QToolButton*>> m_buttons;
};

}  // namespace colorizing::app
