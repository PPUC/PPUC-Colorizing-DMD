// Small pieces of MainWindow's behaviour that are kept outside it so they can be tested: the
// single selection of a list row and the question about unsaved changes.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QLabel>
#include <QListWidget>

#include "AppInfo.h"
#include "FrameImageUtils.h"
#include "dialogs/AboutDialog.h"
#include "ListSelection.h"
#include "UnsavedChanges.h"

namespace app = colorizing::app;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<int> selectedRows(const QListWidget& list)
{
    std::vector<int> rows;
    for (int row = 0; row < list.count(); ++row) {
        if (list.item(row)->isSelected()) {
            rows.push_back(row);
        }
    }
    return rows;
}

void testSelectSingleRow()
{
    QListWidget list;
    list.setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (int row = 0; row < 8; ++row) {
        list.addItem(QString::number(row));
    }
    int currentChanges = 0;
    QObject::connect(&list, &QListWidget::currentRowChanged, [&currentChanges](int) {
        ++currentChanges;
    });

    // What a delete or an undo leaves: several rows selected, the current one among them.
    list.setCurrentRow(1);
    list.item(2)->setSelected(true);
    list.item(3)->setSelected(true);
    list.setCurrentRow(5);
    expect(selectedRows(list) != std::vector<int>{5},
           "Qt's setCurrentRow keeps a multi-selection (the reason for selectSingleRow)");

    list.selectAll();
    app::selectSingleRow(list, 6);
    expect(list.currentRow() == 6 && selectedRows(list) == std::vector<int>{6},
           "selectSingleRow leaves the row as the only selected one");

    list.selectAll();
    currentChanges = 0;
    app::selectSingleRow(list, 6);
    expect(list.currentRow() == 6 && selectedRows(list) == std::vector<int>{6} &&
               currentChanges == 0,
           "selectSingleRow on the current row drops the others without a current change");

    app::selectSingleRow(list, -1);
    expect(list.currentRow() == -1 && selectedRows(list).empty(),
           "selectSingleRow(-1) clears the current row and the selection");
    app::selectSingleRow(list, 8);
    expect(list.currentRow() == -1 && selectedRows(list).empty(),
           "a row behind the last one selects nothing");
}

void testMayCloseProject()
{
    using Answer = app::UnsavedChangesAnswer;
    int questions = 0;
    int saves = 0;
    const auto run = [&](bool dirty, Answer answer, bool saved) {
        questions = 0;
        saves = 0;
        return app::mayCloseProject(
            dirty,
            [&]() {
                ++questions;
                return answer;
            },
            [&]() {
                ++saves;
                return saved;
            });
    };
    expect(run(false, Answer::Cancel, false) && questions == 0 && saves == 0,
           "a project without unsaved changes closes without a question");
    expect(run(true, Answer::Discard, false) && questions == 1 && saves == 0,
           "Discard closes without saving");
    expect(!run(true, Answer::Cancel, true) && questions == 1 && saves == 0,
           "Cancel keeps the project open and saves nothing");
    expect(run(true, Answer::Save, true) && questions == 1 && saves == 1,
           "Save closes once the project was written");
    expect(!run(true, Answer::Save, false) && questions == 1 && saves == 1,
           "Save keeps the project open when it was not written (path prompt cancelled, "
           "save failed)");
}

// The reference view of a 16-color project shows 16 shades, not each shade modulo 4.
void testOriginalShadeColors()
{
    using app::originalIndexToColor;
    for (uint8_t shade = 0; shade < 4; ++shade) {
        expect(originalIndexToColor(shade) == originalIndexToColor(shade, 4) &&
                   originalIndexToColor(static_cast<uint8_t>(shade * 5), 16) ==
                       originalIndexToColor(shade, 4),
               "shades 0, 5, 10, 15 of 16 are the four colors of a 4-color project");
    }
    int previous = -1;
    for (uint8_t shade = 0; shade < 16; ++shade) {
        const int brightness = originalIndexToColor(shade, 16).lightness();
        expect(brightness > previous, "16 shades get 16 colors, brighter and brighter");
        previous = brightness;
    }
    expect(originalIndexToColor(7, 16) != originalIndexToColor(3, 16) &&
               originalIndexToColor(7, 16) != originalIndexToColor(3, 4),
           "shade 7 is not shown as shade 3");
}

}  // namespace

// Help -> About names the version testers report against.
void testAboutDialog()
{
    const QString version = colorizing::app::kApplicationVersion;
    const QStringList parts = version.split(QLatin1Char('.'));
    expect(parts.size() == 3 && parts[0].toInt() >= 3, "the version has three parts");
    colorizing::app::AboutDialog dialog;
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("aboutVersion"));
    expect(label && label->text() == QStringLiteral("Version ") + version &&
               dialog.windowTitle() == QStringLiteral("About PPUC Colorizing DMD"),
           "the About dialog shows the name and the version");
}

void runAppHelpersWidgetTests()
{
    testAboutDialog();
    testSelectSingleRow();
    testMayCloseProject();
    testOriginalShadeColors();
}
