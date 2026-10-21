#include "HandbookWindow.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

#include "Translations.h"
#include "WindowGeometry.h"

namespace colorizing::app {

namespace {

const QString kRoot = QStringLiteral(":/handbook/");

}  // namespace

std::vector<HandbookWindow::Chapter> HandbookWindow::loadChapters(const QString& language)
{
    QDir directory(kRoot + language);
    if (language.isEmpty() || !directory.exists()) {
        directory = QDir(kRoot + QStringLiteral("en"));
    }
    std::vector<Chapter> chapters;
    // NN-name.md, in the order of their numbers; README.md is the index for readers of the
    // sources.
    const QStringList files =
        directory.entryList({QStringLiteral("[0-9]*.md")}, QDir::Files, QDir::Name);
    for (const QString& name : files) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        Chapter chapter;
        chapter.markdown = QString::fromUtf8(file.readAll());
        const qsizetype end = chapter.markdown.indexOf(QLatin1Char('\n'));
        chapter.title = chapter.markdown.left(end).remove(QLatin1Char('#')).trimmed();
        QTextDocument document;
        document.setMarkdown(chapter.markdown);
        chapter.text = document.toPlainText();
        chapters.push_back(std::move(chapter));
    }
    return chapters;
}

HandbookWindow::HandbookWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("ColorizingDMD Handbook"));
    resize(1000, 720);
    keepWindowGeometry(this, QStringLiteral("handbook"));
    m_chapters = loadChapters(currentLanguage());

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search the handbook"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setToolTip(tr("Lists the chapters with the text; Enter goes to its next place"));
    m_chapterList = new QListWidget(this);
    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);

    auto* side = new QWidget(this);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->addWidget(m_searchEdit);
    sideLayout->addWidget(m_chapterList, 1);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(side);
    splitter->addWidget(m_browser);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 740});
    auto* layout = new QHBoxLayout(this);
    layout->addWidget(splitter);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        search(text);
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() { findNext(); });
    connect(m_chapterList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            showChapter(m_chapterList->item(row)->data(Qt::UserRole).toInt());
        }
    });
    search(QString());
}

const std::vector<HandbookWindow::Chapter>& HandbookWindow::chapters() const
{
    return m_chapters;
}

void HandbookWindow::showChapter(int index)
{
    if (index < 0 || index >= static_cast<int>(m_chapters.size())) {
        return;
    }
    if (index != m_current) {
        m_current = index;
        m_browser->setMarkdown(m_chapters[static_cast<std::size_t>(index)].markdown);
    }
    highlightMatches();
}

int HandbookWindow::currentChapter() const
{
    return m_current;
}

void HandbookWindow::search(const QString& text)
{
    m_query = text.trimmed();
    const int shown = m_current;
    QSignalBlocker blocker(m_chapterList);
    m_chapterList->clear();
    int rowOfShown = -1;
    for (std::size_t index = 0; index < m_chapters.size(); ++index) {
        const Chapter& chapter = m_chapters[index];
        QString label = chapter.title;
        if (!m_query.isEmpty()) {
            const qsizetype count = chapter.text.count(m_query, Qt::CaseInsensitive) +
                                    chapter.title.count(m_query, Qt::CaseInsensitive);
            if (count == 0) {
                continue;
            }
            label += QStringLiteral(" (%1)").arg(count);
        }
        auto* item = new QListWidgetItem(label, m_chapterList);
        item->setData(Qt::UserRole, static_cast<int>(index));
        if (static_cast<int>(index) == shown) {
            rowOfShown = m_chapterList->count() - 1;
        }
    }
    // The chapter shown stays when it has the text; otherwise the first that has it.
    const int row = rowOfShown >= 0 ? rowOfShown : (m_chapterList->count() > 0 ? 0 : -1);
    m_chapterList->setCurrentRow(row);
    if (row >= 0) {
        showChapter(m_chapterList->item(row)->data(Qt::UserRole).toInt());
        if (!m_query.isEmpty()) {
            m_browser->moveCursor(QTextCursor::Start);
            findNext();
        }
    } else {
        highlightMatches();
    }
}

bool HandbookWindow::findNext()
{
    if (m_query.isEmpty()) {
        return false;
    }
    if (m_browser->find(m_query)) {
        return true;
    }
    m_browser->moveCursor(QTextCursor::Start);
    return m_browser->find(m_query);
}

std::vector<int> HandbookWindow::listedChapters() const
{
    std::vector<int> rows;
    for (int row = 0; row < m_chapterList->count(); ++row) {
        rows.push_back(m_chapterList->item(row)->data(Qt::UserRole).toInt());
    }
    return rows;
}

void HandbookWindow::highlightMatches()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!m_query.isEmpty()) {
        QTextDocument* document = m_browser->document();
        QTextCursor cursor(document);
        while (true) {
            cursor = document->find(m_query, cursor);
            if (cursor.isNull()) {
                break;
            }
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            selection.format.setBackground(QColor(255, 220, 0));
            selection.format.setForeground(QColor(0, 0, 0));
            selections << selection;
        }
    }
    m_browser->setExtraSelections(selections);
}

}  // namespace colorizing::app
