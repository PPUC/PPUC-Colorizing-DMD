#include "FramePreviewWidget.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
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

colorizing::document::ProjectDocument makeMaskedPreviewDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.romName = "masked_preview";
    colorizing::document::IndexedFrame mask =
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0);
    mask.pixels[0] = 1;
    snapshot.comparisonMasks.push_back(mask);
    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        0,
    });
    return colorizing::document::ProjectDocument::fromSnapshot(snapshot);
}

colorizing::document::ProjectDocument makeDynamicPreviewDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.romName = "dynamic_preview";
    colorizing::document::FrameDynamicData dynamicData{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        colorizing::document::makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    dynamicData.maskSd.pixels[0] = 0;
    dynamicData.maskHd.pixels[0] = 0;
    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        dynamicData,
    });
    return colorizing::document::ProjectDocument::fromSnapshot(snapshot);
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

bool hasVisibleRedPixel(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (color.red() > 80 && color.green() < 40 && color.blue() < 40) {
                return true;
            }
        }
    }
    return false;
}

bool hasVisibleCyanPixel(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (color.red() < 40 && color.green() > 80 && color.blue() > 100) {
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
    widget.setPixelClickedHandler([&](colorizing::render::CanvasHit hit, bool) {
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
    bool previewErase = true;
    bool strokeErase = true;
    widget.setStrokePreviewHandler([&](colorizing::render::CanvasHit start,
                                       colorizing::render::CanvasHit end,
                                       bool erase) {
        previewStart = start;
        previewEnd = end;
        previewErase = erase;
    });
    widget.setStrokeFinishedHandler([&](colorizing::render::CanvasHit start,
                                        colorizing::render::CanvasHit end,
                                        bool erase) {
        strokeStart = start;
        strokeEnd = end;
        strokeErase = erase;
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
    expect(!previewErase, "colorized preview stroke should not erase");
    expect(!strokeErase, "colorized finished stroke should not erase");
}

void testHdPreviewPixelsRender()
{
    colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::createEmpty({
            "preview",
            1,
            true,
        });
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    widget.setFrameIndex(0);
    widget.setPreviewPixels(colorizing::render::CanvasRegion::ColorizedFrame,
                            colorizing::render::Resolution::Hd,
                            {colorizing::render::Point{12, 8}},
                            QColor(255, 0, 0, 150));

    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    expect(hasVisibleRedPixel(image), "HD preview pixel should render visibly");
}

void testOriginalFrameStrokeCallback()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    widget.setFrameIndex(0);

    const std::optional<colorizing::render::CanvasHit> originalHit =
        widget.hitTestAt(QPoint(90, 240));
    expect(originalHit.has_value(), "test coordinate should hit original frame");
    expect(originalHit->region == colorizing::render::CanvasRegion::OriginalFrame,
           "test coordinate should target original frame");

    std::optional<colorizing::render::CanvasHit> strokeStart;
    std::optional<colorizing::render::CanvasHit> strokeEnd;
    bool clickedErase = false;
    bool strokeErase = false;
    widget.setOriginalClickedHandler([&](colorizing::render::CanvasHit, bool erase) {
        clickedErase = erase;
    });
    widget.setStrokeFinishedHandler([&](colorizing::render::CanvasHit start,
                                        colorizing::render::CanvasHit end,
                                        bool erase) {
        strokeStart = start;
        strokeEnd = end;
        strokeErase = erase;
    });

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(90, 240),
                      QPointF(90, 240),
                      QPointF(90, 240),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::ShiftModifier);
    QApplication::sendEvent(&widget, &press);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(120, 250),
                        QPointF(120, 250),
                        QPointF(120, 250),
                        Qt::LeftButton,
                        Qt::LeftButton,
                        Qt::ShiftModifier);
    QApplication::sendEvent(&widget, &release);

    expect(strokeStart.has_value(), "original stroke should capture start hit");
    expect(strokeEnd.has_value(), "original stroke should capture end hit");
    expect(strokeStart->region == colorizing::render::CanvasRegion::OriginalFrame,
           "original stroke start should target original frame");
    expect(strokeEnd->region == colorizing::render::CanvasRegion::OriginalFrame,
           "original stroke end should target original frame");
    expect(clickedErase, "original click should carry shift erase flag");
    expect(strokeErase, "original stroke should carry shift erase flag");
}

void testComparisonMaskOverlayRenders()
{
    colorizing::document::ProjectDocument document = makeMaskedPreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    widget.setComparisonMaskOverlayEnabled(true);
    widget.setFrameIndex(0);

    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    expect(hasVisibleRedPixel(image), "comparison mask overlay should render visibly");
}

void testDynamicMaskOverlayRendersInSdAndHd()
{
    colorizing::document::ProjectDocument document = makeDynamicPreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setDynamicMaskOverlayEnabled(true);
    widget.setFrameIndex(0);

    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    QImage sdImage(widget.size(), QImage::Format_ARGB32);
    sdImage.fill(Qt::transparent);
    QPainter sdPainter(&sdImage);
    widget.render(&sdPainter);
    sdPainter.end();
    expect(hasVisibleCyanPixel(sdImage), "SD dynamic mask overlay should render visibly");

    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    QImage hdImage(widget.size(), QImage::Format_ARGB32);
    hdImage.fill(Qt::transparent);
    QPainter hdPainter(&hdImage);
    widget.render(&hdPainter);
    hdPainter.end();
    expect(hasVisibleCyanPixel(hdImage), "HD dynamic mask overlay should render visibly");
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
    testHdPreviewPixelsRender();
    testOriginalFrameStrokeCallback();
    testComparisonMaskOverlayRenders();
    testDynamicMaskOverlayRendersInSdAndHd();
    testFramePreviewDoesNotShowHdReferenceBelowOriginal();
    std::cout << "colorizing_qt_v2_widget_tests passed\n";
    return 0;
}
