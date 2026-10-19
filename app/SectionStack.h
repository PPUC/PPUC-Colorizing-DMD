#pragma once

#include <QWidget>
#include <QString>

#include <functional>
#include <vector>

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
class SectionStack final : public QWidget {
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
    };
    static constexpr int kDefaultWeight = 100;
    // The smallest height a section is resized to.
    static constexpr int kMinimumHeight = 40;
    // The next open section after the index; -1 for none.
    [[nodiscard]] int nextOpenSection(int index) const;
    void setCurrent(int index);
    void storeOpenSections() const;
    // The open sections share the height; with none open the headers stay at the top.
    void updateStretch();

    std::vector<Section> m_sections;
    QVBoxLayout* m_layout = nullptr;
    int m_current = -1;
    QString m_stateKey;
};

}  // namespace colorizing::app
