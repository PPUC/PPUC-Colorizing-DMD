#include "EditMenuActions.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace colorizing::app {

EditMenuActions::EditMenuActions(QMenu* menu, QObject* parent)
    : QObject(parent)
{
    const auto ctrl = [](int key) { return QKeySequence(Qt::CTRL | key); };
    const auto ctrlShift = [](int key) { return QKeySequence(Qt::CTRL | Qt::SHIFT | key); };
    const auto ctrlAlt = [](int key) { return QKeySequence(Qt::CTRL | Qt::ALT | key); };
    const auto alt = [](int key) { return QKeySequence(Qt::ALT | key); };

    menu->addSeparator();
    add(menu, QStringLiteral("&Copy"), QStringLiteral("Copy the selected part of the frame"),
        {QKeySequence(QKeySequence::Copy)}, &EditMenuActions::onCopy, QKeySequence::Copy);
    m_pasteAction = add(menu, QStringLiteral("&Paste"),
                        QStringLiteral("Float the copied part over the frame; click or Enter "
                                       "pastes it on the selected frames"),
                        {QKeySequence(QKeySequence::Paste)}, &EditMenuActions::onPaste,
                        QKeySequence::Paste);
    m_pasteMatchAction = add(menu, QStringLiteral("Paste Where Originals &Match"),
                             QStringLiteral("Paste only on pixels whose original shade is the "
                                            "copied one"),
                             {ctrlShift(Qt::Key_V)}, &EditMenuActions::onPasteWhereOriginalsMatch);
    m_pasteDynamicAction = add(menu, QStringLiteral("Paste D&ynamic Mask Only"),
                               QStringLiteral("Paste the copied dynamic zones without colors"),
                               {ctrlAlt(Qt::Key_V)}, &EditMenuActions::onPasteDynamicMaskOnly);

    menu->addSeparator();
    add(menu, QStringLiteral("Select &All"), QStringLiteral("Select every pixel of the plane"),
        {QKeySequence(QKeySequence::SelectAll)}, &EditMenuActions::onSelectAll,
        QKeySequence::SelectAll);
    add(menu, QStringLiteral("Select &None"), QStringLiteral("Clear the selection"),
        {ctrlShift(Qt::Key_A), ctrl(Qt::Key_D)}, &EditMenuActions::onSelectNone);
    add(menu, QStringLiteral("&Invert Selection"), QStringLiteral("Swap selected and unselected"),
        {ctrlShift(Qt::Key_I)}, &EditMenuActions::onInvertSelection);
    add(menu, QStringLiteral("Select By &Color"),
        QStringLiteral("Click a pixel: every pixel of its color is selected (also "
                       "Ctrl+Alt+click with the fill tool)"),
        {}, &EditMenuActions::onSelectByColor);
    add(menu, QStringLiteral("Selection From &Background Mask"),
        QStringLiteral("Select the frame's background mask"), {ctrl(Qt::Key_B)},
        &EditMenuActions::onSelectBackgroundMask);
    add(menu, QStringLiteral("Mirror Selection &Horizontally"),
        QStringLiteral("Mirror the selection left to right"), {ctrl(Qt::Key_K)},
        &EditMenuActions::onMirrorHorizontally);
    add(menu, QStringLiteral("Mirror Selection &Vertically"),
        QStringLiteral("Mirror the selection top to bottom"), {ctrl(Qt::Key_L)},
        &EditMenuActions::onMirrorVertically);
    m_nonBlackOnlyAction = menu->addAction(QStringLiteral("Select Non-Black Pixels Only"));
    m_nonBlackOnlyAction->setCheckable(true);
    m_nonBlackOnlyAction->setToolTip(
        QStringLiteral("Selecting with Ctrl leaves black pixels out (also: hold W)"));
    m_actions.push_back(m_nonBlackOnlyAction);
    add(menu, QStringLiteral("Auto &Select..."),
        QStringLiteral("Select stripes of rows or columns"), {}, &EditMenuActions::onAutoSelect);

    menu->addSeparator();
    add(menu, QStringLiteral("Auto C&opy..."),
        QStringLiteral("Copy the selection's colorization to every frame whose original is the "
                       "same there"),
        {}, &EditMenuActions::onAutoCopy);
    add(menu, QStringLiteral("Scrolling Auto &Fill..."),
        QStringLiteral("Colorize the frames of a scrolling between the first and the last "
                       "selected frame"),
        {}, &EditMenuActions::onScrollingFill);
    add(menu, QStringLiteral("&Recolor..."),
        QStringLiteral("Click a pixel: every pixel of its color gets the draw color (also "
                       "Alt+click with the fill tool)"),
        {}, &EditMenuActions::onRecolor);
    add(menu, QStringLiteral("Black Original &To Black"),
        QStringLiteral("Pixels whose original is black become black"), {ctrlShift(Qt::Key_O)},
        &EditMenuActions::onBlackOriginalToBlack);
    add(menu, QStringLiteral("R&emove Dynamic Zones"),
        QStringLiteral("Remove the dynamic zones of the selection, or of the frames"),
        {ctrl(Qt::Key_E), alt(Qt::Key_D)}, &EditMenuActions::onRemoveDynamicZones);
    add(menu, QStringLiteral("Assign Dynamic S&et"),
        QStringLiteral("Put the selection, or the frames, into the active dynamic set"),
        {alt(Qt::Key_A)}, &EditMenuActions::onAssignDynamicSet);
    add(menu, QStringLiteral("D&ynamic To Static"),
        QStringLiteral("Dynamic pixels of the selection, or of the frames, keep the color they "
                       "show as a fixed one"),
        {}, &EditMenuActions::onDynamicToStatic);
    add(menu, QStringLiteral("Bri&ghtness..."),
        QStringLiteral("Make the selected pixels brighter or darker by a percentage"), {},
        &EditMenuActions::onBrightness);
    addDirections(menu, QStringLiteral("S&hadow Next To Selection"),
                  QStringLiteral("Draw the draw color next to the selection, where it is not "
                                 "dynamic"),
                  &EditMenuActions::onShadowSelection);
    addDirections(menu, QStringLiteral("&Grow Selection"),
                  QStringLiteral("Add the pixels next to the selection"),
                  &EditMenuActions::onGrowSelection);
    setState(false, false);
}

void EditMenuActions::addDirections(
    QMenu* menu,
    const QString& title,
    const QString& toolTip,
    std::function<void(edit::SelectionDirection)> EditMenuActions::*handler)
{
    using edit::SelectionDirection;
    QMenu* submenu = menu->addMenu(title);
    submenu->setToolTipsVisible(true);
    submenu->menuAction()->setToolTip(toolTip);
    const std::pair<const char*, SelectionDirection> directions[] = {
        {"All Around", SelectionDirection::AllAround},
        {"Left", SelectionDirection::Left},
        {"Bottom Left", SelectionDirection::BottomLeft},
        {"Bottom", SelectionDirection::Bottom},
        {"Bottom Right", SelectionDirection::BottomRight},
        {"Right", SelectionDirection::Right},
        {"Top Right", SelectionDirection::TopRight},
        {"Top", SelectionDirection::Top},
        {"Top Left", SelectionDirection::TopLeft},
    };
    for (const auto& [text, direction] : directions) {
        QAction* action = submenu->addAction(QString::fromLatin1(text));
        const SelectionDirection chosen = direction;
        connect(action, &QAction::triggered, this, [this, handler, chosen]() {
            if (this->*handler) {
                (this->*handler)(chosen);
            }
        });
        m_actions.push_back(action);
        m_projectActions.push_back(action);
    }
    m_projectActions.push_back(submenu->menuAction());
}

QAction* EditMenuActions::add(QMenu* menu,
                              const QString& text,
                              const QString& toolTip,
                              const QList<QKeySequence>& shortcuts,
                              std::function<void()> EditMenuActions::*handler,
                              QKeySequence::StandardKey forwarded)
{
    QAction* action = menu->addAction(text);
    action->setToolTip(toolTip);
    action->setShortcuts(shortcuts);
    connect(action, &QAction::triggered, this, [this, handler, forwarded]() {
        if (forwarded != QKeySequence::UnknownKey &&
            forwardStandardKey(QApplication::focusWidget(), forwarded)) {
            return;
        }
        if (this->*handler) {
            (this->*handler)();
        }
    });
    m_actions.push_back(action);
    m_projectActions.push_back(action);
    return action;
}

void EditMenuActions::setState(bool hasProject, bool hasClipboard)
{
    for (QAction* action : m_projectActions) {
        action->setEnabled(hasProject);
    }
    for (QAction* action : {m_pasteMatchAction, m_pasteDynamicAction}) {
        action->setEnabled(hasProject && hasClipboard);
    }
    // Paste stays enabled so that it reaches a focused text field.
}

bool EditMenuActions::nonBlackOnly() const
{
    return m_nonBlackOnlyAction->isChecked();
}

const std::vector<QAction*>& EditMenuActions::actions() const
{
    return m_actions;
}

bool EditMenuActions::forwardStandardKey(QWidget* focus, QKeySequence::StandardKey key)
{
    if (!focus) {
        return false;
    }
    if (auto* spinBox = qobject_cast<QAbstractSpinBox*>(focus)) {
        if (key == QKeySequence::SelectAll) {
            spinBox->selectAll();
            return true;
        }
        return false;
    }
    if (auto* lineEdit = qobject_cast<QLineEdit*>(focus)) {
        if (key == QKeySequence::Copy) {
            lineEdit->copy();
        } else if (key == QKeySequence::Paste) {
            lineEdit->paste();
        } else {
            lineEdit->selectAll();
        }
        return true;
    }
    if (auto* textEdit = qobject_cast<QTextEdit*>(focus)) {
        if (key == QKeySequence::Copy) {
            textEdit->copy();
        } else if (key == QKeySequence::Paste) {
            textEdit->paste();
        } else {
            textEdit->selectAll();
        }
        return true;
    }
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(focus)) {
        if (key == QKeySequence::Copy) {
            plainTextEdit->copy();
        } else if (key == QKeySequence::Paste) {
            plainTextEdit->paste();
        } else {
            plainTextEdit->selectAll();
        }
        return true;
    }
    if (auto* view = qobject_cast<QAbstractItemView*>(focus)) {
        if (key == QKeySequence::SelectAll &&
            view->selectionMode() != QAbstractItemView::SingleSelection &&
            view->selectionMode() != QAbstractItemView::NoSelection) {
            view->selectAll();
            return true;
        }
    }
    return false;
}

}  // namespace colorizing::app
