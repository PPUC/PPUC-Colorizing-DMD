#pragma once

#include <QScrollArea>
#include <QString>

#include <functional>
#include <vector>

class QToolButton;
class QVBoxLayout;

namespace colorizing::app {

// Sections one below the other, each with a header that opens and closes it, any number open
// at a time, scrolled together when they do not fit (as the trays of SketchUp). Which sections
// are open is kept in QSettings under the state key, for the next start.
//
// The calls are those of QTabWidget that the editor uses: "current" is the section opened or
// marked last.
class SectionStack final : public QScrollArea {
public:
    explicit SectionStack(QWidget* parent = nullptr);

    int addTab(QWidget* content, const QString& title);
    int insertTab(int index, QWidget* content, const QString& title);
    [[nodiscard]] int count() const;
    [[nodiscard]] QString tabText(int index) const;
    [[nodiscard]] QWidget* widget(int index) const;
    [[nodiscard]] int indexOf(const QWidget* content) const;

    // Opens the section, makes it the current one and scrolls to it.
    void setCurrentIndex(int index);
    void setCurrentWidget(QWidget* content);
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] QWidget* currentWidget() const;
    // Makes the section the current one without opening it or scrolling.
    void markCurrent(const QWidget* content);

    [[nodiscard]] bool isSectionOpen(int index) const;
    void setSectionOpen(int index, bool open);

    // Restores the open sections stored under the key (the given titles when nothing is
    // stored) and stores them from then on.
    void restoreOpenSections(const QString& stateKey, const QStringList& openByDefault);

    // After the current section changed.
    std::function<void(int index)> onCurrentChanged;

private:
    struct Section {
        QString title;
        QWidget* content = nullptr;
        QToolButton* header = nullptr;
        QWidget* frame = nullptr;
    };
    void setCurrent(int index);
    void storeOpenSections() const;

    std::vector<Section> m_sections;
    QVBoxLayout* m_layout = nullptr;
    int m_current = -1;
    QString m_stateKey;
};

}  // namespace colorizing::app
