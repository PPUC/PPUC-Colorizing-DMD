// Helper lines: the undoable command, the inspector's Helper Lines section and the lines on
// the canvas.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>

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
    for (QPushButton* button : panel.findChildren<QPushButton*>()) {
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
}

}  // namespace

void runHelperLinesWidgetTests()
{
    testCommand();
    testPanel();
    testCanvas();
}
