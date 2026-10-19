// Help -> Handbook: the chapters built in, and their search.

#include <cstdlib>
#include <iostream>
#include <string>

#include "HandbookWindow.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: handbook: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

void runHandbookWidgetTests()
{
    const std::vector<app::HandbookWindow::Chapter> english =
        app::HandbookWindow::loadChapters(QStringLiteral("en"));
    expect(english.size() >= 30 && english.front().title == QStringLiteral("Project Files") &&
               !english.front().text.isEmpty(),
           "the English chapters are built in, in their order, with their titles");
    expect(app::HandbookWindow::loadChapters(QStringLiteral("xx")).size() == english.size(),
           "a language without a handbook shows the English one");

    app::HandbookWindow window;
    expect(window.chapters().size() == english.size() &&
               window.listedChapters().size() == english.size() && window.currentChapter() == 0,
           "the window lists every chapter and shows the first");
    window.search(QStringLiteral("dmdserver"));
    const std::vector<int> listed = window.listedChapters();
    bool dmdOutput = false;
    for (int chapter : listed) {
        dmdOutput = dmdOutput || window.chapters()[static_cast<std::size_t>(chapter)].title ==
                                     QStringLiteral("DMD Output");
    }
    expect(!listed.empty() && listed.size() < english.size() && dmdOutput,
           "a search lists the chapters with the text");
    expect(window.findNext(), "and finds its places");
    window.search(QStringLiteral("no such words anywhere"));
    expect(window.listedChapters().empty(), "a text no chapter has lists none");
    window.search(QString());
    expect(window.listedChapters().size() == english.size(), "an empty search lists them all");
}
