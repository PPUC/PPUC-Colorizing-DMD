// The question before a frame move that splits sections: its buttons and the answer they give.

#include <cstdlib>
#include <iostream>
#include <string>

#include <QAbstractButton>
#include <QPushButton>

#include "dialogs/SplitSectionsDialog.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: split sections: " << message << '\n';
        std::exit(1);
    }
}

void testAnswers()
{
    for (const char* name : {"sectionsGoWithFrames", "sectionsStay"}) {
        app::SplitSectionsDialog dialog({QStringLiteral("Multiball")});
        expect(dialog.text().contains(QStringLiteral("Multiball")),
               "the question names the section");
        expect(!dialog.choice(), "no answer before a button is clicked");
        auto* button = dialog.findChild<QPushButton*>(QString::fromLatin1(name));
        expect(button != nullptr, std::string("there is the button ") + name);
        button->click();
        const edit::SplitSectionStart expected =
            std::string(name) == "sectionsStay" ? edit::SplitSectionStart::StaysWithFrames
                                                : edit::SplitSectionStart::GoesWithMovedFrames;
        expect(dialog.choice() == expected, std::string("the answer of ") + name);
    }

    app::SplitSectionsDialog dialog({QStringLiteral("Attract"), QStringLiteral("Bonus")});
    expect(dialog.text().contains(QStringLiteral("Attract, Bonus")),
           "the question names every section");
    expect(dialog.defaultButton() == dialog.findChild<QPushButton*>(QStringLiteral("sectionsStay")),
           "keeping the sections is the default, as before the question");
    dialog.button(QMessageBox::Cancel)->click();
    expect(!dialog.choice(), "cancelled: no answer, nothing is moved");
}

}  // namespace

void runSplitSectionsWidgetTests()
{
    testAnswers();
}
