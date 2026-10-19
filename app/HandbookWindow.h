#pragma once

#include <QCoreApplication>
#include <QString>
#include <QWidget>

#include <vector>

class QLineEdit;
class QListWidget;
class QTextBrowser;

namespace colorizing::app {

// Help -> Handbook: the handbook's chapters (handbook/<language>/NN-*.md, built in as resources
// under :/handbook), searchable. The chapters of the user interface language are shown when
// there are any, the English ones otherwise.
class HandbookWindow final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(HandbookWindow)

public:
    explicit HandbookWindow(QWidget* parent = nullptr);

    struct Chapter {
        QString title;
        QString markdown;
        // For the search.
        QString text;
    };
    // The chapters in their order, of the language ("de") or, without it, English.
    [[nodiscard]] static std::vector<Chapter> loadChapters(const QString& language);

    [[nodiscard]] const std::vector<Chapter>& chapters() const;
    void showChapter(int index);
    [[nodiscard]] int currentChapter() const;
    // Lists the chapters that contain the text (all chapters for none), with the number of
    // places; the hits are marked in the chapter shown.
    void search(const QString& text);
    // The next place of the search text in the chapter shown, from the top after the last.
    bool findNext();
    // The rows of the chapter list: their chapters.
    [[nodiscard]] std::vector<int> listedChapters() const;

private:
    void highlightMatches();

    std::vector<Chapter> m_chapters;
    QString m_query;
    int m_current = -1;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_chapterList = nullptr;
    QTextBrowser* m_browser = nullptr;
};

}  // namespace colorizing::app
