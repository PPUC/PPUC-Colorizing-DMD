#pragma once

#include <functional>
#include <optional>
#include <utility>

#include <QIcon>
#include <QObject>

class QListWidget;
class QListWidgetItem;
class QTimer;

namespace colorizing::app {

// Builds the icons of a list's rows when they come into view instead of all at once. A row
// shows the placeholder until then. A built icon is kept until invalidate() is called for its
// item or the item is removed; nothing is evicted, so at most one icon per row exists, as if
// all had been built up front.
//
// New items are given placeholder() by their creator and count as not built. Row heights must
// come from the items' size hints, so that they do not depend on the icons.
class LazyListIcons final : public QObject {
public:
    using Builder = std::function<QIcon(const QListWidgetItem& item)>;

    LazyListIcons(QListWidget* list, QIcon placeholder, Builder builder);

    [[nodiscard]] const QIcon& placeholder() const;
    [[nodiscard]] bool isBuilt(const QListWidgetItem* item) const;
    // The icon no longer shows the item's data. A row in view is rebuilt at once; any other
    // goes back to the placeholder and is rebuilt when it comes into view.
    void invalidate(QListWidgetItem* item);
    // Builds the icons that are missing in the rows first..last. The list does this for the
    // rows in view. Tests and the ui_dump tool use it to see rows that are not in view.
    void ensureBuilt(int firstRow, int lastRow);
    // Call after rows were added. Scrolling, resizing and showing the list are noticed here.
    void scheduleRowsInView();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Nothing while the list is hidden or empty.
    [[nodiscard]] std::optional<std::pair<int, int>> rowsInView() const;

    QListWidget* m_list = nullptr;
    QIcon m_placeholder;
    Builder m_builder;
    QTimer* m_timer = nullptr;
};

}  // namespace colorizing::app
