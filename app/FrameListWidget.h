#pragma once

#include <QImage>
#include <QListWidget>

#include <functional>
#include <map>
#include <vector>

class QKeyEvent;

namespace colorizing::app {

// The frame list. The selected frames can be dragged to another place in the list; the list
// does not move its rows itself but asks onMoveRequested to move the frames, and is then built
// again from the project.
//
// The list takes its own drags itself: in the icon mode of the frame strip, QListView accepts a
// drop only onto an item that takes drops, and frames do not. A vertical line between the
// frames shows where they go.
//
// As in the Win32 editor, a plain click on a frame of a selection of several frames keeps the
// selection and only makes that frame the current one (so a canvas selection can be checked
// on each before copying); a plain click on another frame selects that one alone.
class FrameListWidget final : public QListWidget {
public:
    explicit FrameListWidget(QWidget* parent = nullptr);

    // The row the dragged frames go before; count() for behind the last row.
    std::function<void(int beforeRow)> onMoveRequested;

    // The frame strip: one row of icons of gridSize cells, left to right, frames dragged to
    // another place. QListView turns dragging and the viewport's drops off for the Static
    // movement its icon mode needs here; they are turned on again last.
    void useStripLayout(QSize gridSize);

    // The row a drop at the position puts the frames before, as dropEvent() works it out.
    [[nodiscard]] int dropRow(const QPoint& position) const;

    // The row the line shows the frames going before during a drag; -1 without a drag.
    [[nodiscard]] int shownDropRow() const;

    // A key that steps through the frames while the canvas has the focus (issue #20): Left or
    // Right, with Shift extending the selection as in the strip, without Ctrl, Alt or Meta.
    [[nodiscard]] static bool isFrameStepKey(const QKeyEvent& event);
    // Steps as the key does in the strip, wherever the focus is.
    void stepWithKey(const QKeyEvent& event);
    // Whether the current frame changes by such a key now (the canvas selection stays then,
    // issue #31).
    [[nodiscard]] bool isSteppingWithKey() const { return m_steppingWithKey; }
    // Esc: a selection of several frames becomes the current frame alone. Returns whether
    // there was one to reduce.
    bool selectCurrentAlone();

    // Rows to mark with an outline and with ticks on the scroll bar, ascending: the frames that
    // are the same as the current one while the comparison mask is shown (issue #24).
    void setMarkedRows(std::vector<int> rows);
    [[nodiscard]] const std::vector<int>& markedRows() const;
    // Ctrl+Left or Ctrl+Right (Cmd on macOS), as in the Win32 editor: through the marked rows.
    [[nodiscard]] static bool isMarkedRowStepKey(const QKeyEvent& event);
    // Scrolls to the marked row before (direction < 0) or after the one shown last, at first
    // the current row, and outlines it more strongly. The current frame and the selection stay,
    // so the frames can be looked through before a deletion. Returns the row; -1 if there is
    // none in that direction (nothing then moves).
    int showMarkedRow(int direction);

    // Rows of frames a partner works on in a team project: the partner's avatar in the upper
    // left corner of the thumbnail, a lock while there is none (a null image).
    void setClaimBadges(std::map<int, QImage> badges);
    [[nodiscard]] const std::map<int, QImage>& claimBadges() const { return m_claimBadges; }
    // The badge as painted, size pixels wide and high: the avatar in a ring, or a lock.
    [[nodiscard]] static QImage claimBadge(const QImage& avatar, int size, qreal ratio = 1.0);
    // The marked row showMarkedRow() went to; -1 once the current row changes.
    [[nodiscard]] int shownMarkedRow() const;

protected:
    [[nodiscard]] QItemSelectionModel::SelectionFlags selectionCommand(
        const QModelIndex& index, const QEvent* event = nullptr) const override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // As in the Win32 editor, a double click selects the frame alone: a selection of several
    // frames is dropped (issue #18).
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Accepts a drag of the list's own rows and shows where they would go.
    bool takeDrag(QDragMoveEvent* event);

    int m_dropRow = -1;
    std::vector<int> m_markedRows;
    int m_shownMarkedRow = -1;
    std::map<int, QImage> m_claimBadges;
    bool m_steppingWithKey = false;
};

}  // namespace colorizing::app
