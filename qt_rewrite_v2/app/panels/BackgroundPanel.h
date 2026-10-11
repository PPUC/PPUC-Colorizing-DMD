#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <QWidget>

#include "document/ProjectDocument.h"

class QLabel;
class QToolButton;

namespace colorizing::app {

// What the background page displays. The selection itself belongs to the owner.
struct BackgroundPanelState {
    const document::ProjectDocument* document = nullptr;
    std::optional<std::size_t> selectedBackgroundIndex;
    render::Resolution resolution = render::Resolution::Sd;
    bool hasCurrentFrame = false;
    bool hasSelectedFrames = false;
};

// Inspector page for the selected background. It only displays and reports what the user
// asked for; the owner applies the edits and calls refresh().
class BackgroundPanel final : public QWidget {
public:
    explicit BackgroundPanel(QWidget* parent = nullptr);

    void refresh(const BackgroundPanelState& state);

    // Focusable controls in creation order, for the owner to arrange the window's tab order.
    [[nodiscard]] std::vector<QWidget*> tabOrderWidgets() const;

    std::function<void()> onAssignRequested;
    std::function<void()> onEditRequested;
    std::function<void()> onCopyFrameToBackgroundRequested;
    std::function<void()> onCopyBackgroundToFramesRequested;
    std::function<void()> onImportImageRequested;
    std::function<void()> onExportRequested;

private:
    QLabel* m_selectedBackgroundLabel = nullptr;
    QToolButton* m_assignButton = nullptr;
    QToolButton* m_editButton = nullptr;
    QToolButton* m_copyFrameToBackgroundButton = nullptr;
    QToolButton* m_copyBackgroundToFramesButton = nullptr;
    QToolButton* m_importImageButton = nullptr;
    QToolButton* m_exportButton = nullptr;
    QLabel* m_previewLabel = nullptr;
};

}  // namespace colorizing::app
