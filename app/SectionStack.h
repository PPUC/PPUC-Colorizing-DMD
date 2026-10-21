#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QFrame;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace colorizing::app {

class SectionResizeHandle;

// Sections one below the other, each with a header that opens and closes it, any number open
// at a time (as the trays of SketchUp). The open sections share the height below the headers,
// each scrolling its own content; the headers of the closed ones stay visible. The shares start
// equal; a handle below an open section, when another one is open further down, moves the
// border between the two (a double click makes all shares equal again). Which sections are open
// and their shares are kept in QSettings under the state key, for the next start.
//
// A section has a key, which does not change (kept in the settings, tabText()), and a title, the
// header's text. The calls are those of QTabWidget that the editor uses: "current" is the
// section opened or marked last.
//
// The author arranges the sections: a header dragged up or down moves its section, and the
// header's context menu moves it, puts all back in their first order, and hides an optional
// section or shows a hidden one again. The order and the hidden sections are kept in QSettings
// under the state key as well. An index is a section's place as added and stays it; only the
// order shown changes. Hidden by the author and hidden by setSectionHidden() are separate: a
// section shows when neither hides it.
class SectionStack final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SectionStack)

public:
    explicit SectionStack(QWidget* parent = nullptr);

    // title: the header's text; empty for the key.
    int addTab(QWidget* content, const QString& key, const QString& title = QString());
    [[nodiscard]] int count() const;
    // The key.
    [[nodiscard]] QString tabText(int index) const;
    [[nodiscard]] QString title(int index) const;
    [[nodiscard]] QWidget* widget(int index) const;
    [[nodiscard]] int indexOf(const QWidget* content) const;

    // Opens the section and makes it the current one.
    void setCurrentIndex(int index);
    void setCurrentWidget(QWidget* content);
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] QWidget* currentWidget() const;
    // Makes the section the current one without opening it or scrolling.
    void markCurrent(const QWidget* content);

    [[nodiscard]] bool isSectionOpen(int index) const;
    void setSectionOpen(int index, bool open);
    // A hidden section takes no room, header included; it keeps being open or closed (and
    // stored so) and shows again as it was.
    [[nodiscard]] bool isSectionHidden(int index) const;
    void setSectionHidden(int index, bool hidden);
    // An optional section can be hidden by the author (context menu of the headers).
    void setSectionOptional(int index, bool optional);
    [[nodiscard]] bool isSectionOptional(int index) const;
    [[nodiscard]] bool isSectionHiddenByAuthor(int index) const;
    // Ignored for a section that is not optional.
    void setSectionHiddenByAuthor(int index, bool hidden);
    // The keys in the order shown.
    [[nodiscard]] QStringList sectionOrder() const;
    // Moves the section to that place of the order shown (0: the top).
    void moveSection(int index, int position);
    // The order the sections were added in, and every optional section shown again.
    void resetArrangement();
    // The headers' context menu, for tests: its entries' texts.
    [[nodiscard]] QStringList contextMenuEntries(int index) const;
    // The section's share of the height among the open ones (100 each at first).
    [[nodiscard]] int sectionWeight(int index) const;
    // Moves the border below the open section by dy pixels, against the next open section below
    // it, as dragging its handle does. Returns whether there is such a border.
    bool resizeBelow(int index, int dy);
    // All open sections get the same share.
    void resetSectionSizes();

    // Restores the open sections stored under the key (the given titles when nothing is
    // stored) and stores them from then on.
    void restoreOpenSections(const QString& stateKey, const QStringList& openByDefault);

    // After the current section changed.
    std::function<void(int index)> onCurrentChanged;

private:
    struct Section {
        QString key;
        QString title;
        QWidget* content = nullptr;
        QToolButton* header = nullptr;
        QScrollArea* frame = nullptr;
        SectionResizeHandle* handle = nullptr;
        int weight = kDefaultWeight;
        bool hidden = false;
        bool optional = false;
        bool hiddenByAuthor = false;
    };
    static constexpr int kDefaultWeight = 100;
    // The smallest height a section is resized to.
    static constexpr int kMinimumHeight = 40;
    // Open and not hidden: it has a share of the height.
    [[nodiscard]] bool isSectionShown(int index) const;
    // Hidden by the editor or by the author.
    [[nodiscard]] bool isSectionOutOfView(int index) const;
    void applyVisibility(int index);
    // The layout in the order shown; the section's place in it.
    void relayout();
    [[nodiscard]] int positionOf(int index) const;
    void storeArrangement() const;
    void showContextMenu(int index, const QPoint& globalPos);
    // Dragging a header: the place it would go to for a global y, shown by m_dropLine.
    [[nodiscard]] int dropPosition(int globalY) const;
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The next shown open section after the index; -1 for none.
    [[nodiscard]] int nextOpenSection(int index) const;
    void setCurrent(int index);
    void storeOpenSections() const;
    // The open sections share the height; with none open the headers stay at the top.
    void updateStretch();

    std::vector<Section> m_sections;
    // Section indices in the order shown.
    std::vector<int> m_order;
    QVBoxLayout* m_layout = nullptr;
    QFrame* m_dropLine = nullptr;
    int m_dragIndex = -1;
    bool m_dragging = false;
    int m_pressY = 0;
    int m_current = -1;
    QString m_stateKey;
};

}  // namespace colorizing::app
