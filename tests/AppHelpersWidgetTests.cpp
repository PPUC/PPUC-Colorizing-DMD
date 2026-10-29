// Small pieces of MainWindow's behaviour that are kept outside it so they can be tested: the
// single selection of a list row and the question about unsaved changes.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ActionUndoGroups.h"
#include "ActivityClock.h"
#include "RuntimeFixtures.h"
#include "AppInfo.h"
#include "FrameImageUtils.h"
#include "FrameStatusText.h"
#include "dialogs/AboutDialog.h"
#include "FlowLayout.h"
#include "ListSelection.h"
#include "UnsavedChanges.h"
#include "WheelGuard.h"

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

// A save of a 192x64 project warns about the 96x32 pictures its files still hold.
void testDiscardedExtraPlaneWarning()
{
    expect(app::discardedExtraPlaneWarning({}).isEmpty(), "nothing dropped, no warning");
    const QString warning = app::discardedExtraPlaneWarning({3, 0, 2});
    expect(warning.contains(QStringLiteral("96x32")) &&
               warning.contains(QStringLiteral("frames: 3")) &&
               warning.contains(QStringLiteral("sprites: 2")) &&
               !warning.contains(QStringLiteral("backgrounds")),
           "the warning names what the files hold: " + warning.toStdString());
}

// What one user action records is one undo step: the pushes until it returns to the event
// loop are a group.
void testActionUndoGroups()
{
    namespace edit = colorizing::edit;
    using colorizing::render::Resolution;
    namespace fixtures = colorizing::runtime_fixtures;
    colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::fromSnapshot(
            fixtures::makeProject(colorizing::render::PlaneLayout::Ordinary, 3));
    edit::UndoHistories histories;
    app::ActionUndoGroups groups(histories);
    const auto paint = [&](std::size_t frame, uint16_t color) {
        const uint16_t old = document.colorizedPixel(frame, Resolution::Sd, 2, 2);
        auto command = std::make_unique<edit::PixelEditCommand>(
            edit::EditKind::FramePixels, frame, Resolution::Sd,
            std::vector<edit::PixelChange>{{2, 2, old, color}});
        command->apply(document);
        const edit::HistoryKey key = edit::historyKey(command->target());
        groups.push(key, std::move(command));
        return key;
    };
    // One action on three frames, then another on frame 0.
    const edit::HistoryKey frame0 = paint(0, 0x1111);
    paint(1, 0x2222);
    paint(2, 0x3333);
    QApplication::processEvents();
    paint(0, 0x4444);
    QApplication::processEvents();
    expect(!histories.isGroupOpen(), "the group closes back in the event loop");

    std::size_t ran = 0;
    const auto count = [&](const edit::EditCommand&) { ++ran; };
    histories.undo(frame0, document, count);
    expect(ran == 1 && document.colorizedPixel(0, Resolution::Sd, 2, 2) == 0x1111,
           "the later action is a step of its own");
    ran = 0;
    histories.undo(frame0, document, count);
    expect(ran == 3 && document.colorizedPixel(1, Resolution::Sd, 2, 2) != 0x2222 &&
               document.colorizedPixel(2, Resolution::Sd, 2, 2) != 0x3333,
           "the action on three frames is undone in one step");
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

// The project clock counts working time: up to a minute after each input.
void testActivityClock()
{
    colorizing::app::ActivityClock clock;
    expect(clock.elapsedMs(5000) == 0, "a clock not started counts nothing");
    clock.start(0);
    expect(clock.elapsedMs(30000) == 30000 && !clock.isPaused(30000),
           "it counts while there was input less than a minute ago");
    expect(clock.elapsedMs(600000) == 60000 && clock.isPaused(600000),
           "a pause stops it a minute after the last input");
    clock.activity(600000);
    expect(clock.elapsedMs(610000) == 70000 && !clock.isPaused(610000),
           "input starts it again, without the pause");
    clock.activity(620000);
    clock.activity(640000);
    expect(clock.elapsedMs(640000) == 100000, "inputs in a row count the time between them");
}

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

// Issue #28: the selected frames' duration together, for a scene made of them.
void testSelectedFramesStatusText()
{
    using colorizing::document::ProjectDocument;
    ProjectDocument document = ProjectDocument::fromSnapshot(
        colorizing::runtime_fixtures::makeProject(colorizing::render::PlaneLayout::Ordinary, 4));
    document.setFrameDurationMs(0, 125);
    document.setFrameDurationMs(1, 125);
    document.setFrameDurationMs(3, 1000);
    expect(colorizing::app::selectedFramesStatusText(document, {1}).isEmpty(),
           "one frame: its duration is in the line already");
    expect(colorizing::app::selectedFramesStatusText(document, {0, 1, 3}) ==
               QStringLiteral(" | 3 selected, 1250 ms"),
           "the frames and their durations added up");
}

// The wheel over a combo or spin box in a scrolled section scrolls the section; the value
// changes only in the control that has the focus.
void testWheelGuard()
{
    app::installWheelGuard(QApplication::instance());
    QScrollArea area;
    area.setWidgetResizable(true);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    auto* combo = new QComboBox;
    combo->addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    auto* spin = new QSpinBox;
    layout->addWidget(combo);
    layout->addWidget(spin);
    layout->addSpacing(2000);
    area.setWidget(content);
    area.resize(200, 200);
    area.show();
    QApplication::processEvents();
    // Qt would give a control with the wheel focus policy the focus on a real wheel event,
    // before the guard sees it (a combo box on macOS takes the focus from the tab key only).
    const auto takesWheelFocus = [](const QWidget* widget) {
        return (widget->focusPolicy() & Qt::WheelFocus) == Qt::WheelFocus;
    };
    expect(!takesWheelFocus(combo) && !takesWheelFocus(spin),
           "the wheel does not give a value control the focus");
    // Down, or up (the spin box's value goes up from its minimum).
    const auto wheel = [](QWidget* widget, int delta = -120) {
        QWheelEvent event(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, delta), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(widget, &event);
    };
    // Showing the window gives the first control the focus.
    if (QWidget* focused = QApplication::focusWidget()) {
        focused->clearFocus();
    }
    expect(area.verticalScrollBar()->value() == 0, "the section starts at the top");
    wheel(combo);
    expect(combo->currentIndex() == 0, "the wheel leaves the combo box's value alone");
    expect(area.verticalScrollBar()->value() > 0, "and scrolls the section instead");
    const int scrolled = area.verticalScrollBar()->value();
    wheel(spin);
    expect(spin->value() == 0 && area.verticalScrollBar()->value() > scrolled,
           "the same over a spin box");
    area.activateWindow();
    spin->setFocus();
    QApplication::processEvents();
    if (spin->hasFocus()) {
        const int before = area.verticalScrollBar()->value();
        wheel(spin, 120);
        expect(spin->value() != 0 && area.verticalScrollBar()->value() == before,
               "the spin box with the focus takes the wheel");
    }
}

// The toolbar's groups differ in height (a spin box is taller than a button); each sits in
// the middle of its row, not at the top.
void testFlowLayoutCentersRows()
{
    QWidget container;
    auto* flow = new app::FlowLayout(&container, 4, 4);
    auto* tall = new QWidget;
    tall->setFixedSize(40, 30);
    auto* low = new QWidget;
    low->setFixedSize(40, 20);
    flow->addWidget(tall);
    flow->addWidget(low);
    flow->setGeometry(QRect(0, 0, 200, 30));
    expect(tall->geometry().top() == 0 && low->geometry().top() == 5,
           "a lower item is centered in its row");
    flow->setGeometry(QRect(0, 0, 60, 60));
    expect(low->geometry().top() == 34 && low->geometry().left() == 0,
           "alone in the next row, it starts at that row's top");
}

void runAppHelpersWidgetTests()
{
    testAboutDialog();
    testActivityClock();
    testSelectSingleRow();
    testMayCloseProject();
    testDiscardedExtraPlaneWarning();
    testActionUndoGroups();
    testOriginalShadeColors();
    testSelectedFramesStatusText();
    testWheelGuard();
    testFlowLayoutCentersRows();
}
