#include "FramePreviewWidget.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "SpriteEditorWidget.h"

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

colorizing::document::ProjectDocument makeDynamicReplacementDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.romName = "dynamic_replacement";
    colorizing::document::FrameDynamicData dynamicData{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        colorizing::document::makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    dynamicData.maskSd.pixels[0] = 2;
    dynamicData.maskHd.pixels[0] = 2;
    dynamicData.colorsSd[2 * 16] = 0xf800;
    dynamicData.colorsHd[2 * 16] = 0x07e0;
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

colorizing::document::ProjectDocument makeBackgroundPreviewDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.romName = "background_preview";
    colorizing::document::FrameBackgroundData backgroundData{
        0,
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeIndexedFrame(colorizing::render::kHdFrameSize, 0),
    };
    backgroundData.maskSd.pixels[0] = 1;
    backgroundData.maskHd.pixels[0] = 1;
    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        std::nullopt,
        backgroundData,
    });
    snapshot.backgrounds.push_back(colorizing::document::BackgroundRecord{
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0x07e0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0x07e0),
    });
    return colorizing::document::ProjectDocument::fromSnapshot(snapshot);
}

colorizing::document::ProjectDocument makeSpriteEditorDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.romName = "sprite_editor";
    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
    });
    colorizing::document::SpriteRecord sprite;
    sprite.originalSd = colorizing::document::makeIndexedFrame({256, 64}, 255);
    sprite.colorizedSd = colorizing::document::makeRgb565Frame({256, 64}, 0);
    sprite.maskHd = colorizing::document::makeIndexedFrame({256, 64}, 255);
    sprite.colorizedHd = colorizing::document::makeRgb565Frame({256, 64}, 0);
    sprite.detectionDwords.assign(colorizing::document::kMaxSpriteDetectAreas, 0xffffffff);
    sprite.detectionDwordPositions.assign(colorizing::document::kMaxSpriteDetectAreas, 0xffff);
    sprite.detectionAreas.assign(colorizing::document::kMaxSpriteDetectAreas,
                                 colorizing::document::SpriteDetectionArea{
                                     0xffff,
                                     0xffff,
                                     0xffff,
                                     0xffff,
                                 });
    sprite.detectionAreas[0] = colorizing::document::SpriteDetectionArea{4, 5, 20, 8};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 12; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y + 5) * 256 + static_cast<std::size_t>(x + 4);
            sprite.originalSd.pixels[index] = 1;
            sprite.colorizedSd.pixels[index] = 0x07e0;
        }
    }
    snapshot.sprites.push_back(sprite);
    return colorizing::document::ProjectDocument::fromSnapshot(snapshot);
}

colorizing::document::ProjectDocument makeDynamicOverBackgroundDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.romName = "dynamic_over_background";

    colorizing::document::IndexedFrame original =
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0);
    original.pixels[1] = 1;

    colorizing::document::FrameDynamicData dynamicData{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        colorizing::document::makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    dynamicData.maskSd.pixels[0] = 2;
    dynamicData.maskSd.pixels[1] = 2;
    dynamicData.maskHd.pixels[0] = 2;
    dynamicData.maskHd.pixels[2] = 2;
    dynamicData.colorsSd[2 * 16] = 0x0000;
    dynamicData.colorsSd[2 * 16 + 1] = 0xf800;
    dynamicData.colorsHd[2 * 16] = 0x0000;
    dynamicData.colorsHd[2 * 16 + 1] = 0xf800;

    colorizing::document::FrameBackgroundData backgroundData{
        0,
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeIndexedFrame(colorizing::render::kHdFrameSize, 0),
    };
    backgroundData.maskSd.pixels[0] = 1;
    backgroundData.maskHd.pixels[0] = 1;

    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        original,
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        dynamicData,
        backgroundData,
    });
    snapshot.backgrounds.push_back(colorizing::document::BackgroundRecord{
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0x07e0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0x07e0),
    });
    return colorizing::document::ProjectDocument::fromSnapshot(snapshot);
}

colorizing::document::ProjectDocument makeSpriteCompositionDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.romName = "sprite_composition";

    colorizing::document::IndexedFrame original =
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0);
    original.pixels[5 * colorizing::render::kSdFrameSize.width + 10] = 1;
    original.pixels[5 * colorizing::render::kSdFrameSize.width + 11] = 2;
    original.pixels[5 * colorizing::render::kSdFrameSize.width + 12] = 3;
    original.pixels[5 * colorizing::render::kSdFrameSize.width + 13] = 0;

    colorizing::document::SpriteRecord sprite;
    sprite.hasHd = true;
    sprite.originalSd = colorizing::document::makeIndexedFrame(
        {static_cast<int>(colorizing::document::kMaxSpriteWidth),
         static_cast<int>(colorizing::document::kMaxSpriteHeight)},
        255);
    sprite.colorizedSd = colorizing::document::makeRgb565Frame(
        {static_cast<int>(colorizing::document::kMaxSpriteWidth),
         static_cast<int>(colorizing::document::kMaxSpriteHeight)},
        0);
    sprite.maskHd = colorizing::document::makeIndexedFrame(
        {static_cast<int>(colorizing::document::kMaxSpriteWidth),
         static_cast<int>(colorizing::document::kMaxSpriteHeight)},
        255);
    sprite.colorizedHd = colorizing::document::makeRgb565Frame(
        {static_cast<int>(colorizing::document::kMaxSpriteWidth),
         static_cast<int>(colorizing::document::kMaxSpriteHeight)},
        0);
    for (int x = 0; x < 4; ++x) {
        sprite.originalSd.pixels[static_cast<std::size_t>(x)] =
            static_cast<uint8_t>(x == 3 ? 0 : x + 1);
        sprite.colorizedSd.pixels[static_cast<std::size_t>(x)] = 0xf800;
    }
    for (int x = 0; x < 8; ++x) {
        sprite.maskHd.pixels[static_cast<std::size_t>(x)] = 0;
        sprite.colorizedHd.pixels[static_cast<std::size_t>(x)] = 0x07e0;
    }
    sprite.detectionDwords = {0x00030201, 0xffffffff, 0xffffffff, 0xffffffff};
    sprite.detectionDwordPositions = {0, 0xffff, 0xffff, 0xffff};
    sprite.detectionAreas = {
        colorizing::document::SpriteDetectionArea{0, 0, 4, 1},
        colorizing::document::SpriteDetectionArea{},
        colorizing::document::SpriteDetectionArea{},
        colorizing::document::SpriteDetectionArea{},
    };
    snapshot.sprites.push_back(sprite);

    std::vector<colorizing::document::FrameSpriteReference> spriteReferences(
        colorizing::document::kMaxSpritesPerFrame);
    spriteReferences[0] = colorizing::document::FrameSpriteReference{0, 0, 0, 127, 31};
    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        original,
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        std::nullopt,
        std::nullopt,
        spriteReferences,
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

bool hasRedTintedPixel(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (color.red() > 80 && color.red() > color.green() &&
                color.red() > color.blue()) {
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

bool hasVisibleYellowPixel(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (color.red() > 80 && color.green() > 80 && color.blue() < 40) {
                return true;
            }
        }
    }
    return false;
}

bool hasVisibleGreenPixel(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (color.red() < 40 && color.green() > 80 && color.blue() < 40) {
                return true;
            }
        }
    }
    return false;
}

bool isMostlyBlack(const QColor& color)
{
    return color.red() < 20 && color.green() < 20 && color.blue() < 20;
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

void testFrameThumbnailComposesBackgroundBeforeDynamic()
{
    colorizing::document::ProjectDocument document = makeDynamicOverBackgroundDocument();
    const QIcon icon = colorizing::app::makeFrameThumbnail(document, 0, QSize(160, 84));
    const QPixmap pixmap = icon.pixmap(QSize(160, 84));
    expect(!pixmap.isNull(), "background thumbnail pixmap should not be null");

    const QImage image = pixmap.toImage();
    expect(hasVisibleGreenPixel(image),
           "thumbnail should render background pixels behind dynamic zones");
    expect(hasVisibleRedPixel(image),
           "thumbnail should still render non-background dynamic pixels");
}

void testFramePreviewHitTesting()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    widget.setFrameIndex(0);

    const std::optional<colorizing::render::CanvasHit> hit = widget.hitTestAt(QPoint(90, 90));
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
                      QPointF(90, 90),
                      QPointF(90, 90),
                      QPointF(90, 90),
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
                      QPointF(90, 90),
                      QPointF(90, 90),
                      QPointF(90, 90),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);

    QMouseEvent move(QEvent::MouseMove,
                     QPointF(120, 110),
                     QPointF(120, 110),
                     QPointF(120, 110),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(140, 120),
                        QPointF(140, 120),
                        QPointF(140, 120),
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

void testSelectedSpriteBoundsOverlayRenders()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    widget.setSelectedSpriteReference(colorizing::document::FrameSpriteReference{
        0,
        10,
        5,
        30,
        12,
    });
    widget.setFrameIndex(0);

    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    expect(hasVisibleCyanPixel(image),
           "selected sprite slot bounds should render over original frame");

    widget.setSelectedSpriteBoundsOverlayEnabled(false);
    QImage hiddenImage(widget.size(), QImage::Format_ARGB32);
    hiddenImage.fill(Qt::transparent);
    QPainter hiddenPainter(&hiddenImage);
    widget.render(&hiddenPainter);
    hiddenPainter.end();
    expect(!hasVisibleCyanPixel(hiddenImage),
           "selected sprite slot bounds overlay should be hideable");
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

void testDynamicReplacementRendersInSdAndHd()
{
    colorizing::document::ProjectDocument document = makeDynamicReplacementDocument();

    const QImage sdImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Sd,
        2,
        false);
    expect(hasVisibleRedPixel(sdImage), "SD dynamic replacement should use dynamic color set");

    const QImage hdImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Hd,
        2,
        false);
    expect(hasVisibleGreenPixel(hdImage), "HD dynamic replacement should use dynamic color set");
}

void testDmdRenderOptionsLeavePixelSpacing()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();

    colorizing::app::DmdRenderOptions squareOptions;
    squareOptions.pixelShape = colorizing::app::DmdPixelShape::Square;
    squareOptions.pixelCoverage = 0.5;
    const QImage squareImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Sd,
        8,
        false,
        squareOptions);
    expect(isMostlyBlack(QColor(squareImage.pixel(0, 0))),
           "reduced square DMD pixel should leave black spacing");
    expect(QColor(squareImage.pixel(3, 3)).red() > 80,
           "reduced square DMD pixel should keep a colored center");

    colorizing::app::DmdRenderOptions dotOptions;
    dotOptions.pixelShape = colorizing::app::DmdPixelShape::Dot;
    dotOptions.pixelCoverage = 0.5;
    const QImage dotImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Sd,
        8,
        false,
        dotOptions);
    expect(isMostlyBlack(QColor(dotImage.pixel(0, 0))),
           "dot DMD pixel should leave dark cell corners");
    expect(QColor(dotImage.pixel(4, 4)).red() > 80,
           "dot DMD pixel should keep a colored center");

    dotOptions.pixelCoverage = 1.0;
    const QImage fullDotImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Sd,
        8,
        false,
        dotOptions);
    expect(isMostlyBlack(QColor(fullDotImage.pixel(0, 0))),
           "full-size dot DMD pixel should not overlap into the cell corner");
    expect(QColor(fullDotImage.pixel(4, 4)).red() > 80,
           "full-size dot DMD pixel should fill the cell center");
}

void testBackgroundMaskOverlayRendersInSdAndHd()
{
    colorizing::document::ProjectDocument document = makeBackgroundPreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setBackgroundMaskOverlayEnabled(true);
    widget.setFrameIndex(0);

    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    QImage sdImage(widget.size(), QImage::Format_ARGB32);
    sdImage.fill(Qt::transparent);
    QPainter sdPainter(&sdImage);
    widget.render(&sdPainter);
    sdPainter.end();
    expect(hasVisibleYellowPixel(sdImage), "SD background mask overlay should render visibly");

    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    QImage hdImage(widget.size(), QImage::Format_ARGB32);
    hdImage.fill(Qt::transparent);
    QPainter hdPainter(&hdImage);
    widget.render(&hdPainter);
    hdPainter.end();
    expect(hasVisibleYellowPixel(hdImage), "HD background mask overlay should render visibly");
}

void testBackgroundLayerRendersInSdAndHd()
{
    colorizing::document::ProjectDocument document = makeBackgroundPreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setBackgroundLayerEnabled(true);
    widget.setFrameIndex(0);

    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    QImage sdImage(widget.size(), QImage::Format_ARGB32);
    sdImage.fill(Qt::transparent);
    QPainter sdPainter(&sdImage);
    widget.render(&sdPainter);
    sdPainter.end();
    expect(hasVisibleGreenPixel(sdImage), "SD background layer should render visibly");

    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    QImage hdImage(widget.size(), QImage::Format_ARGB32);
    hdImage.fill(Qt::transparent);
    QPainter hdPainter(&hdImage);
    widget.render(&hdPainter);
    hdPainter.end();
    expect(hasVisibleGreenPixel(hdImage), "HD background layer should render visibly");
}

void testDynamicDoesNotReplaceAppliedBackground()
{
    colorizing::document::ProjectDocument document = makeDynamicOverBackgroundDocument();

    const QImage sdImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Sd,
        2,
        true);
    expect(QColor(sdImage.pixel(0, 0)).green() > 80,
           "SD applied background should not be replaced by dynamic black");
    expect(hasVisibleRedPixel(sdImage),
           "SD non-background dynamic pixels should still use dynamic color set");

    const QImage hdImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Hd,
        2,
        true);
    expect(QColor(hdImage.pixel(0, 0)).green() > 80,
           "HD applied background should not be replaced by dynamic black");
    expect(hasVisibleRedPixel(hdImage),
           "HD non-background dynamic pixels should still use dynamic color set");
}

void testSpriteCompositionRendersInSdAndHd()
{
    colorizing::document::ProjectDocument document = makeSpriteCompositionDocument();
    expect(document.spriteCount() == 1, "sprite composition fixture should have one sprite");

    const colorizing::document::Rgb565Frame sdComposed =
        colorizing::app::composeResolvedFrame(document,
                                              0,
                                              colorizing::render::Resolution::Sd,
                                              false);
    expect(sdComposed.pixels[5 * colorizing::render::kSdFrameSize.width + 10] == 0xf800,
           "SD composed frame should contain matched sprite pixel");

    const QImage sdImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Sd,
        2,
        false);
    expect(hasVisibleRedPixel(sdImage), "SD composed preview should render matched sprites");

    const QImage hdImage = colorizing::app::renderResolvedFrameImage(
        document,
        0,
        colorizing::render::Resolution::Hd,
        2,
        false);
    expect(hasVisibleGreenPixel(hdImage), "HD composed preview should render matched sprites");
}

void testBackgroundImageEditModeRendersInSdAndHd()
{
    colorizing::document::ProjectDocument document = makeBackgroundPreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setBackgroundImageEditModeEnabled(true);
    widget.setFrameIndex(0);

    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    QImage sdImage(widget.size(), QImage::Format_ARGB32);
    sdImage.fill(Qt::transparent);
    QPainter sdPainter(&sdImage);
    widget.render(&sdPainter);
    sdPainter.end();
    expect(hasVisibleGreenPixel(sdImage), "SD background edit image should render visibly");

    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    QImage hdImage(widget.size(), QImage::Format_ARGB32);
    hdImage.fill(Qt::transparent);
    QPainter hdPainter(&hdImage);
    widget.render(&hdPainter);
    hdPainter.end();
    expect(hasVisibleGreenPixel(hdImage), "HD background edit image should render visibly");
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

void testSpriteEditorRendersAndEditsDetectionArea()
{
    colorizing::document::ProjectDocument document = makeSpriteEditorDocument();
    colorizing::app::SpriteEditorWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setSpriteIndex(std::size_t{0});
    widget.setDetectionAreasVisible(true);
    widget.setSelectedDetectionAreaIndex(std::size_t{0});

    struct PaintedSpritePixel {
        std::size_t spriteIndex = 0;
        colorizing::render::Resolution resolution = colorizing::render::Resolution::Sd;
        int x = 0;
        int y = 0;
        bool erase = false;
    };

    std::optional<PaintedSpritePixel> paintedPixel;
    bool paintFinished = false;
    widget.setColorizedPixelEditedHandler(
        [&](std::size_t spriteIndex,
            colorizing::render::Resolution resolution,
            int x,
            int y,
            bool erase) {
            paintedPixel = PaintedSpritePixel{spriteIndex, resolution, x, y, erase};
        });
    widget.setColorizedPaintFinishedHandler([&]() { paintFinished = true; });

    QMouseEvent paintPress(QEvent::MouseButtonPress,
                           QPointF(84, 80),
                           QPointF(84, 80),
                           QPointF(84, 80),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier,
                           QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &paintPress);
    QMouseEvent paintRelease(QEvent::MouseButtonRelease,
                             QPointF(84, 80),
                             QPointF(84, 80),
                             QPointF(84, 80),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier,
                             QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &paintRelease);

    expect(paintedPixel.has_value(), "sprite editor should emit colorized sprite pixel edits");
    expect(paintedPixel->spriteIndex == 0 &&
               paintedPixel->resolution == colorizing::render::Resolution::Sd &&
               paintedPixel->x == 10 && paintedPixel->y == 5 && !paintedPixel->erase,
           "sprite editor should convert colorized sprite canvas coordinates");
    expect(paintFinished, "sprite editor should emit colorized paint finish");

    paintedPixel.reset();
    QMouseEvent erasePress(QEvent::MouseButtonPress,
                           QPointF(86, 82),
                           QPointF(86, 82),
                           QPointF(86, 82),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::ShiftModifier,
                           QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &erasePress);
    QMouseEvent eraseRelease(QEvent::MouseButtonRelease,
                             QPointF(86, 82),
                             QPointF(86, 82),
                             QPointF(86, 82),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::ShiftModifier,
                             QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &eraseRelease);

    expect(paintedPixel.has_value() && paintedPixel->erase,
           "sprite editor should send Shift colorized sprite edits as erase");

    std::optional<std::vector<colorizing::render::Point>> paintedStroke;
    widget.setDrawingTool(colorizing::app::SpriteDrawingTool::Line);
    widget.setActiveDrawColor(0xf800);
    widget.setColorizedPixelsEditedHandler(
        [&](std::size_t spriteIndex,
            colorizing::render::Resolution resolution,
            const std::vector<colorizing::render::Point>& pixels,
            bool erase) {
            expect(spriteIndex == 0, "sprite editor stroke should edit selected sprite");
            expect(resolution == colorizing::render::Resolution::Sd,
                   "sprite editor stroke should use active sprite resolution");
            expect(!erase, "sprite editor stroke should use paint mode without Shift");
            paintedStroke = pixels;
        });

    QMouseEvent linePress(QEvent::MouseButtonPress,
                          QPointF(84, 80),
                          QPointF(84, 80),
                          QPointF(84, 80),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier,
                          QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &linePress);
    QMouseEvent lineMove(QEvent::MouseMove,
                         QPointF(104, 80),
                         QPointF(104, 80),
                         QPointF(104, 80),
                         Qt::NoButton,
                         Qt::LeftButton,
                         Qt::NoModifier,
                         QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &lineMove);

    QImage linePreview(widget.size(), QImage::Format_ARGB32);
    linePreview.fill(Qt::transparent);
    QPainter linePainter(&linePreview);
    widget.render(&linePainter);
    linePainter.end();
    expect(hasRedTintedPixel(linePreview), "sprite editor should render line preview");

    QMouseEvent lineRelease(QEvent::MouseButtonRelease,
                            QPointF(104, 80),
                            QPointF(104, 80),
                            QPointF(104, 80),
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier,
                            QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &lineRelease);

    expect(paintedStroke.has_value(), "sprite editor line should emit stroke pixels");
    expect(paintedStroke->size() == 11 &&
               paintedStroke->front().x == 10 && paintedStroke->front().y == 5 &&
               paintedStroke->back().x == 20 && paintedStroke->back().y == 5,
           "sprite editor line should convert stroke coordinates to pixels");

    std::optional<colorizing::render::Point> pickedSpritePixel;
    widget.setDrawingTool(colorizing::app::SpriteDrawingTool::ColorPicker);
    widget.setColorizedPixelPickedHandler(
        [&](std::size_t spriteIndex, colorizing::render::Resolution resolution, int x, int y) {
            expect(spriteIndex == 0, "sprite editor pick should target selected sprite");
            expect(resolution == colorizing::render::Resolution::Sd,
                   "sprite editor pick should use active sprite resolution");
            pickedSpritePixel = colorizing::render::Point{x, y};
        });
    QMouseEvent pickPress(QEvent::MouseButtonPress,
                          QPointF(84, 80),
                          QPointF(84, 80),
                          QPointF(84, 80),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier,
                          QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &pickPress);
    expect(pickedSpritePixel.has_value() && pickedSpritePixel->x == 10 &&
               pickedSpritePixel->y == 5,
           "sprite editor should emit color pick coordinates");

    std::optional<colorizing::render::Point> floodSpritePixel;
    widget.setDrawingTool(colorizing::app::SpriteDrawingTool::FloodFill);
    widget.setColorizedFloodFillHandler(
        [&](std::size_t spriteIndex, colorizing::render::Resolution resolution, int x, int y) {
            expect(spriteIndex == 0, "sprite editor flood fill should target selected sprite");
            expect(resolution == colorizing::render::Resolution::Sd,
                   "sprite editor flood fill should use active sprite resolution");
            floodSpritePixel = colorizing::render::Point{x, y};
        });
    QMouseEvent floodPress(QEvent::MouseButtonPress,
                           QPointF(84, 80),
                           QPointF(84, 80),
                           QPointF(84, 80),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier,
                           QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &floodPress);
    expect(floodSpritePixel.has_value() && floodSpritePixel->x == 10 &&
               floodSpritePixel->y == 5,
           "sprite editor should emit flood fill coordinates");

    widget.setDrawingTool(colorizing::app::SpriteDrawingTool::Point);

    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();
    expect(hasVisibleCyanPixel(image), "sprite editor should render detection area overlay");

    std::optional<colorizing::document::SpriteDetectionArea> editedArea;
    widget.setDetectionAreaChangedHandler(
        [&](std::size_t areaIndex, colorizing::document::SpriteDetectionArea area) {
            expect(areaIndex == 0, "sprite editor should edit selected area");
            editedArea = area;
        });

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(84, 232),
                      QPointF(84, 232),
                      QPointF(84, 232),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &press);
    QMouseEvent move(QEvent::MouseMove,
                     QPointF(104, 252),
                     QPointF(104, 252),
                     QPointF(104, 252),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier,
                     QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &move);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(104, 252),
                        QPointF(104, 252),
                        QPointF(104, 252),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier,
                        QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &release);

    expect(editedArea.has_value(), "sprite editor drag should emit edited area");
    expect(editedArea->x == 10 && editedArea->y == 5 &&
               editedArea->width == 11 && editedArea->height == 11,
           "sprite editor drag should convert canvas coordinates to area");
}

}  // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    testFramePreviewWidgetRendersPixels();
    testFrameThumbnailRendersPixels();
    testFrameThumbnailComposesBackgroundBeforeDynamic();
    testFramePreviewHitTesting();
    testFramePreviewClickCallback();
    testFramePreviewStrokeCallback();
    testHdPreviewPixelsRender();
    testOriginalFrameStrokeCallback();
    testComparisonMaskOverlayRenders();
    testSelectedSpriteBoundsOverlayRenders();
    testDynamicMaskOverlayRendersInSdAndHd();
    testDynamicReplacementRendersInSdAndHd();
    testDmdRenderOptionsLeavePixelSpacing();
    testBackgroundMaskOverlayRendersInSdAndHd();
    testBackgroundLayerRendersInSdAndHd();
    testDynamicDoesNotReplaceAppliedBackground();
    testSpriteCompositionRendersInSdAndHd();
    testBackgroundImageEditModeRendersInSdAndHd();
    testFramePreviewDoesNotShowHdReferenceBelowOriginal();
    testSpriteEditorRendersAndEditsDetectionArea();
    std::cout << "colorizing_qt_v2_widget_tests passed\n";
    return 0;
}
