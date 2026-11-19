// Painting dynamic zones (issue #58): the right button paints them whatever the setting; the
// left button paints them in the Dynamic mode (the default) or colors (the Win32 editor's way),
// where Mask can be on beside Dynamic. The zones can be left off the original.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>

#include "CanvasEditController.h"
#include "DynamicZoneMode.h"
#include "FramePreviewWidget.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/EditCommand.h"

namespace {

using colorizing::app::CanvasEditContext;
using colorizing::app::CanvasEditController;
using colorizing::app::CanvasEditLayer;
using colorizing::app::CanvasEditOutputs;
using colorizing::app::CanvasModes;
using colorizing::app::CanvasStroke;
using colorizing::app::DynamicSetLabel;
using colorizing::app::DynamicZoneOptions;
using colorizing::app::dynamicSetLabels;
using colorizing::app::FramePreviewWidget;
using colorizing::app::paintsDynamicZonesWithRightButton;
using colorizing::edit::DrawingTool;
using colorizing::render::CanvasHit;
using colorizing::render::CanvasRegion;
using colorizing::render::Resolution;
namespace document = colorizing::document;
namespace edit = colorizing::edit;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: dynamic zones: " << message << '\n';
        std::exit(1);
    }
}

document::ProjectDocument makeDocument()
{
    document::ProjectSnapshot snapshot = document::ProjectDocument::createEmpty({
                                             "dynamic_zones",
                                             1,
                                             true,
                                         })
                                             .toSnapshot();
    // A frame with dynamic data and no zone yet.
    document::FrameDynamicData dynamicData;
    dynamicData.maskSd = document::makeIndexedFrame(colorizing::render::kSdFrameSize, 255);
    dynamicData.maskHd = document::makeIndexedFrame(colorizing::render::kHdFrameSize, 255);
    dynamicData.colorsSd.assign(document::kMaxDynamicSetsPerFrame * 4, 0);
    dynamicData.colorsHd.assign(document::kMaxDynamicSetsPerFrame * 4, 0);
    snapshot.frames.at(0).dynamicData = dynamicData;
    return document::ProjectDocument::fromSnapshot(snapshot);
}

// A point of the widget over the pixel of the region.
QPoint pointOver(const FramePreviewWidget& widget, CanvasRegion region, int x, int y)
{
    for (int py = 0; py < widget.height(); ++py) {
        for (int px = 0; px < widget.width(); ++px) {
            const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(px, py));
            if (hit && hit->region == region && hit->resolution == Resolution::Sd &&
                hit->x == x && hit->y == y) {
                return QPoint(px, py);
            }
        }
    }
    expect(false, "no point over pixel " + std::to_string(x) + "," + std::to_string(y));
    return {};
}

void send(FramePreviewWidget& widget,
          QEvent::Type type,
          QPoint at,
          Qt::MouseButton button,
          Qt::MouseButtons buttons,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, QPointF(at), QPointF(at), QPointF(widget.mapToGlobal(at)), button,
                      buttons, modifiers);
    QApplication::sendEvent(&widget, &event);
}

void testLayers()
{
    const DynamicZoneOptions modeWay;
    DynamicZoneOptions win32Way;
    win32Way.leftPaintsColors = true;
    CanvasModes dynamicOn;
    dynamicOn.dynamicZones = true;
    CanvasModes maskAndDynamic = dynamicOn;
    maskAndDynamic.comparisonMask = true;

    // Default: Dynamic is a mode, as before.
    expect(canvasEditLayer(dynamicOn, modeWay, CanvasStroke::Left, true) ==
               CanvasEditLayer::DynamicMask,
           "by default the left button paints zones with Dynamic on");
    expect(canvasEditLayer(CanvasModes{}, modeWay, CanvasStroke::Left, true) ==
               CanvasEditLayer::FramePixels,
           "and colors with Dynamic off");
    expect(!maskWithDynamicZones(modeWay), "Mask and Dynamic are one at a time by default");
    // The right button paints zones whatever the setting.
    for (const DynamicZoneOptions& options : {modeWay, win32Way}) {
        expect(canvasEditLayer(dynamicOn, options, CanvasStroke::Right, true) ==
                       CanvasEditLayer::DynamicMask &&
                   canvasEditLayer(maskAndDynamic, options, CanvasStroke::Right, true) ==
                       CanvasEditLayer::DynamicMask,
               "the right button paints zones");
    }
    // The Win32 editor's way: Dynamic only shows the zones.
    expect(canvasEditLayer(dynamicOn, win32Way, CanvasStroke::Left, true) ==
               CanvasEditLayer::FramePixels,
           "the left button paints colors with Dynamic on");
    expect(maskWithDynamicZones(win32Way), "Mask can be on beside Dynamic");
    expect(canvasEditLayer(maskAndDynamic, win32Way, CanvasStroke::Left, true) ==
                   CanvasEditLayer::FramePixels &&
               canvasEditLayer(maskAndDynamic, win32Way, CanvasStroke::Left, false) ==
                   CanvasEditLayer::ComparisonMask,
           "with Mask, colors on the colorized picture and the mask on the original");
    CanvasModes maskOnBackground = maskAndDynamic;
    maskOnBackground.backgroundImage = true;
    expect(canvasEditLayer(maskOnBackground, win32Way, CanvasStroke::Left, true) ==
               CanvasEditLayer::BackgroundImage,
           "or the background's colors while it is edited");
    expect(canvasEditLayer(maskAndDynamic, modeWay, CanvasStroke::Left, true) ==
               CanvasEditLayer::ComparisonMask,
           "by default Mask keeps the colorized picture as it is");
    // The zones on the original.
    expect(dynamicZonesOnOriginal(dynamicOn, modeWay) &&
               !dynamicZonesOnOriginal(maskAndDynamic, win32Way),
           "on the original unless the mask is shown there");
    DynamicZoneOptions colorizedOnly;
    colorizedOnly.onOriginal = false;
    expect(!dynamicZonesOnOriginal(dynamicOn, colorizedOnly), "or they are left off it");
    expect(paintsDynamicZonesWithRightButton(DrawingTool::FilledRectangle) &&
               paintsDynamicZonesWithRightButton(DrawingTool::FloodFill) &&
               paintsDynamicZonesWithRightButton(DrawingTool::ColorPicker) &&
               !paintsDynamicZonesWithRightButton(DrawingTool::Selection) &&
               !paintsDynamicZonesWithRightButton(DrawingTool::PaletteGradient),
           "the tools that paint zones with the right button");
}

// The canvas starts a stroke of the right button where its handler lets it, and tells the
// window which button and which frame the stroke is of while the stroke's handlers run.
void testRightButtonStroke()
{
    document::ProjectDocument document = makeDocument();
    FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);

    bool allowed = false;
    int asked = 0;
    widget.setRightPressHandler([&](CanvasHit) {
        ++asked;
        return allowed;
    });
    std::vector<Qt::MouseButton> clickButtons;
    std::optional<CanvasRegion> clickRegion;
    widget.setPixelClickedHandler([&](CanvasHit, bool) {
        clickButtons.push_back(widget.strokeButton());
        clickRegion = widget.strokeRegion();
    });
    widget.setOriginalClickedHandler([&](CanvasHit, bool) {
        clickButtons.push_back(widget.strokeButton());
        clickRegion = widget.strokeRegion();
    });
    std::vector<Qt::MouseButton> finishButtons;
    bool finishErase = false;
    widget.setStrokeFinishedHandler([&](CanvasHit, CanvasHit, bool erase) {
        finishButtons.push_back(widget.strokeButton());
        finishErase = erase;
    });

    const QPoint start = pointOver(widget, CanvasRegion::ColorizedFrame, 10, 10);
    const QPoint end = pointOver(widget, CanvasRegion::ColorizedFrame, 20, 12);
    send(widget, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
    send(widget, QEvent::MouseButtonRelease, start, Qt::RightButton, Qt::NoButton);
    expect(asked == 1 && clickButtons.empty() && finishButtons.empty(),
           "a right press the window refuses paints nothing");

    allowed = true;
    send(widget, QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton,
         Qt::ShiftModifier);
    expect(clickButtons == std::vector<Qt::MouseButton>{Qt::RightButton} &&
               clickRegion == CanvasRegion::ColorizedFrame,
           "the right press clicks as one of the right button on the colorized frame");
    // A left press while the right stroke goes on starts nothing.
    send(widget, QEvent::MouseButtonPress, end, Qt::LeftButton,
         Qt::LeftButton | Qt::RightButton);
    expect(clickButtons.size() == 1, "the left button does not cut into the right stroke");
    send(widget, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::RightButton);
    send(widget, QEvent::MouseMove, end, Qt::NoButton, Qt::RightButton);
    send(widget, QEvent::MouseButtonRelease, end, Qt::RightButton, Qt::NoButton);
    expect(finishButtons == std::vector<Qt::MouseButton>{Qt::RightButton} && finishErase,
           "the release finishes the right stroke, Shift erasing");
    expect(widget.strokeButton() == Qt::NoButton && !widget.strokeRegion(),
           "no stroke afterwards");

    const QPoint original = pointOver(widget, CanvasRegion::OriginalFrame, 5, 5);
    send(widget, QEvent::MouseButtonPress, original, Qt::RightButton, Qt::RightButton);
    send(widget, QEvent::MouseButtonRelease, original, Qt::RightButton, Qt::NoButton);
    expect(clickButtons.size() == 2 && clickButtons.back() == Qt::RightButton &&
               clickRegion == CanvasRegion::OriginalFrame,
           "a right stroke on the original");

    send(widget, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    expect(clickButtons.back() == Qt::LeftButton, "a left stroke is one of the left button");
    send(widget, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    expect(finishButtons.back() == Qt::LeftButton && widget.strokeButton() == Qt::NoButton,
           "as before");

    QContextMenuEvent menu(QContextMenuEvent::Mouse, start, widget.mapToGlobal(start));
    menu.ignore();
    QApplication::sendEvent(&widget, &menu);
    expect(menu.isAccepted(), "the canvas takes the context menu event: no menu opens");
}

struct Editing {
    document::ProjectDocument document = makeDocument();
    FramePreviewWidget widget;
    std::vector<std::unique_ptr<edit::EditCommand>> commands;
    CanvasEditController controller;
    CanvasEditContext context;

    Editing()
        : controller(makeOutputs())
    {
        widget.resize(640, 420);
        widget.setDocument(&document);
        widget.setFrameIndex(0);
        context.document = &document;
        context.currentFrame = 0;
        context.selectedFrames = {0};
        context.layer = CanvasEditLayer::DynamicMask;
        context.tool = DrawingTool::Point;
        context.dynamicSet = 3;
    }

    CanvasEditOutputs makeOutputs()
    {
        CanvasEditOutputs outputs;
        outputs.commit = [this](std::unique_ptr<edit::EditCommand> command) {
            command->apply(document);
            commands.push_back(std::move(command));
        };
        return outputs;
    }

    [[nodiscard]] CanvasHit hit(CanvasRegion region, int x, int y) const
    {
        return *widget.hitTestAt(pointOver(widget, region, x, y));
    }
};

// Zones left off the original are not painted there either.
void testZonesOffTheOriginal()
{
    Editing editing;
    editing.controller.originalPixelClicked(
        editing.context, editing.hit(CanvasRegion::OriginalFrame, 4, 4), false);
    expect(editing.document.dynamicMaskPixel(0, Resolution::Sd, 4, 4) == 3,
           "by default a zone is painted on the original");

    editing.context.dynamicZonesOnOriginal = false;
    editing.controller.originalPixelClicked(
        editing.context, editing.hit(CanvasRegion::OriginalFrame, 6, 4), false);
    editing.context.tool = DrawingTool::FilledRectangle;
    editing.controller.strokeFinished(editing.context,
                                      editing.hit(CanvasRegion::OriginalFrame, 8, 8),
                                      editing.hit(CanvasRegion::OriginalFrame, 10, 10), false);
    expect(editing.document.dynamicMaskPixel(0, Resolution::Sd, 6, 4) == 255 &&
               editing.document.dynamicMaskPixel(0, Resolution::Sd, 9, 9) == 255,
           "not when the zones are left off the original");
    editing.controller.strokeFinished(editing.context,
                                      editing.hit(CanvasRegion::ColorizedFrame, 8, 8),
                                      editing.hit(CanvasRegion::ColorizedFrame, 10, 10), false);
    expect(editing.document.dynamicMaskPixel(0, Resolution::Sd, 9, 9) == 3,
           "the colorized picture still paints them");
}

}  // namespace

// Issue #76: pixels of a set that touch at a corner are one area with one number; another set
// beside them has its own.
void testSetLabelsJoinDiagonals()
{
    constexpr int N = 255;
    // Set 0 and set 1 as two diagonal stairs side by side, as in the report; set 2 apart.
    const std::vector<int> sets{
        N, 0, 1, N, N,
        0, 1, N, N, N,
        N, N, N, N, 2,
    };
    const std::vector<DynamicSetLabel> labels = dynamicSetLabels(sets, 5, 3);
    expect(labels.size() == 3, "one number per set: " + std::to_string(labels.size()));
    expect(labels[0].set == 0 && labels[0].at.x == 1 && labels[0].at.y == 0 &&
               labels[1].set == 1 && labels[1].at.x == 2 && labels[1].at.y == 0 &&
               labels[2].set == 2 && labels[2].at.x == 4 && labels[2].at.y == 2,
           "each at its area's first pixel from the top left");
    const std::vector<int> apart{0, N, 0, N, N, N, 0, N, 0};
    expect(dynamicSetLabels(apart, 3, 3).size() == 4, "pixels that do not touch stay apart");
}

void runDynamicZoneWidgetTests()
{
    testSetLabelsJoinDiagonals();
    testLayers();
    testRightButtonStroke();
    testZonesOffTheOriginal();
}
