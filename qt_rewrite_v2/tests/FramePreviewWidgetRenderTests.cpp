#include "FramePreviewWidget.h"

#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QImage>
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

}  // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    testFramePreviewWidgetRendersPixels();
    testFrameThumbnailRendersPixels();
    std::cout << "colorizing_qt_v2_widget_tests passed\n";
    return 0;
}
