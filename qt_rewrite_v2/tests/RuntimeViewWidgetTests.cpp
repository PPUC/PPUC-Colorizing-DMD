// The Runtime view of the canvas: its state (app/RuntimeView) and what FramePreviewWidget
// does while it is on.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "FramePreviewWidget.h"
#include "RuntimeView.h"
#include "runtime/RuntimePreview.h"

using colorizing::app::FramePreviewWidget;
using colorizing::app::RuntimeView;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::Rgb565Frame;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::render::CanvasHit;
using colorizing::render::CanvasRegion;
using colorizing::render::PlaneLayout;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;
using colorizing::runtime::PlaneRequest;
using colorizing::runtime::RuntimePreview;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: runtime view: " << message << '\n';
        std::exit(1);
    }
}

uint16_t frameColor(std::size_t frame)
{
    return static_cast<uint16_t>(0x0841 * (frame + 1));
}

// Frames with one flat color each and ROM frames that differ; a 256x64-original project has
// its 256x64 frames.
ProjectDocument makeDocument(PlaneLayout layout, std::size_t frameCount)
{
    ProjectSnapshot snapshot;
    snapshot.romName = "runtime_view";
    snapshot.colorCount = 4;
    snapshot.cromRevision = 20;
    snapshot.geometry = ProjectGeometry(layout);
    for (std::size_t f = 0; f < frameCount; ++f) {
        ProjectFrameSnapshot frame;
        frame.originalSd = makeIndexedFrame(snapshot.geometry.romFrameSize());
        frame.originalSd.pixels[f] = 1;
        frame.originalSd.pixels[f + 500] = 3;
        frame.colorizedSd =
            makeRgb565Frame(snapshot.geometry.planeSize(Resolution::Sd), frameColor(f));
        if (layout == PlaneLayout::NativeExtra) {
            frame.colorizedHd = makeRgb565Frame(snapshot.geometry.planeSize(Resolution::Hd),
                                                static_cast<uint16_t>(0x1082 * (f + 1)));
        }
        frame.comparisonMaskId = 255;
        frame.triggerId = colorizing::document::kNoTriggerId;
        frame.durationMs = 100;
        snapshot.frames.push_back(std::move(frame));
    }
    snapshot.comparisonMasks.push_back(makeIndexedFrame(snapshot.geometry.romFrameSize()));
    return ProjectDocument::fromSnapshot(snapshot);
}

QImage renderWidget(FramePreviewWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();
    return image;
}

// A widget position that shows the pixel of the region.
QPoint positionOf(const FramePreviewWidget& widget, CanvasRegion region, int x, int y)
{
    for (int py = 0; py < widget.height(); ++py) {
        for (int px = 0; px < widget.width(); ++px) {
            const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(px, py));
            if (hit && hit->region == region && hit->x == x && hit->y == y) {
                return QPoint(px, py);
            }
        }
    }
    expect(false, "the canvas shows the pixel that is looked for");
    return {};
}

Size colorizedRegionSize(const FramePreviewWidget& widget)
{
    Size size;
    for (int py = 0; py < widget.height(); ++py) {
        for (int px = 0; px < widget.width(); ++px) {
            const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(px, py));
            if (hit && hit->region == CanvasRegion::ColorizedFrame) {
                size.width = std::max(size.width, hit->x + 1);
                size.height = std::max(size.height, hit->y + 1);
            }
        }
    }
    return size;
}

void click(FramePreviewWidget& widget, QPoint position)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(position), QPointF(position),
                      QPointF(position), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position), QPointF(position),
                        QPointF(position), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

void testState()
{
    RuntimeView view;
    expect(!view.isEnabled() && !view.isPlaying(), "the view starts off");
    expect(view.rows() == PlaneRequest::Rows32, "the view starts on the 32-row plane");
    view.setRows(PlaneRequest::Both);
    expect(view.rows() == PlaneRequest::Rows32, "only one plane can be shown");
    view.setRows(PlaneRequest::Rows64);

    const ProjectGeometry ordinary(PlaneLayout::Ordinary);
    const ProjectGeometry big(PlaneLayout::NativeExtra);
    const ProjectGeometry native(PlaneLayout::NativeOriginal);
    expect(view.rowsFor(ordinary) == PlaneRequest::Rows64 &&
               view.layoutPlane(ordinary) == Resolution::Hd,
           "64 rows of an ordinary project are laid out as its 256x64 plane");
    view.setRows(PlaneRequest::Rows32);
    expect(view.rowsFor(ordinary) == PlaneRequest::Rows32 &&
               view.layoutPlane(ordinary) == Resolution::Sd,
           "32 rows of an ordinary project are laid out as its 128x32 plane");
    expect(view.rowsFor(big) == PlaneRequest::Rows64 && view.layoutPlane(big) == Resolution::Hd,
           "a 256x64-original project shows its one plane");
    expect(view.rowsFor(native) == PlaneRequest::Rows64 &&
               view.layoutPlane(native) == Resolution::Sd,
           "a 192x64-original project shows its one plane");
}

void testRendersOnlyWhenSomethingChanged()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, 3);
    RuntimeView view;
    view.setEnabled(true);
    const Rgb565Frame* image = view.image(document, 1);
    expect(image && image->size == Size{128, 32} && image->pixels[77] == frameColor(1),
           "the view returns libserum's 32-row plane of the frame");
    expect(view.message(document, 1).empty(), "a frame with a picture has no message");
    expect(view.frame(document, 1).identified && view.renderCount() == 1,
           "asking again for the same frame does not render again");

    document.setColorizedPixel(1, Resolution::Sd, 5, 2, 0xf800);
    image = view.image(document, 1);
    expect(view.renderCount() == 2 && image && image->pixels[2 * 128 + 5] == 0xf800,
           "an edit of the document is rendered");
    (void)view.image(document, 2);
    expect(view.renderCount() == 3, "another frame is rendered");
    view.setRows(PlaneRequest::Rows64);
    image = view.image(document, 2);
    expect(view.renderCount() == 4 && image && image->size == Size{256, 64},
           "another plane is rendered: the 64-row one, upscaled by libserum");
    expect(!RuntimePreview::instance().isOpen(), "stills leave nothing loaded in libserum");

    // A frame of a 256x64-original project whose 256x64 data is missing has no picture.
    ProjectSnapshot bigSnapshot = makeDocument(PlaneLayout::NativeExtra, 2).toSnapshot();
    bigSnapshot.frames[1].colorizedHd.reset();
    const ProjectDocument big = ProjectDocument::fromSnapshot(bigSnapshot);
    expect(view.image(big, 0) != nullptr, "a 256x64 frame has its picture");
    expect(view.image(big, 1) == nullptr &&
               view.message(big, 1).find("no picture") != std::string::npos,
           "a frame libserum shows nothing for has a message instead of a picture");
    // Shorter than the runtime's skip limit and not colorized: saved as inactive, which
    // libserum does not look for.
    bigSnapshot.frames[1].durationMs = 5;
    const ProjectDocument skipped = ProjectDocument::fromSnapshot(bigSnapshot);
    expect(view.image(skipped, 1) == nullptr &&
               view.message(skipped, 1).find("does not identify") != std::string::npos,
           "a frame libserum skips is reported as not identified");
}

void testPlayback()
{
    ProjectSnapshot snapshot = makeDocument(PlaneLayout::Ordinary, 60).toSnapshot();
    // Frame 2 rotates between its color and another one.
    snapshot.frames[2].colorRotationsSd.assign(4 * 64, 0);
    snapshot.frames[2].colorRotationsSd[0] = 2;
    snapshot.frames[2].colorRotationsSd[1] = 40;
    snapshot.frames[2].colorRotationsSd[2] = frameColor(2);
    snapshot.frames[2].colorRotationsSd[3] = 0x07e0;
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    std::vector<std::size_t> order;
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        order.push_back(f);
    }

    RuntimePreview& preview = RuntimePreview::instance();
    RuntimeView view;
    view.setEnabled(true);
    expect(view.rotate() == 0, "nothing rotates while the view does not play");
    view.startPlayback(order);
    expect(view.isPlaying(), "the view plays");
    const Rgb565Frame* image = view.image(document, 0);
    expect(image && image->pixels[0] == frameColor(0), "the first frame of the sequence is shown");
    expect(preview.isOpen() && preview.sequenceHasFrame(RuntimeView::kSequenceFrames - 1) &&
               !preview.sequenceHasFrame(RuntimeView::kSequenceFrames),
           "one sequence of a bounded number of frames is loaded");
    const std::size_t afterFirst = view.renderCount();
    (void)view.image(document, 0);
    expect(view.renderCount() == afterFirst, "a repaint does not feed the frame again");

    image = view.image(document, 2);
    expect(image && image->pixels[9] == frameColor(2), "the rotating frame starts at its color");
    expect(view.frame(document, 2).nextRotationMs > 0, "libserum announces the rotation");
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    expect(view.rotate() > 0, "libserum announces the next rotation step");
    image = view.image(document, 2);
    expect(image && image->pixels[9] == 0x07e0, "libserum rotated the color");
    expect(view.frame(document, 2).identified, "the rotated frame is still the identified one");

    // Past the loaded frames the next ones are loaded.
    image = view.image(document, RuntimeView::kSequenceFrames);
    expect(image && image->pixels[0] == frameColor(RuntimeView::kSequenceFrames),
           "a frame past the loaded sequence is shown");
    expect(preview.sequenceHasFrame(RuntimeView::kSequenceFrames) && !preview.sequenceHasFrame(0),
           "the next frames were loaded in place of the first ones");

    // An edit while playing is shown: the sequence is loaded again.
    document.setColorizedPixel(RuntimeView::kSequenceFrames, Resolution::Sd, 0, 0, 0xffff);
    image = view.image(document, RuntimeView::kSequenceFrames);
    expect(image && image->pixels[0] == 0xffff, "an edit during playback is rendered");

    view.stopPlayback();
    expect(!view.isPlaying() && !preview.isOpen(), "stopping releases what libserum had loaded");
    view.startPlayback(order);
    (void)view.image(document, 1);
    expect(preview.isOpen(), "playing again loads a sequence");
    view.setEnabled(false);
    expect(!view.isPlaying() && !preview.isOpen(), "switching the view off ends playback");
}

void testCanvas(PlaneLayout layout)
{
    ProjectDocument document = makeDocument(layout, 2);
    document.setComparisonMaskId(0, 0);
    document.setComparisonMaskPixel(0, 40, 10, 1);
    const Size native = document.geometry().planeSize(document.geometry().nativePlane());

    RuntimeView view;
    FramePreviewWidget widget;
    widget.resize(900, 500);
    widget.setRuntimeView(&view);
    widget.setDocument(&document);
    widget.setActiveResolution(document.geometry().nativePlane());
    widget.setFrameIndex(0);
    widget.setComparisonMaskOverlayEnabled(true);

    int clicks = 0;
    widget.setPixelClickedHandler([&](CanvasHit, bool) { ++clicks; });
    widget.setOriginalClickedHandler([&](CanvasHit, bool) { ++clicks; });
    widget.setStrokeFinishedHandler([&](CanvasHit, CanvasHit, bool) { ++clicks; });

    const QPoint picturePixel = positionOf(widget, CanvasRegion::ColorizedFrame, 20, 6);
    const QPoint maskedPixel = positionOf(widget, CanvasRegion::OriginalFrame, 40, 10);
    const QPoint originalPixel = positionOf(widget, CanvasRegion::OriginalFrame, 60, 10);
    const QImage editing = renderWidget(widget);
    expect(editing.pixelColor(maskedPixel) != editing.pixelColor(originalPixel),
           "while editing the comparison mask overlay is drawn");
    expect(view.renderCount() == 0, "libserum is not asked while the view is off");
    click(widget, picturePixel);
    expect(clicks == 2, "while editing a click reaches the handlers");

    view.setEnabled(true);
    widget.update();
    expect(colorizedRegionSize(widget) == native, "the runtime picture has the native size");
    const QImage runtime = renderWidget(widget);
    const Rgb565Frame* image = view.image(document, 0);
    expect(image != nullptr, "libserum has a picture of the frame");
    expect(runtime.pixelColor(positionOf(widget, CanvasRegion::ColorizedFrame, 20, 6)) ==
               colorizing::app::rgb565ToColor(image->pixels[6 * static_cast<std::size_t>(
                                                                    native.width) + 20]),
           "the canvas shows libserum's pixels");
    expect(runtime.pixelColor(maskedPixel) == runtime.pixelColor(originalPixel),
           "the Runtime view hides the overlays");
    expect(runtime != editing, "the Runtime view is told apart from the editing canvas");
    clicks = 0;
    click(widget, positionOf(widget, CanvasRegion::ColorizedFrame, 20, 6));
    click(widget, maskedPixel);
    expect(clicks == 0, "the Runtime view takes no clicks");

    // An edit of the document shows at the next paint.
    const Resolution plane = document.geometry().nativePlane();
    document.setColorizedPixel(0, plane, 20, 6, 0xf81f);
    const QImage edited = renderWidget(widget);
    expect(edited.pixelColor(positionOf(widget, CanvasRegion::ColorizedFrame, 20, 6)) ==
               colorizing::app::rgb565ToColor(0xf81f),
           "the Runtime view follows an edit");

    if (layout == PlaneLayout::Ordinary) {
        view.setRows(PlaneRequest::Rows64);
        widget.update();
        expect(colorizedRegionSize(widget) == Size{256, 64},
               "the 64-row plane is laid out as 256x64, also for a frame without HD");
        const QImage rows64 = renderWidget(widget);
        expect(rows64.pixelColor(positionOf(widget, CanvasRegion::ColorizedFrame, 40, 12)) ==
                   colorizing::app::rgb565ToColor(0xf81f),
               "the upscaled plane shows the edited pixel doubled");
        view.setRows(PlaneRequest::Rows32);
    }

    view.setEnabled(false);
    widget.update();
    expect(colorizedRegionSize(widget) == native, "leaving the view restores the editing layout");
    clicks = 0;
    click(widget, positionOf(widget, CanvasRegion::ColorizedFrame, 20, 6));
    expect(clicks == 2, "after leaving the view clicks edit again");
    widget.setRuntimeView(nullptr);
}

}  // namespace

void runRuntimeViewWidgetTests()
{
    testState();
    testRendersOnlyWhenSomethingChanged();
    testPlayback();
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        testCanvas(layout);
    }
}
