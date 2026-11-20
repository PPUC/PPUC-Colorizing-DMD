// The Inspector's Quick Actions section: the author's menu entries as buttons, and the square of
// directions of Shadow Next To Selection and Grow Selection.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QSettings>
#include <QToolButton>

#include "EditMenuActions.h"
#include "panels/QuickActionsPanel.h"

namespace {

using colorizing::app::EditMenuActions;
using colorizing::app::QuickActionsPanel;
using colorizing::app::ShortcutEntry;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: quick actions: " << message << '\n';
        std::exit(1);
    }
}

QToolButton* buttonFor(const QuickActionsPanel& panel, const QString& id)
{
    for (const auto& [buttonId, button] : panel.buttons()) {
        if (buttonId == id) {
            return button;
        }
    }
    return nullptr;
}

void testButtons()
{
    QMenu editMenu;
    EditMenuActions actions(&editMenu, nullptr);
    actions.setState(true, false);
    int selectedNone = 0;
    std::vector<int> shadows;
    actions.onSelectNone = [&selectedNone]() { ++selectedNone; };
    actions.onShadowSelection = [&shadows](colorizing::edit::SelectionDirection direction) {
        shadows.push_back(static_cast<int>(direction));
    };

    QuickActionsPanel panel;
    panel.actionFor = [&editMenu](const QString& id) {
        return editMenu.findChild<QAction*>(id);
    };
    panel.setItems({QStringLiteral("select-none"),
                    QString::fromLatin1(QuickActionsPanel::kShadowGroup),
                    QStringLiteral("no-such-entry")});
    // select-none, and the nine of the square with All Around.
    expect(panel.buttons().size() == 11, "a button per entry and per direction, none for an "
                                         "unknown entry");
    QToolButton* none = buttonFor(panel, QStringLiteral("select-none"));
    expect(none && none->text() == QStringLiteral("Select None") && none->isEnabled(),
           "a button shows its entry's text without the mnemonic");
    none->click();
    expect(selectedNone == 1, "a click runs the entry as the menu does");

    QToolButton* stern = buttonFor(panel, QStringLiteral("shadow-all-around-stern-style"));
    QToolButton* bottomLeft = buttonFor(panel, QStringLiteral("shadow-bottom-left"));
    expect(stern && stern->text() == QStringLiteral("Stern") && bottomLeft &&
               bottomLeft->text() == QStringLiteral("BL") &&
               buttonFor(panel, QStringLiteral("shadow-all-around")),
           "the shadow square: Stern in the middle, the directions around, All Around below");
    bottomLeft->click();
    stern->click();
    expect(shadows == std::vector<int>{
                          static_cast<int>(colorizing::edit::SelectionDirection::BottomLeft),
                          static_cast<int>(colorizing::edit::SelectionDirection::Sides)},
           "each direction runs its entry");

    actions.setState(false, false);
    expect(!none->isEnabled() && !stern->isEnabled(), "a button is enabled as its entry is");
    actions.setState(true, false);
    expect(none->isEnabled(), "and follows it back");
}

void testChoicesAndSettings()
{
    const std::vector<ShortcutEntry> entries{
        {QStringLiteral("copy"), QStringLiteral("Edit -> Copy"), {}, {}},
        {QStringLiteral("shadow-left"), QStringLiteral("Edit -> Shadow -> Left"), {}, {}},
        {QStringLiteral("grow-top"), QStringLiteral("Edit -> Grow -> Top"), {}, {}},
        {QStringLiteral("save"), QStringLiteral("File -> Save"), {}, {}},
    };
    const auto offered = QuickActionsPanel::choices(entries);
    expect(offered.size() == 4 &&
               offered[0].first == QString::fromLatin1(QuickActionsPanel::kShadowGroup) &&
               offered[1].first == QString::fromLatin1(QuickActionsPanel::kGrowGroup) &&
               offered[2].first == QStringLiteral("copy") &&
               offered[3].first == QStringLiteral("save"),
           "the groups first, then the entries without the single directions");

    QCoreApplication::setOrganizationName(QStringLiteral("QuickActionsWidgetTests"));
    QSettings().remove(QStringLiteral("quickActions"));
    expect(QuickActionsPanel::loadItems() == QuickActionsPanel::defaultItems(),
           "a new editor shows the default entries");
    QuickActionsPanel::saveItems({QStringLiteral("copy")});
    expect(QuickActionsPanel::loadItems() == QStringList{QStringLiteral("copy")},
           "the author's choice is kept");
    QuickActionsPanel::saveItems({});
    expect(QuickActionsPanel::loadItems().isEmpty(), "also an empty one");
    QSettings().remove(QStringLiteral("quickActions"));
    QCoreApplication::setOrganizationName(QString());
}

}  // namespace

void runQuickActionsWidgetTests()
{
    testButtons();
    testChoicesAndSettings();
}
