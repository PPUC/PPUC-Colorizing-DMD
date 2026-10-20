// Helper lines: the undoable command, the inspector's Helper Lines section and the lines on
// the canvas.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include "Appearance.h"
#include "FramePreviewWidget.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/HelperLines.h"
#include "panels/HelperLinesPanel.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: helper lines: " << message << '\n';
        std::exit(1);
    }
}

document::HelperLine horizontal(const std::string& name, uint32_t row)
{
    document::HelperLine line;
    line.name = name;
    line.position = row;
    return line;
}

void testCommand()
{
    document::ProjectDocument document = document::ProjectDocument::createEmpty({"lines", 1});
    document.markClean();
    expect(!edit::setHelperLines(document, {}), "no command when nothing changes");
    std::unique_ptr<edit::EditCommand> command =
        edit::setHelperLines(document, {horizontal("Score", 8)});
    expect(command && document.helperLines().size() == 1 && document.isDirty() &&
               command->target().kind == edit::EditKind::HelperLines,
           "adding a line is a command of its own history and changes the project");
    command->revert(document);
    expect(document.helperLines().empty(), "undo takes it away");
    command->apply(document);
    expect(document.helperLines() == std::vector<document::HelperLine>{horizontal("Score", 8)},
           "redo brings it back");
    expect(document.toSnapshot().helperLines == document.helperLines(),
           "the lines are saved with the project");
}

void testPanel()
{
    document::ProjectDocument document = document::ProjectDocument::createEmpty({"lines", 1});
    app::HelperLinesPanel panel;
    std::vector<std::pair<std::size_t, bool>> toggled;
    int added = -1;
    panel.onVisibilityChanged = [&](std::size_t line, bool visible) {
        toggled.emplace_back(line, visible);
    };
    panel.onAddRequested = [&](bool vertical) { added = vertical ? 1 : 0; };
    document::HelperLine hidden = horizontal("Ball", 20);
    hidden.vertical = true;
    hidden.visible = false;
    panel.setLines({horizontal("Score", 8), hidden}, &document);
    auto* list = panel.findChild<QListWidget*>();
    expect(list && list->count() == 2 && list->item(0)->checkState() == Qt::Checked &&
               list->item(1)->checkState() == Qt::Unchecked &&
               list->item(1)->text().contains(QStringLiteral("Ball")) &&
               list->item(1)->text().contains(QStringLiteral("vertical at 20")),
           "the section lists the lines with their visibility");
    expect(toggled.empty(), "filling the list reports nothing");
    list->item(1)->setCheckState(Qt::Checked);
    expect(toggled.size() == 1 && toggled[0] == std::make_pair(std::size_t{1}, true),
           "a check box shows or hides its line");
    for (QToolButton* button : panel.findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Vertical")) {
            button->click();
        }
    }
    expect(added == 1, "Vertical asks for a vertical line");
}

// The canvas, rendered: the color of helper lines somewhere in the picture.
bool showsHelperLine(app::FramePreviewWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();
    const QColor wanted = app::helperLineColor();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (std::abs(color.red() - wanted.red()) < 30 &&
                std::abs(color.green() - wanted.green()) < 30 &&
                std::abs(color.blue() - wanted.blue()) < 30) {
                return true;
            }
        }
    }
    return false;
}

void testCanvas()
{
    document::ProjectDocument document = document::ProjectDocument::createEmpty({"lines", 1});
    app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(render::Resolution::Sd);
    widget.setFrameIndex(0);
    expect(!showsHelperLine(widget), "no lines, nothing drawn");
    document::HelperLine line = horizontal(std::string(), 16);
    document.setHelperLines({line});
    expect(showsHelperLine(widget), "a visible line is drawn on the canvas");
    line.visible = false;
    document.setHelperLines({line});
    expect(!showsHelperLine(widget), "a hidden one is not");

    // A named line's name lies outside the frame: the frame leaves room for it at zoom 1.
    line.visible = true;
    line.name = "Score baseline";
    document.setHelperLines({line});
    const QMarginsF margins = widget.helperLineMargins();
    expect(margins.left() > app::FramePreviewWidget::kHelperLineOverhang,
           "a horizontal line's name takes room left of the frame");
    for (const auto& view : widget.regionViews()) {
        expect(view.origin.x() - view.viewport.left() >= margins.left() - 1.0,
               "every frame keeps that room free, so the name covers no pixel");
    }

    // Show Helper Lines off hides them all, keeping each one's own visibility.
    widget.setHelperLinesShown(false);
    expect(!showsHelperLine(widget) && widget.helperLineMargins().isNull() &&
               document.helperLines()[0].visible,
           "all lines hidden together, the frame takes their room");
    widget.setHelperLinesShown(true);
    expect(showsHelperLine(widget), "and shown again");

    // Zoomed in, a vertical line scrolled out of the shown part takes its name with it.
    document::HelperLine column = horizontal("Left edge", 1);
    column.vertical = true;
    document.setHelperLines({column});
    widget.setZoom(8.0);
    widget.setViewOffset(QPointF(0.0, 0.0));
    expect(showsHelperLine(widget), "zoomed in, the line in the shown part is drawn");
    widget.setViewOffset(QPointF(1.0, 0.0));
    expect(!showsHelperLine(widget), "scrolled away from it, neither the line nor its name is");
}

void sendMouse(QWidget& widget, QEvent::Type type, QPointF point, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, point, widget.mapToGlobal(point), Qt::LeftButton, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

// A line is dragged by its part outside the frame or by its name; the canvas only reports
// where it goes.
void testDrag()
{
    document::ProjectDocument document = document::ProjectDocument::createEmpty({"lines", 1});
    app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(render::Resolution::Sd);
    widget.setFrameIndex(0);
    document.setHelperLines({horizontal("Score", 16)});
    struct Move {
        std::size_t line;
        uint32_t position;
        bool finished;
    };
    std::vector<Move> moves;
    widget.setHelperLineMoveHandler([&](std::size_t line, uint32_t position, bool finished) {
        moves.push_back({line, position, finished});
    });
    const auto views = widget.regionViews();
    expect(!views.empty(), "the canvas shows the frame");
    const auto& view = views.front();
    const double rowHeight = view.contentSize.height() / 32.0;
    const QPointF outside(view.origin.x() - 4.0, view.origin.y() + 16 * rowHeight);
    const QPointF inside(view.origin.x() + 20.0, view.origin.y() + 16 * rowHeight);
    expect(widget.helperLineHandleAt(outside) == std::optional<std::size_t>(0),
           "the line's part left of the frame is its handle");
    expect(!widget.helperLineHandleAt(inside), "over the frame a click paints");

    sendMouse(widget, QEvent::MouseButtonPress, outside, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseMove, QPointF(outside.x(), view.origin.y() + 8.3 * rowHeight),
              Qt::LeftButton);
    expect(!moves.empty() && moves.back().line == 0 && moves.back().position == 8 &&
               !moves.back().finished,
           "dragging reports the nearest row border");
    expect(document.helperLines()[0].position == 16, "the document is not changed meanwhile");
    sendMouse(widget, QEvent::MouseButtonRelease,
              QPointF(outside.x(), view.origin.y() + 8.3 * rowHeight), Qt::NoButton);
    expect(moves.back().position == 8 && moves.back().finished, "the drop reports the row");

    moves.clear();
    sendMouse(widget, QEvent::MouseButtonPress, outside, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseMove, QPointF(outside.x(), view.origin.y() + 30 * rowHeight),
              Qt::LeftButton);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&widget, &escape);
    expect(!moves.empty() && moves.back().position == 16 && moves.back().finished,
           "Escape puts the line back");
    const std::size_t reported = moves.size();
    sendMouse(widget, QEvent::MouseButtonRelease, outside, Qt::NoButton);
    expect(moves.size() == reported, "and the release after it reports nothing");
}

}  // namespace

void runHelperLinesWidgetTests()
{
    testCommand();
    testPanel();
    testCanvas();
    testDrag();
}
