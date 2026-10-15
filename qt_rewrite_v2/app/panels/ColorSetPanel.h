#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <QCoreApplication>
#include <QString>
#include <QWidget>

#include "document/ProjectDocument.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;

namespace colorizing::app {

// Inspector page of the 64 color sets of the cRP (the Win32 color set dialog, ColSet_Proc):
// each with its colors, name and active flag. It shows and reports; the owner changes the
// document and calls refresh().
class ColorSetPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ColorSetPanel)

public:
    explicit ColorSetPanel(QWidget* parent = nullptr);

    // Keeps the selected set. A new project (projectChanged) selects its current color set and
    // scrolls to its first displayed one, which the Win32 dialog remembers.
    void refresh(const document::ProjectDocument* document, bool projectChanged);
    [[nodiscard]] std::optional<std::size_t> selectedSet() const;
    void selectSet(std::size_t setIndex);
    // The first set in view, at most kMaxColorSets - 8 (the Win32 dialog shows 8 sets).
    [[nodiscard]] std::size_t firstVisibleSet() const;

    // "Set" of the Win32 dialog: the set takes the edit colors and becomes active.
    std::function<void(std::size_t setIndex)> onStoreRequested;
    // A click on a set in the Win32 dialog: the edit colors take its colors. Also a double
    // click on the set.
    std::function<void(std::size_t setIndex)> onLoadRequested;
    std::function<void(std::size_t setIndex)> onDeactivateRequested;
    std::function<void(std::size_t setIndex, const QString& name)> onNameEdited;
    // The user scrolled the list.
    std::function<void(std::size_t firstVisibleSet)> onScrolled;

private:
    void updateButtons();

    std::vector<bool> m_active;
    QListWidget* m_list = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QToolButton* m_storeButton = nullptr;
    QToolButton* m_loadButton = nullptr;
    QToolButton* m_deactivateButton = nullptr;
    QLabel* m_hintLabel = nullptr;
    bool m_refreshing = false;
    bool m_userScrolling = false;
};

}  // namespace colorizing::app
