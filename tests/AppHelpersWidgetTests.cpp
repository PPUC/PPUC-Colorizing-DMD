// Small pieces of MainWindow's behaviour that are kept outside it so they can be tested: the
// single selection of a list row and the question about unsaved changes.

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QComboBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ActionUndoGroups.h"
#include "ActivityClock.h"
#include "Appearance.h"
#include "DrawingToolIcons.h"
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
// How many pixels of the icon are painted, and whether all of them have the color.
std::pair<int, bool> iconPixels(const QIcon& icon, QIcon::Mode mode, QIcon::State state,
                                const QColor& color)
{
    const QImage image = icon.pixmap(QSize(36, 36), mode, state).toImage();
    int painted = 0;
    bool inColor = true;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() == 255) {
                ++painted;
                inColor = inColor && pixel.rgb() == color.rgb();
            }
        }
    }
    return {painted, inColor};
}

void testDrawingToolIcons()
{
    using colorizing::edit::DrawingTool;
    const QPalette palette = QApplication::palette();
    const QColor text = palette.color(QPalette::ButtonText);
    std::set<QByteArray> pictures;
    for (DrawingTool tool :
         {DrawingTool::Point, DrawingTool::Line, DrawingTool::Rectangle,
          DrawingTool::FilledRectangle, DrawingTool::Circle, DrawingTool::FilledCircle,
          DrawingTool::Ellipse, DrawingTool::FilledEllipse, DrawingTool::PaletteGradient,
          DrawingTool::Selection, DrawingTool::ColorPicker, DrawingTool::FloodFill}) {
        const QIcon icon = app::drawingToolIcon(tool);
        const auto [painted, inColor] = iconPixels(icon, QIcon::Normal, QIcon::Off, text);
        expect(painted > 0 && inColor, "a tool's icon is there, in the button's text color");
        const QImage image = icon.pixmap(QSize(36, 36)).toImage();
        pictures.insert(QByteArray(reinterpret_cast<const char*>(image.constBits()),
                                   static_cast<qsizetype>(image.sizeInBytes())));
    }
    expect(pictures.size() == 12, "every tool has its own icon");
    for (const auto& [outline, filled] :
         std::vector<std::pair<DrawingTool, DrawingTool>>{
             {DrawingTool::Rectangle, DrawingTool::FilledRectangle},
             {DrawingTool::Circle, DrawingTool::FilledCircle},
             {DrawingTool::Ellipse, DrawingTool::FilledEllipse}}) {
        expect(iconPixels(app::drawingToolIcon(filled), QIcon::Normal, QIcon::Off, text).first >
                   2 * iconPixels(app::drawingToolIcon(outline), QIcon::Normal, QIcon::Off, text)
                           .first,
               "a filled shape's icon is filled");
    }
    const QIcon icon = app::drawingToolIcon(DrawingTool::FilledRectangle);
    expect(iconPixels(icon, QIcon::Normal, QIcon::On, app::checkedButtonTextColor(palette)).second,
           "on a checked button the icon has the checked text color");
    expect(iconPixels(icon, QIcon::Disabled, QIcon::Off,
                      palette.color(QPalette::Disabled, QPalette::ButtonText))
               .second,
           "on a disabled button the disabled text color");
    // The palette at the time of painting: the icon follows a change of the theme.
    QPalette changed = palette;
    changed.setColor(QPalette::ButtonText, QColor(10, 200, 30));
    QApplication::setPalette(changed);
    const bool followed =
        iconPixels(icon, QIcon::Normal, QIcon::Off, QColor(10, 200, 30)).second;
    QApplication::setPalette(palette);
    expect(followed, "the icon follows the theme");
    // A checked button shows the icon in the contrast color on its fill, whatever the theme.
    for (app::Theme theme : {app::Theme::Light, app::Theme::Dark}) {
        app::applyTheme(theme);
        QToolButton button;
        button.setCheckable(true);
        button.setChecked(true);
        button.setIcon(app::drawingToolIcon(DrawingTool::FilledRectangle));
        button.setIconSize(QSize(18, 18));
        button.resize(40, 30);
        const QImage shown = button.grab().toImage();
        const QColor middle = shown.pixelColor(shown.width() / 2, shown.height() / 2);
        expect(middle.rgb() == app::checkedButtonTextColor(QApplication::palette()).rgb(),
               "the checked button's icon has its contrast color");
    }
    app::applyTheme(app::Theme::System);
}

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
    // Issue #56: an off pixel has no amber in it and is told apart from the lowest lit shade.
    const QColor off = originalIndexToColor(0, 16);
    expect(off == app::defaultOffPixelColor() && off.red() == off.green() &&
               off.green() == off.blue() && off.lightness() <= 24,
           "shade 0 is a very dark grey");
    expect(off == originalIndexToColor(0, 4), "in 4-color projects too");
    const QColor lowest = originalIndexToColor(1, 16);
    expect(lowest.red() > lowest.blue() + 40 && lowest.lightness() >= off.lightness() + 10,
           "shade 1 of 16 is a visible amber");
    // The setting: only off pixels follow it.
    app::setOffPixelColor(QColor(0, 0, 40));
    expect(originalIndexToColor(0, 16) == QColor(0, 0, 40) &&
               originalIndexToColor(0, 4) == QColor(0, 0, 40) &&
               originalIndexToColor(1, 16) == lowest,
           "the setting colors the off pixels and nothing else");
    app::setOffPixelColor(app::defaultOffPixelColor());
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
    // Issue #47: the window titles carry the version too, so a screenshot shows it.
    expect(colorizing::app::kApplicationTitle == QStringLiteral("PPUC Colorizing DMD ") + version,
           "the window title names the version");
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
    testDrawingToolIcons();
    testSelectedFramesStatusText();
    testWheelGuard();
    testFlowLayoutCentersRows();
}
