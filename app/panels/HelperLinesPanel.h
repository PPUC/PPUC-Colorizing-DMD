#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "document/HelperLine.h"

class QListWidget;
class QPushButton;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

// The Helper Lines section of the inspector: the project's helper lines with a check box each
// to show or hide it, and buttons to add, edit and delete them. It only shows them and reports
// what was asked; the owner changes the lines (undoably) and calls setLines().
class HelperLinesPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(HelperLinesPanel)

public:
    explicit HelperLinesPanel(QWidget* parent = nullptr);

    // document: for the plane names; nullptr without a project.
    void setLines(const std::vector<document::HelperLine>& lines,
                  const document::ProjectDocument* document);
    [[nodiscard]] std::optional<std::size_t> selectedLine() const;
    // The Show Lines button: all checked lines shown on the canvas, or none.
    void setLinesShown(bool shown);

    std::function<void(bool vertical)> onAddRequested;
    std::function<void(std::size_t line)> onEditRequested;
    std::function<void(std::size_t line)> onDeleteRequested;
    std::function<void(std::size_t line, bool visible)> onVisibilityChanged;
    std::function<void(bool shown)> onLinesShownChanged;

private:
    void updateButtons();

    QListWidget* m_list = nullptr;
    QPushButton* m_showButton = nullptr;
    QPushButton* m_addHorizontalButton = nullptr;
    QPushButton* m_addVerticalButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    bool m_hasProject = false;
    bool m_filling = false;
};

}  // namespace colorizing::app
