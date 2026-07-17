#include "FramePreviewWidget.h"

#include <cstdlib>
#include <iostream>
#include <optional>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include "document/ProjectDocument.h"
#include "FrameThumbnail.h"

namespace {

void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

colorizing::document::ProjectDocument makePreviewDocument()
{
    colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::createEmpty({
            "preview",
            1,
            true,
        });
    document.setColorizedPixel(0, colorizing::render::Resolution::Sd, 0, 0, 0xf800);
    document.setColorizedPixel(0, colorizing::render::Resolution::Sd, 127, 31, 0x07e0);
    document.setColorizedPixel(0, colorizing::render::Resolution::Hd, 0, 0, 0x001f);
    document.markClean();
    return document;
}

bool hasNonBackgroundPixel(const QImage& image)
{
    const QRgb first = image.pixel(0, 0);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != first) {
                return true;
            }
        }
    }
    return false;
}

void testFramePreviewWidgetRendersPixels()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    widget.setFrameIndex(0);

    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    expect(hasNonBackgroundPixel(image), "preview widget should render non-background pixels");
}

void testFrameThumbnailRendersPixels()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    const QIcon icon = colorizing::app::makeFrameThumbnail(document, 0, QSize(160, 84));
    const QPixmap pixmap = icon.pixmap(QSize(160, 84));
    expect(!pixmap.isNull(), "thumbnail pixmap should not be null");
    expect(hasNonBackgroundPixel(pixmap.toImage()), "thumbnail should render non-background pixels");
}

void testFramePreviewHitTesting()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    widget.setFrameIndex(0);

    const std::optional<colorizing::render::CanvasHit> hit = widget.hitTestAt(QPoint(90, 60));
    expect(hit.has_value(), "preview hit test should hit active frame");
    expect(hit->region == colorizing::render::CanvasRegion::ColorizedFrame,
           "preview hit test should target colorized frame");
    expect(hit->resolution == colorizing::render::Resolution::Hd,
           "preview hit test should preserve HD resolution");
    expect(hit->x >= 0 && hit->x < 256 && hit->y >= 0 && hit->y < 64,
           "preview hit test coordinates should be inside HD frame");
}

void testFramePreviewClickCallback()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    widget.setFrameIndex(0);

    bool clicked = false;
    widget.setPixelClickedHandler([&](colorizing::render::CanvasHit hit) {
        clicked = hit.region == colorizing::render::CanvasRegion::ColorizedFrame;
    });

    QMouseEvent event(QEvent::MouseButtonPress,
                      QPointF(90, 60),
                      QPointF(90, 60),
                      QPointF(90, 60),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
    expect(clicked, "preview click should invoke pixel callback on colorized frame");
}

void testFramePreviewStrokeCallback()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    widget.setFrameIndex(0);

    std::optional<colorizing::render::CanvasHit> strokeStart;
    std::optional<colorizing::render::CanvasHit> strokeEnd;
    std::optional<colorizing::render::CanvasHit> previewStart;
    std::optional<colorizing::render::CanvasHit> previewEnd;
    widget.setStrokePreviewHandler([&](colorizing::render::CanvasHit start,
                                       colorizing::render::CanvasHit end) {
        previewStart = start;
        previewEnd = end;
    });
    widget.setStrokeFinishedHandler([&](colorizing::render::CanvasHit start,
                                        colorizing::render::CanvasHit end) {
        strokeStart = start;
        strokeEnd = end;
    });

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(90, 60),
                      QPointF(90, 60),
                      QPointF(90, 60),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);

    QMouseEvent move(QEvent::MouseMove,
                     QPointF(120, 80),
                     QPointF(120, 80),
                     QPointF(120, 80),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(140, 90),
                        QPointF(140, 90),
                        QPointF(140, 90),
                        Qt::LeftButton,
                        Qt::LeftButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);

    expect(previewStart.has_value(), "preview stroke should capture start hit");
    expect(previewEnd.has_value(), "preview stroke should capture current drag hit");
    expect(strokeStart.has_value(), "preview stroke should capture start hit");
    expect(strokeEnd.has_value(), "preview stroke should capture end hit");
    expect(previewStart->region == colorizing::render::CanvasRegion::ColorizedFrame,
           "preview stroke start should target colorized frame");
    expect(previewEnd->region == colorizing::render::CanvasRegion::ColorizedFrame,
           "preview stroke end should target colorized frame");
    expect(strokeStart->region == colorizing::render::CanvasRegion::ColorizedFrame,
           "preview stroke start should target colorized frame");
    expect(strokeEnd->region == colorizing::render::CanvasRegion::ColorizedFrame,
           "preview stroke end should target colorized frame");
    expect(strokeStart->resolution == strokeEnd->resolution,
           "preview stroke should stay within one resolution");
}

void testFramePreviewDoesNotShowHdReferenceBelowOriginal()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    widget.setFrameIndex(0);

    expect(!widget.hitTestAt(QPoint(90, 395)),
           "SD preview should not expose an extra HD region below original");

    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    expect(!widget.hitTestAt(QPoint(90, 395)),
           "HD preview should not expose a duplicate HD region below original");
}

}  // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    testFramePreviewWidgetRendersPixels();
    testFrameThumbnailRendersPixels();
    testFramePreviewHitTesting();
    testFramePreviewClickCallback();
    testFramePreviewStrokeCallback();
    testFramePreviewDoesNotShowHdReferenceBelowOriginal();
    std::cout << "colorizing_qt_v2_widget_tests passed\n";
    return 0;
}
