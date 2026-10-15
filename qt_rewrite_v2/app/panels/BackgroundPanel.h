#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include <QCoreApplication>
#include <QWidget>

#include "document/ProjectDocument.h"

class QLabel;
class QToolButton;

namespace colorizing::app {

// What the background page displays. The selection itself belongs to the owner.
struct BackgroundPanelState {
    const document::ProjectDocument* document = nullptr;
    std::optional<std::size_t> selectedBackgroundIndex;
    bool hasCurrentFrame = false;
    bool hasSelectedFrames = false;
    // One of the selected frames has a background assigned.
    bool selectedFramesHaveBackground = false;
    // The canvas edits the selected background (Edit BG is down).
    bool editing = false;
};

// Inspector page for the selected background. It only displays and reports what the user
// asked for; the owner applies the edits and calls refresh().
class BackgroundPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(BackgroundPanel)

public:
    explicit BackgroundPanel(QWidget* parent = nullptr);

    void refresh(const BackgroundPanelState& state);

    std::function<void()> onAssignRequested;
    // Remove BG: the selected frames get no background.
    std::function<void()> onRemoveRequested;
    // Edit BG, a toggle as Edit Sprite: down, the canvas edits the selected background; up
    // again, the frame. editing: the state asked for.
    std::function<void(bool editing)> onEditRequested;
    std::function<void()> onCopyFrameToBackgroundRequested;
    std::function<void()> onCopyBackgroundToFramesRequested;
    std::function<void()> onImportImageRequested;
    std::function<void()> onExportRequested;

private:
    QLabel* m_selectedBackgroundLabel = nullptr;
    QToolButton* m_assignButton = nullptr;
    QToolButton* m_removeButton = nullptr;
    QToolButton* m_editButton = nullptr;
    QToolButton* m_copyFrameToBackgroundButton = nullptr;
    QToolButton* m_copyBackgroundToFramesButton = nullptr;
    QToolButton* m_importImageButton = nullptr;
    QToolButton* m_exportButton = nullptr;
};

}  // namespace colorizing::app
