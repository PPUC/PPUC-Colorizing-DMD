// Importing a picture into the selection (the Win32 editor's Images window): the adjustments,
// the zone and its scaling, the pixels written, and the window with a still picture.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>

#include "ImageImport.h"
#include "dialogs/ImageImportDialog.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: image import: " << message << '\n';
        std::exit(1);
    }
}

void testAdjustments()
{
    QImage picture(3, 1, QImage::Format_RGB32);
    picture.setPixel(0, 0, qRgb(100, 0, 255));
    picture.setPixel(1, 0, qRgb(200, 50, 0));
    picture.setPixel(2, 0, qRgb(0, 100, 10));
    expect(app::adjustedImage(picture, {}).pixel(0, 0) == picture.pixel(0, 0),
           "no adjustment changes nothing");
    // Brightness 10: + 25; contrast 4: times 1.2, as the Win32 editor (OpenCV) computes.
    const QImage brighter = app::adjustedImage(picture, app::ImageAdjustments{10, 0, 0});
    expect(brighter.pixel(0, 0) == qRgb(125, 25, 255), "brightness adds 2.5 per step, clamped");
    const QImage contrast = app::adjustedImage(picture, app::ImageAdjustments{0, 4, 0});
    expect(contrast.pixel(1, 0) == qRgb(240, 60, 0), "contrast multiplies by 1 + 0.05 per step");
    // Blur 3 of a row of 3: each pixel the mean of itself and its neighbours (edges reflected).
    const QImage blurred = app::adjustedImage(picture, app::ImageAdjustments{0, 0, 3});
    expect(qRed(blurred.pixel(1, 0)) == 100 && qRed(blurred.pixel(0, 0)) == 167,
           "a box blur, its edges reflected as OpenCV does");
}

void testZones()
{
    // A picture 200x100, a selection of 4:1: the widest zone is 200x50 in the middle.
    expect(app::fittingZone(QSize(200, 100), 4.0) == QRect(0, 25, 200, 50),
           "the widest zone of the proportions, in the middle");
    expect(app::fittingZone(QSize(100, 100), 0.5) == QRect(25, 0, 50, 100),
           "a tall zone in a square picture");
    expect(app::zoneAt(QSize(200, 100), 4.0, 40, QPoint(100, 50)) == QRect(80, 45, 40, 10),
           "a zone around a point");
    expect(app::zoneAt(QSize(200, 100), 4.0, 40, QPoint(0, 0)) == QRect(0, 0, 40, 10),
           "moved inside the picture at its edge");
    expect(app::zoneAt(QSize(200, 100), 4.0, 1000, QPoint(0, 0)).width() == 200 &&
               app::zoneAt(QSize(200, 100), 4.0, 1, QPoint(0, 0)).width() == 8,
           "no wider than the picture, no narrower than 8 pixels");
    const QRect start(0, 0, 40, 10);
    const QRect end(100, 50, 80, 20);
    expect(app::zoneBetween(start, end, 0, 3) == start &&
               app::zoneBetween(start, end, 1, 3) == QRect(50, 25, 60, 15) &&
               app::zoneBetween(start, end, 2, 3) == end,
           "scroll copy: from the start zone to the end zone over the frames");

    QImage picture(4, 2, QImage::Format_RGB32);
    for (int x = 0; x < 4; ++x) {
        picture.setPixel(x, 0, qRgb(x * 60, 0, 0));
        picture.setPixel(x, 1, qRgb(0, x * 60, 0));
    }
    const QImage doubled =
        app::zonePicture(picture, QRect(0, 0, 4, 2), QSize(8, 4), app::ImageScaleFilter::Nearest);
    expect(doubled.size() == QSize(8, 4) && doubled.pixel(3, 0) == picture.pixel(1, 0) &&
               doubled.pixel(3, 3) == picture.pixel(1, 1),
           "nearest takes the pixel each target pixel falls on");
    const QImage part =
        app::zonePicture(picture, QRect(2, 0, 2, 2), QSize(2, 2), app::ImageScaleFilter::Nearest);
    expect(part.pixel(0, 0) == picture.pixel(2, 0), "only the zone");
}

void testPixels()
{
    document::Rgb565Frame plane = document::makeRgb565Frame(render::Size{8, 4}, 0);
    QImage picture(4, 2, QImage::Format_RGB32);
    picture.fill(qRgb(255, 0, 0));
    std::vector<uint8_t> mask(32, 0);
    mask[1 * 8 + 2] = 1;
    mask[1 * 8 + 3] = 1;
    const std::vector<edit::PixelChange> all =
        app::importedPixelChanges(plane, picture, render::Rect{2, 1, 4, 2}, {});
    const std::vector<edit::PixelChange> masked =
        app::importedPixelChanges(plane, picture, render::Rect{2, 1, 4, 2}, mask);
    expect(all.size() == 8 && all.front().x == 2 && all.front().y == 1 &&
               all.front().newColor == 0xf800,
           "without a mask the whole rect gets the picture");
    expect(masked.size() == 2, "with one, only the selected pixels");

    // SD 8x4 to HD 16x8 and back.
    const app::PlaneArea sd{render::Rect{2, 1, 2, 1}, mask};
    const app::PlaneArea hd = app::areaOnPlane(sd, render::Size{8, 4}, render::Size{16, 8});
    expect(hd.rect.x == 4 && hd.rect.y == 2 && hd.rect.width == 4 && hd.rect.height == 2 &&
               hd.mask.size() == 128 && hd.mask[2 * 16 + 4] == 1 && hd.mask[3 * 16 + 7] == 1 &&
               hd.mask[2 * 16 + 8] == 0,
           "on the HD plane the rect and the selection are doubled");
    std::vector<uint8_t> hdMask(128, 0);
    hdMask[2 * 16 + 5] = 1;
    const app::PlaneArea back = app::areaOnPlane(app::PlaneArea{render::Rect{5, 2, 1, 1}, hdMask},
                                                 render::Size{16, 8}, render::Size{8, 4});
    expect(back.rect.x == 2 && back.rect.y == 1 && back.rect.width == 1 && back.mask[1 * 8 + 2] == 1,
           "on the SD plane a pixel is selected when one it covers is");
}

void sendMove(QWidget& widget, QPointF at)
{
    QMouseEvent move(QEvent::MouseMove, at, widget.mapToGlobal(at), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);
}

void sendClick(QWidget& widget, QPointF at, Qt::MouseButton button)
{
    QMouseEvent press(QEvent::MouseButtonPress, at, widget.mapToGlobal(at), button, button,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, widget.mapToGlobal(at), button,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

void testDialog()
{
    // A 400x200 picture, a selection of 32x8 (4:1) on the SD plane of a project with HD.
    QImage picture(400, 200, QImage::Format_RGB32);
    picture.fill(qRgb(0, 0, 255));
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; ++x) {
            picture.setPixel(x, y, qRgb(255, 0, 0));
        }
    }
    app::ImageImportSetup setup;
    setup.selectionSize = render::Size{32, 8};
    setup.firstPlaneName = QStringLiteral("128x32");
    setup.secondPlaneName = QStringLiteral("256x64");
    setup.backgroundName = QStringLiteral("3");
    setup.durationsMs = {50, 50, 50};
    app::ImageImportDialog dialog(picture, QStringLiteral("picture.png"), setup);
    dialog.resize(900, 760);
    dialog.show();
    QApplication::processEvents();
    expect(!dialog.isMovie() && dialog.zone() == QRect(0, 50, 400, 100) && dialog.toFirstPlane() &&
               !dialog.toSecondPlane() && !dialog.toBackground() && dialog.frameTimes().empty(),
           "a still picture: the widest zone of the selection's proportions, SD, the frames");
    expect(dialog.resultPicture().size() == QSize(32, 8), "the result has the selection's size");

    // Over the top left quarter: the zone follows the mouse; the wheel makes it smaller; a
    // click takes it.
    app::ImageZoneView& view = *dialog.view();
    const QPointF topLeft = view.toView(QRect(100, 50, 1, 1)).center();
    sendMove(view, topLeft);
    for (int i = 0; i < 30; ++i) {
        QWheelEvent wheel(topLeft, view.mapToGlobal(topLeft), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&view, &wheel);
    }
    sendClick(view, topLeft, Qt::LeftButton);
    const QRect zone = dialog.zone();
    expect(zone.width() < 400 && zone.width() == zone.height() * 4 &&
               std::abs(zone.center().x() - 100) <= 2 && std::abs(zone.center().y() - 50) <= 2,
           "the wheel (Shift: ten times) makes the zone smaller, a click takes it there");
    expect(qRed(dialog.resultPicture().pixel(16, 4)) == 255,
           "the result shows the red quarter under the zone");

    // A right click takes an end zone: the zone scrolls over the selected frames.
    const QPointF bottomRight = view.toView(QRect(300, 150, 1, 1)).center();
    sendMove(view, bottomRight);
    sendClick(view, bottomRight, Qt::RightButton);
    expect(dialog.endZone() && dialog.zoneFor(0, 3) == zone && dialog.zoneFor(2, 3) == *dialog.endZone() &&
               dialog.zoneFor(1, 3).center().x() > zone.center().x(),
           "a right click: the end zone of a scroll over the frames");
    dialog.setEndZone(std::nullopt);
    expect(dialog.zoneFor(2, 3) == zone, "without an end zone every frame gets the zone");

    dialog.setAdjustments(app::ImageAdjustments{-50, 0, 0});
    expect(qRed(dialog.adjustedPicture().pixel(0, 0)) == 130 && dialog.adjustments().brightness == -50,
           "the adjustments change the picture shown and the result");
    dialog.setToBackground(true);
    dialog.setPlanes(true, true);
    expect(dialog.toBackground() && dialog.toSecondPlane(), "the background and both planes");
}

}  // namespace

void runImageImportWidgetTests()
{
    testAdjustments();
    testZones();
    testPixels();
    testDialog();
}
