#pragma once

#include "edit/RecolorHelpers.h"
#include <functional>
#include <vector>

#include <QCoreApplication>
#include <QKeySequence>
#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QWidget;

namespace colorizing::app {

// The copy/paste and selection section of the Edit menu. Each entry calls its handler; the owner
// decides what it does. Shortcuts:
//
//   Copy                          Ctrl+C (Cmd+C)       Win32 Ctrl+C
//   Paste                         Ctrl+V (Cmd+V)       Win32 Ctrl+V
//   Paste Where Originals Match   Ctrl+Shift+V         Win32 Shift+click in paste mode
//   Paste Dynamic Mask Only       Ctrl+Alt+V           Win32 Alt+click in paste mode
//   Stay In Paste Mode            (checkable)          Win32 ends paste mode with a stamp
//   Select All                    Ctrl+A (Cmd+A)       Win32 Ctrl+A
//   Select None                   Ctrl+Shift+A, Ctrl+D Win32 Ctrl+D
//   Invert Selection              Ctrl+Shift+I         Win32 button only
//   Selection From Background Mask Ctrl+B              Win32 Ctrl+B
//   Mirror Selection Horizontally Ctrl+K               Win32 Ctrl+K
//   Mirror Selection Vertically   Ctrl+L               Win32 Ctrl+L
//   Black Original To Black       Ctrl+Shift+O         Win32 Ctrl+O (Open here)
//   Remove Dynamic Zones          Ctrl+E, Alt+D        Win32 Ctrl+E, Alt+D
//   Assign Dynamic Set            Alt+A                Win32 Alt+A
//
// Ctrl is Cmd on macOS (Qt's ControlModifier) and Alt is Option. A plain letter cannot be a menu
// shortcut (a native menu bar takes it from every widget), so invert is not "I".
//
// Standard keys go to text fields and lists first: with a line edit, a spin box or a list in
// focus, Copy, Paste and Select All do what they do there (see forwardStandardKey()).
class EditMenuActions final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(EditMenuActions)

public:
    EditMenuActions(QMenu* menu, QObject* parent);

    std::function<void()> onCopy;
    std::function<void()> onPaste;
    std::function<void()> onPasteWhereOriginalsMatch;
    std::function<void()> onPasteDynamicMaskOnly;
    std::function<void()> onSelectAll;
    std::function<void()> onSelectNone;
    std::function<void()> onInvertSelection;
    std::function<void()> onSelectByColor;
    std::function<void()> onSelectBackgroundMask;
    std::function<void()> onMirrorHorizontally;
    std::function<void()> onMirrorVertically;
    std::function<void()> onAutoSelect;
    std::function<void()> onAutoCopy;
    std::function<void()> onScrollingFill;
    std::function<void()> onRecolor;
    std::function<void()> onBlackOriginalToBlack;
    std::function<void()> onRemoveDynamicZones;
    std::function<void()> onAssignDynamicSet;
    std::function<void()> onDynamicToStatic;
    std::function<void()> onBrightness;
    // The Win32 shadow buttons: a shadow in the draw color next to the selection, or (Ctrl)
    // the selection grown by a pixel, in a direction or all around.
    std::function<void(edit::SelectionDirection direction)> onShadowSelection;
    std::function<void(edit::SelectionDirection direction)> onGrowSelection;

    // Whether a project is open and whether something was copied.
    void setState(bool hasProject, bool hasClipboard);
    // "Only Non-Black Pixels": selection strokes leave out black pixels (Win32: W held).
    [[nodiscard]] bool nonBlackOnly() const;
    // "Stay In Paste Mode": a stamp keeps paste mode until Esc. Does not notify.
    void setStayInPasteMode(bool stay);
    [[nodiscard]] bool stayInPasteMode() const;
    std::function<void(bool stay)> onStayInPasteModeToggled;
    [[nodiscard]] const std::vector<QAction*>& actions() const;

    // Gives a standard key to the focused text field, spin box or item view if it has one;
    // true when it did.
    static bool forwardStandardKey(QWidget* focus, QKeySequence::StandardKey key);

private:
    // A submenu with an entry per direction.
    void addDirections(QMenu* menu,
                       const QString& title,
                       const QString& toolTip,
                       std::function<void(edit::SelectionDirection)> EditMenuActions::*handler);
    QAction* add(QMenu* menu,
                 const QString& text,
                 const QString& toolTip,
                 const QList<QKeySequence>& shortcuts,
                 std::function<void()> EditMenuActions::*handler,
                 QKeySequence::StandardKey forwarded = QKeySequence::UnknownKey);

    std::vector<QAction*> m_actions;
    std::vector<QAction*> m_projectActions;
    QAction* m_pasteAction = nullptr;
    QAction* m_pasteMatchAction = nullptr;
    QAction* m_pasteDynamicAction = nullptr;
    QAction* m_nonBlackOnlyAction = nullptr;
    QAction* m_stayInPasteModeAction = nullptr;
};

}  // namespace colorizing::app
