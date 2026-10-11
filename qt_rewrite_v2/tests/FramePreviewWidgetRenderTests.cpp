#include "FramePreviewWidget.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QSpinBox>
#include <QToolButton>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "panels/ColorRotationPanel.h"
#include "panels/PalettePanel.h"
#include "panels/SpritePanel.h"
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
    snapshot.colorCount = 16;
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
    snapshot.colorCount = 16;
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
    snapshot.colorCount = 16;
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
    snapshot.colorCount = 16;
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
    snapshot.colorCount = 16;
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
    snapshot.colorCount = 16;
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

colorizing::document::ProjectDocument makeDynamicShadowDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "dynamic_shadow";

    colorizing::document::IndexedFrame original =
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0);
    original.pixels[5 * 128 + 5] = 1;
    original.pixels[5 * 128 + 6] = 1;

    colorizing::document::FrameDynamicData dynamicData{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        colorizing::document::makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    dynamicData.maskSd.pixels[5 * 128 + 5] = 2;
    dynamicData.maskSd.pixels[5 * 128 + 6] = 2;
    dynamicData.maskSd.pixels[6 * 128 + 5] = 2;
    dynamicData.colorsSd[2 * 16 + 1] = 0xf800;
    dynamicData.shadowDirectionsSd.assign(colorizing::document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsSd.assign(colorizing::document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowDirectionsSd[2] = 0x08;
    dynamicData.shadowColorsSd[2] = 0x07e0;

    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        original,
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        std::nullopt,
        0,
        255,
        dynamicData,
    });
    return colorizing::document::ProjectDocument::fromSnapshot(snapshot);
}

colorizing::document::ProjectDocument makeSpriteCompositionDocument()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
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

    widget.setSelectedSpriteBoundsOverlayEnabled(true);
    widget.setSelectedSpriteReference(std::nullopt);
    QImage noSelectionImage(widget.size(), QImage::Format_ARGB32);
    noSelectionImage.fill(Qt::transparent);
    QPainter noSelectionPainter(&noSelectionImage);
    widget.render(&noSelectionPainter);
    noSelectionPainter.end();

    widget.setSelectedSpriteReference(colorizing::document::FrameSpriteReference{
        255,
        0,
        0,
        127,
        31,
    });
    QImage emptySlotImage(widget.size(), QImage::Format_ARGB32);
    emptySlotImage.fill(Qt::transparent);
    QPainter emptySlotPainter(&emptySlotImage);
    widget.render(&emptySlotPainter);
    emptySlotPainter.end();
    expect(hasVisibleCyanPixel(emptySlotImage),
           "empty sprite slots should render a bounds overlay for capture");
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

void testDynamicShadowsRender()
{
    const colorizing::document::ProjectDocument document = makeDynamicShadowDocument();
    const colorizing::document::Rgb565Frame frame =
        colorizing::app::composeResolvedFrame(document,
                                              0,
                                              colorizing::render::Resolution::Sd,
                                              false);
    expect(frame.pixels[5 * 128 + 5] == 0xf800,
           "dynamic source pixel should render dynamic color");
    expect(frame.pixels[5 * 128 + 6] == 0xf800,
           "dynamic neighbor should not be overwritten by shadow");
    expect(frame.pixels[5 * 128 + 7] == 0x07e0,
           "dynamic shadow should render into non-dynamic neighbor");
    expect(frame.pixels[6 * 128 + 6] == 0,
           "dynamic pixels over black original should not cast shadows");
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
    widget.setFrameIndex(0);
    widget.setSelectedBackgroundIndex(0);
    widget.setBackgroundImageEditModeEnabled(true);

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

void testSpriteEditorHdKeepsSdFootprint()
{
    colorizing::document::ProjectDocument document = makeSpriteEditorDocument();
    colorizing::document::ProjectSnapshot snapshot = document.toSnapshot();
    snapshot.sprites[0].hasHd = true;
    snapshot.sprites[0].maskHd =
        colorizing::document::makeIndexedFrame({256, 64}, 255);
    snapshot.sprites[0].colorizedHd =
        colorizing::document::makeRgb565Frame({256, 64}, 0);
    snapshot.sprites[0].maskHd.pixels[10 * 256 + 20] = 1;
    snapshot.sprites[0].colorizedHd.pixels[10 * 256 + 20] = 0xf800;
    document = colorizing::document::ProjectDocument::fromSnapshot(snapshot);

    colorizing::app::SpriteEditorWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setSpriteIndex(std::size_t{0});
    widget.setActiveResolution(colorizing::render::Resolution::Hd);

    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    const QColor sdFootprintPixel(image.pixel(84, 80));
    expect(sdFootprintPixel.red() > 80 && sdFootprintPixel.green() < 40,
           "HD sprite pixel should render in SD footprint position");

    std::optional<colorizing::render::Point> pickedPixel;
    widget.setDrawingTool(colorizing::app::SpriteDrawingTool::ColorPicker);
    widget.setColorizedPixelPickedHandler(
        [&](std::size_t, colorizing::render::Resolution resolution, int x, int y) {
            expect(resolution == colorizing::render::Resolution::Hd,
                   "HD sprite hit should use HD resolution");
            pickedPixel = colorizing::render::Point{x, y};
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
    expect(pickedPixel.has_value() && pickedPixel->x == 20 && pickedPixel->y == 10,
           "HD sprite hit should map SD footprint coordinate to HD source pixel");
}

void testColorRotationPreviewTransform()
{
    colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::createEmpty({
            "rotation_preview",
            1,
            false,
        });
    document.setColorizedPixel(0, colorizing::render::Resolution::Sd, 0, 0, 0x1111);
    document.setColorizedPixel(0, colorizing::render::Resolution::Sd, 1, 0, 0x2222);
    document.setColorizedPixel(0, colorizing::render::Resolution::Sd, 2, 0, 0x3333);
    document.setColorRotationLength(0, colorizing::render::Resolution::Sd, 0, 3);
    document.setColorRotationDelayMs(0, colorizing::render::Resolution::Sd, 0, 100);
    document.setColorRotationColor(0, colorizing::render::Resolution::Sd, 0, 0, 0x1111);
    document.setColorRotationColor(0, colorizing::render::Resolution::Sd, 0, 1, 0x2222);
    document.setColorRotationColor(0, colorizing::render::Resolution::Sd, 0, 2, 0x3333);

    colorizing::document::Rgb565Frame frame =
        colorizing::app::composeResolvedFrame(document,
                                              0,
                                              colorizing::render::Resolution::Sd,
                                              false);
    colorizing::app::applyColorRotations(document,
                                         0,
                                         colorizing::render::Resolution::Sd,
                                         100,
                                         frame);

    expect(frame.pixels[0] == 0x2222,
           "rotation preview should advance first color by one step");
    expect(frame.pixels[1] == 0x3333,
           "rotation preview should advance second color by one step");
    expect(frame.pixels[2] == 0x1111,
           "rotation preview should wrap final color to first color");
}

template <typename Widget>
Widget* childByToolTip(QWidget& parent, const char* toolTip)
{
    for (Widget* child : parent.findChildren<Widget*>()) {
        if (child->toolTip() == QLatin1String(toolTip)) {
            return child;
        }
    }
    fail(std::string("missing child widget: ") + toolTip);
    return nullptr;
}

QList<QToolButton*> colorGridButtons(QWidget& parent, const char* gridToolTip)
{
    return childByToolTip<QWidget>(parent, gridToolTip)
        ->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
}

int enabledButtonCount(const QList<QToolButton*>& buttons)
{
    int count = 0;
    for (const QToolButton* button : buttons) {
        count += button->isEnabled() ? 1 : 0;
    }
    return count;
}

void testColorRotationPanel()
{
    using colorizing::render::Resolution;
    colorizing::document::ProjectDocument document = makePreviewDocument();
    document.setColorRotationLength(0, Resolution::Sd, 1, 3);
    document.setColorRotationDelayMs(0, Resolution::Sd, 1, 250);
    document.setColorRotationColor(0, Resolution::Sd, 1, 0, 0xf800);
    document.setColorRotationLength(0, Resolution::Hd, 1, 2);
    document.markClean();

    colorizing::app::ColorRotationPanel panel;
    int slotChanges = 0;
    int otherCallbacks = 0;
    std::optional<std::pair<std::size_t, int>> length;
    std::optional<std::pair<std::size_t, int>> delay;
    std::optional<std::pair<std::size_t, std::size_t>> color;
    std::optional<std::size_t> clearedSlot;
    panel.onSlotSelectionChanged = [&]() {
        ++slotChanges;
    };
    panel.onLengthEdited = [&](std::size_t slotIndex, int value) {
        ++otherCallbacks;
        length = {slotIndex, value};
    };
    panel.onDelayEdited = [&](std::size_t slotIndex, int value) {
        ++otherCallbacks;
        delay = {slotIndex, value};
    };
    panel.onColorClicked = [&](std::size_t slotIndex, std::size_t colorIndex) {
        ++otherCallbacks;
        color = {slotIndex, colorIndex};
    };
    panel.onClearSlotRequested = [&](std::size_t slotIndex) {
        ++otherCallbacks;
        clearedSlot = slotIndex;
    };

    auto* slotComboBox =
        childByToolTip<QComboBox>(panel, "Color rotation slot for the active SD/HD mode");
    auto* lengthSpinBox = childByToolTip<QSpinBox>(panel, "Number of colors in this rotation");
    auto* delaySpinBox = childByToolTip<QSpinBox>(panel, "Delay between rotation steps");
    auto* clearButton =
        childByToolTip<QToolButton>(panel, "Clear the selected color rotation slot");
    const QList<QToolButton*> gridButtons =
        colorGridButtons(panel, "Colors in the selected rotation");
    expect(slotComboBox->count() == static_cast<int>(colorizing::document::kMaxColorRotations),
           "rotation panel should list every rotation slot");
    expect(gridButtons.size() ==
               static_cast<int>(colorizing::document::kMaxColorRotationColors),
           "rotation panel should show one button per rotation color");
    expect(clearButton->text() == QStringLiteral("Clear Slot"),
           "rotation panel should keep the clear button text");

    panel.refresh(nullptr, std::nullopt, Resolution::Sd);
    expect(!slotComboBox->isEnabled() && !lengthSpinBox->isEnabled() &&
               !delaySpinBox->isEnabled() && !clearButton->isEnabled(),
           "rotation panel should be disabled without a frame");
    expect(enabledButtonCount(gridButtons) == 0,
           "rotation panel should show no active colors without a frame");

    panel.refresh(&document, 0, Resolution::Sd);
    expect(slotComboBox->isEnabled() && lengthSpinBox->isEnabled() &&
               delaySpinBox->isEnabled() && clearButton->isEnabled(),
           "rotation panel should be enabled for a frame");
    expect(panel.selectedSlotIndex() == 0, "rotation panel should start on slot 0");
    expect(lengthSpinBox->value() == 0 && delaySpinBox->value() == 0,
           "rotation panel should show the empty slot 0");

    slotComboBox->setCurrentIndex(1);
    expect(slotChanges == 1 && panel.selectedSlotIndex() == 1,
           "selecting a rotation slot should notify once");
    expect(lengthSpinBox->value() == 0,
           "rotation panel should wait for the owner to refresh after a slot change");
    panel.refresh(&document, 0, Resolution::Sd);
    expect(lengthSpinBox->value() == 3 && delaySpinBox->value() == 250,
           "rotation panel should show length and delay of the selected slot");
    expect(enabledButtonCount(gridButtons) == 3 &&
               gridButtons[0]->toolTip() == QStringLiteral("Color 0: 0xf800"),
           "rotation panel should show the active rotation colors");
    panel.refresh(&document, 0, Resolution::Hd);
    expect(lengthSpinBox->value() == 2 && delaySpinBox->value() == 0,
           "rotation panel should show the slot of the requested resolution");
    panel.refresh(&document, 0, Resolution::Sd);
    expect(slotChanges == 1 && otherCallbacks == 0,
           "refreshing the rotation panel should not notify");

    lengthSpinBox->setValue(5);
    expect(length == std::make_pair(std::size_t{1}, 5),
           "editing the rotation length should report slot and length");
    delaySpinBox->setValue(120);
    expect(delay == std::make_pair(std::size_t{1}, 120),
           "editing the rotation delay should report slot and delay");
    gridButtons[2]->click();
    expect(color == std::make_pair(std::size_t{1}, std::size_t{2}),
           "clicking a rotation color should report slot and color index");
    clearButton->click();
    expect(clearedSlot == std::size_t{1}, "clearing should report the selected slot");
    expect(otherCallbacks == 4, "each rotation edit should notify exactly once");
    expect(document.colorRotationLength(0, Resolution::Sd, 1) == 3 &&
               document.colorRotationDelayMs(0, Resolution::Sd, 1) == 250 &&
               !document.isDirty(),
           "rotation panel should leave the document untouched");

    panel.refresh(&document, std::nullopt, Resolution::Sd);
    expect(!lengthSpinBox->isEnabled() && lengthSpinBox->value() == 0 &&
               delaySpinBox->value() == 0,
           "rotation panel should reset without a frame");
    expect(slotChanges == 1 && otherCallbacks == 4,
           "resetting the rotation panel should not notify");
}

colorizing::document::ProjectDocument makePaletteDocument(std::size_t paletteCount)
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "palettes";
    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
    });
    for (std::size_t paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
        colorizing::document::PaletteRecord palette;
        palette.colors.assign(colorizing::document::kPaletteColorCount, 0);
        palette.colors[0] = 0xf800;
        palette.name = paletteIndex == 1 ? "second" : "";
        snapshot.palettes.push_back(palette);
    }
    return colorizing::document::ProjectDocument::fromSnapshot(snapshot);
}

void testPalettePanel()
{
    using colorizing::app::PaletteGradientShape;
    using colorizing::edit::PaletteGradientDirection;
    using colorizing::edit::PaletteGradientPattern;
    const colorizing::document::ProjectDocument document = makePaletteDocument(3);
    const colorizing::document::ProjectDocument smallDocument = makePaletteDocument(1);

    colorizing::app::PalettePanel panel;
    int callbacks = 0;
    int paletteChanges = 0;
    int gradientSettingChanges = 0;
    int applyRequests = 0;
    int fillRequests = 0;
    int directionClicks = 0;
    int clearRequests = 0;
    std::optional<QString> name;
    std::optional<std::size_t> clickedColor;
    std::optional<std::size_t> doubleClickedColor;
    panel.onPaletteSelectionChanged = [&]() {
        ++callbacks;
        ++paletteChanges;
    };
    panel.onGradientSettingsChanged = [&]() {
        ++callbacks;
        ++gradientSettingChanges;
    };
    panel.onNameEdited = [&](const QString& value) {
        ++callbacks;
        name = value;
    };
    panel.onColorClicked = [&](std::size_t colorIndex) {
        ++callbacks;
        clickedColor = colorIndex;
    };
    panel.onColorDoubleClicked = [&](std::size_t colorIndex) {
        ++callbacks;
        doubleClickedColor = colorIndex;
    };
    panel.onApplyGradientRequested = [&]() {
        ++callbacks;
        ++applyRequests;
    };
    panel.onFillSelectionRequested = [&]() {
        ++callbacks;
        ++fillRequests;
    };
    panel.onSelectionDirectionClicked = [&]() {
        ++callbacks;
        ++directionClicks;
    };
    panel.onClearSelectionRequested = [&]() {
        ++callbacks;
        ++clearRequests;
    };

    auto* paletteComboBox = childByToolTip<QComboBox>(panel, "Selected palette");
    auto* nameLineEdit = childByToolTip<QLineEdit>(panel, "Selected palette name");
    auto* startSpinBox =
        childByToolTip<QSpinBox>(panel, "First palette color index for gradient fill");
    auto* endSpinBox =
        childByToolTip<QSpinBox>(panel, "Last palette color index for gradient fill");
    auto* shapeComboBox = childByToolTip<QComboBox>(panel, "Shape used by the Grad drawing tool");
    auto* patternComboBox =
        childByToolTip<QComboBox>(panel, "Color distribution used by the Grad drawing tool");
    auto* directionComboBox =
        childByToolTip<QComboBox>(panel, "Direction used by the Grad drawing tool");
    auto* applyButton = childByToolTip<QToolButton>(
        panel,
        "Fill the selected palette range between the endpoint colors");
    auto* fillButton = childByToolTip<QToolButton>(
        panel,
        "Fill the current canvas selection with the palette gradient");
    auto* directionButton = childByToolTip<QToolButton>(
        panel,
        "Draw the free gradient direction line for the current selection");
    auto* clearButton =
        childByToolTip<QToolButton>(panel, "Clear the current canvas selection");
    const QList<QToolButton*> gridButtons =
        colorGridButtons(panel, "Colors in the selected palette");
    QLabel* selectedLabel = nullptr;
    for (QLabel* label : panel.findChildren<QLabel*>()) {
        if (label->text().startsWith(QStringLiteral("Selected:"))) {
            selectedLabel = label;
        }
    }
    expect(selectedLabel != nullptr, "palette panel should show the selected palette label");
    expect(gridButtons.size() == static_cast<int>(colorizing::document::kPaletteColorCount) &&
               gridButtons[0]->text() == QStringLiteral("1"),
           "palette panel should show one-based color buttons");

    panel.refresh(nullptr, false, false);
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               !paletteComboBox->isEnabled() && !nameLineEdit->isEnabled() &&
               !applyButton->isEnabled() && enabledButtonCount(gridButtons) == 0,
           "palette panel should be disabled without a document");
    expect(!panel.selectedPaletteIndex(nullptr).has_value(),
           "palette panel should report no palette without a document");
    expect(panel.gradientEndpoints() == std::make_pair(0, 63) &&
               panel.gradientShape() == PaletteGradientShape::Rectangle &&
               panel.gradientPattern() == PaletteGradientPattern::Linear &&
               panel.gradientDirection() == PaletteGradientDirection::Free,
           "palette panel should start with the default gradient settings");

    panel.setPalettes(&document);
    panel.ensurePaletteSelected(&document);
    panel.refresh(&document, false, false);
    expect(paletteComboBox->count() == 3 &&
               paletteComboBox->itemText(0) == QStringLiteral("0") &&
               paletteComboBox->itemText(1) == QStringLiteral("1 - second"),
           "palette panel should list the document palettes");
    expect(panel.selectedPaletteIndex(&document) == std::size_t{0} &&
               selectedLabel->text() == QStringLiteral("Selected: 0") &&
               nameLineEdit->text().isEmpty() && nameLineEdit->isEnabled(),
           "palette panel should select and show the first palette");
    expect(applyButton->isEnabled() && !fillButton->isEnabled() &&
               !directionButton->isEnabled() && !clearButton->isEnabled(),
           "palette panel should only enable selection buttons with a canvas selection");
    expect(enabledButtonCount(gridButtons) == 64 &&
               gridButtons[0]->toolTip() == QStringLiteral("Color 1: 0xf800"),
           "palette panel should show the palette colors");
    expect(callbacks == 0, "populating and refreshing the palette panel should not notify");

    paletteComboBox->setCurrentIndex(1);
    expect(paletteChanges == 1 && callbacks == 1 &&
               panel.selectedPaletteIndex(&document) == std::size_t{1},
           "selecting a palette should notify once");
    expect(!panel.selectedPaletteIndex(&smallDocument).has_value(),
           "palette panel should not report a palette the document does not have");
    panel.setPalettes(&document);
    panel.ensurePaletteSelected(&document);
    panel.refresh(&document, false, false);
    expect(panel.selectedPaletteIndex(&document) == std::size_t{1} &&
               selectedLabel->text() == QStringLiteral("Selected: 1 - second") &&
               nameLineEdit->text() == QStringLiteral("second"),
           "palette panel should keep the selected palette when the list is rebuilt");
    expect(callbacks == 1, "rebuilding the palette list should not notify");

    startSpinBox->setValue(3);
    endSpinBox->setValue(1);
    expect(panel.gradientEndpoints() == std::make_pair(2, 0) && callbacks == 1,
           "gradient endpoints should be zero-based and silent");
    shapeComboBox->setCurrentIndex(2);
    patternComboBox->setCurrentIndex(1);
    directionComboBox->setCurrentIndex(5);
    expect(gradientSettingChanges == 3 &&
               panel.gradientShape() == PaletteGradientShape::Ellipse &&
               panel.gradientPattern() == PaletteGradientPattern::Circle &&
               panel.gradientDirection() == PaletteGradientDirection::TopLeftToBottomRight,
           "gradient settings should notify and be readable");

    nameLineEdit->setText(QStringLiteral("renamed"));
    expect(!name.has_value(), "typing a palette name should not notify before it is finished");
    Q_EMIT nameLineEdit->editingFinished();
    expect(name == QStringLiteral("renamed") && document.palette(1).name == "second",
           "finishing the palette name should report it without renaming the palette");
    gridButtons[4]->click();
    expect(clickedColor == std::size_t{4}, "clicking a palette color should report its index");
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,
                            QPointF(5, 5),
                            QPointF(5, 5),
                            QPointF(5, 5),
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
    QApplication::sendEvent(gridButtons[6], &doubleClick);
    expect(doubleClickedColor == std::size_t{6},
           "double-clicking a palette color should report its index");
    applyButton->click();
    fillButton->click();
    directionButton->click();
    clearButton->click();
    expect(applyRequests == 1 && fillRequests == 0 && directionClicks == 0 &&
               clearRequests == 0,
           "disabled selection buttons should not notify");

    const int callbacksBeforeRefresh = callbacks;
    panel.refresh(&document, true, true);
    expect(fillButton->isEnabled() && directionButton->isEnabled() &&
               clearButton->isEnabled() && directionButton->isChecked(),
           "palette panel should show the canvas selection state");
    panel.refresh(&document, true, false);
    expect(!directionButton->isChecked(), "palette panel should show direction mode off");
    panel.setSelectionDirectionChecked(true);
    expect(directionButton->isChecked() && callbacks == callbacksBeforeRefresh,
           "refreshing the selection state should not notify");
    fillButton->click();
    directionButton->click();
    clearButton->click();
    expect(fillRequests == 1 && directionClicks == 1 && clearRequests == 1,
           "selection buttons should notify once each");
    panel.refresh(&document, false, true);
    expect(!directionButton->isEnabled() && !directionButton->isChecked(),
           "direction mode needs a canvas selection");

    const int callbacksBeforeClose = callbacks;
    panel.setPalettes(nullptr);
    panel.refresh(nullptr, true, true);
    expect(paletteComboBox->count() == 0 && !paletteComboBox->isEnabled() &&
               selectedLabel->text() == QStringLiteral("Selected: None") &&
               nameLineEdit->text().isEmpty() && !directionButton->isChecked() &&
               !fillButton->isEnabled() && enabledButtonCount(gridButtons) == 0,
           "palette panel should reset without a document");
    expect(callbacks == callbacksBeforeClose, "resetting the palette panel should not notify");
}

colorizing::document::ProjectDocument makeSpritePanelDocument()
{
    using colorizing::document::FrameSpriteReference;
    using colorizing::document::SpriteDetectionArea;
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "sprite_panel";
    // Frame 0 uses two sprites, frame 1 has only empty slots, frame 2 has no slot table.
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
        colorizing::document::ProjectFrameSnapshot frame{
            colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
            colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        };
        if (frameIndex < 2) {
            frame.spriteReferences.assign(colorizing::document::kMaxSpritesPerFrame,
                                          FrameSpriteReference{});
        }
        snapshot.frames.push_back(frame);
    }
    snapshot.frames[0].spriteReferences[0] = FrameSpriteReference{0, 10, 5, 40, 20};
    snapshot.frames[0].spriteReferences[2] = FrameSpriteReference{1, 50, 2, 70, 12};

    const colorizing::render::Size spriteSize{
        static_cast<int>(colorizing::document::kMaxSpriteWidth),
        static_cast<int>(colorizing::document::kMaxSpriteHeight)};
    colorizing::document::SpriteRecord sprite;
    sprite.originalSd = colorizing::document::makeIndexedFrame(spriteSize, 255);
    sprite.colorizedSd = colorizing::document::makeRgb565Frame(spriteSize, 0);
    sprite.maskHd = colorizing::document::makeIndexedFrame(spriteSize, 255);
    sprite.colorizedHd = colorizing::document::makeRgb565Frame(spriteSize, 0);
    sprite.detectionDwords.assign(colorizing::document::kMaxSpriteDetectAreas, 0xffffffff);
    sprite.detectionDwordPositions.assign(colorizing::document::kMaxSpriteDetectAreas, 0xffff);
    sprite.detectionAreas.assign(colorizing::document::kMaxSpriteDetectAreas,
                                 SpriteDetectionArea{});
    sprite.originalSd.pixels[0] = 1;
    sprite.colorizedSd.pixels[0] = 0xf800;
    snapshot.sprites.push_back(sprite);
    snapshot.sprites.push_back(sprite);
    snapshot.sprites[0].name = "hero";
    snapshot.sprites[0].hasHd = true;
    snapshot.sprites[0].detectionAreas[0] = SpriteDetectionArea{4, 5, 20, 8};
    colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

void testSpritePanel()
{
    using colorizing::app::SpriteDetectionAreaInput;
    using colorizing::app::SpritePanelState;
    using colorizing::app::SpriteSlotBounds;
    const colorizing::document::ProjectDocument document = makeSpritePanelDocument();
    const auto stateFor = [&document](std::optional<std::size_t> frameIndex,
                                      std::optional<std::size_t> spriteIndex) {
        SpritePanelState state;
        state.document = &document;
        state.frameIndex = frameIndex;
        state.spriteIndex = spriteIndex;
        return state;
    };

    colorizing::app::SpritePanel panel;
    int callbacks = 0;
    int slotChanges = 0;
    int areaChanges = 0;
    int assignRequests = 0;
    int captureRequests = 0;
    int removeRequests = 0;
    int clearCaptureRequests = 0;
    int applyRequests = 0;
    int clearAreaRequests = 0;
    std::optional<QString> name;
    std::optional<bool> captureSelection;
    std::optional<bool> boundsOverlay;
    std::optional<bool> detectionAreas;
    std::optional<bool> spriteCanvas;
    std::optional<SpriteSlotBounds> bounds;
    std::optional<SpriteDetectionAreaInput> area;
    panel.onNameEdited = [&](const QString& value) {
        ++callbacks;
        name = value;
    };
    panel.onSlotSelectionChanged = [&]() {
        ++callbacks;
        ++slotChanges;
    };
    panel.onAssignRequested = [&]() {
        ++callbacks;
        ++assignRequests;
    };
    panel.onCaptureRequested = [&]() {
        ++callbacks;
        ++captureRequests;
    };
    panel.onRemoveRequested = [&]() {
        ++callbacks;
        ++removeRequests;
    };
    panel.onCaptureSelectionToggled = [&](bool checked) {
        ++callbacks;
        captureSelection = checked && panel.isCaptureSelectionChecked();
    };
    panel.onClearCaptureSelectionRequested = [&]() {
        ++callbacks;
        ++clearCaptureRequests;
    };
    panel.onBoundsOverlayToggled = [&](bool checked) {
        ++callbacks;
        boundsOverlay = checked && panel.isBoundsOverlayChecked();
    };
    panel.onDetectionAreasToggled = [&](bool checked) {
        ++callbacks;
        detectionAreas = checked && panel.isDetectionAreasChecked();
    };
    panel.onSpriteCanvasToggled = [&](bool checked) {
        ++callbacks;
        spriteCanvas = checked && panel.isSpriteCanvasChecked();
    };
    panel.onApplySpriteEditsRequested = [&]() {
        ++callbacks;
        ++applyRequests;
    };
    panel.onSlotBoundsEdited = [&]() {
        ++callbacks;
        bounds = panel.slotBounds();
    };
    panel.onDetectionAreaSelectionChanged = [&]() {
        ++callbacks;
        ++areaChanges;
    };
    panel.onDetectionAreaEdited = [&]() {
        ++callbacks;
        area = panel.detectionAreaInput();
    };
    panel.onClearDetectionAreaRequested = [&]() {
        ++callbacks;
        ++clearAreaRequests;
    };

    auto* nameLineEdit = childByToolTip<QLineEdit>(panel, "Selected sprite name");
    auto* slotComboBox = childByToolTip<QComboBox>(panel, "Current frame sprite slot");
    auto* assignButton =
        childByToolTip<QToolButton>(panel, "Assign selected sprite to selected slot");
    auto* captureButton = childByToolTip<QToolButton>(
        panel,
        "Copy original/colorized pixels from current frame bounds into selected sprite");
    auto* removeButton = childByToolTip<QToolButton>(panel, "Clear selected sprite slot");
    auto* captureSelectionButton = childByToolTip<QToolButton>(
        panel,
        "Draw a shaped sprite capture selection on the original frame");
    auto* clearCaptureButton =
        childByToolTip<QToolButton>(panel, "Clear the current sprite capture selection");
    auto* boundsOverlayButton = childByToolTip<QToolButton>(
        panel,
        "Show selected sprite slot bounds on original frame");
    auto* detectionAreasButton = childByToolTip<QToolButton>(
        panel,
        "Show sprite detection areas on original sprite preview");
    auto* spriteCanvasButton =
        childByToolTip<QToolButton>(panel, "Show selected sprite in the main canvas");
    auto* applyButton =
        childByToolTip<QToolButton>(panel, "Refresh frame previews from edited sprite data");
    auto* minXSpinBox = childByToolTip<QSpinBox>(panel, "Sprite slot bounding box minimum X");
    auto* minYSpinBox = childByToolTip<QSpinBox>(panel, "Sprite slot bounding box minimum Y");
    auto* maxXSpinBox = childByToolTip<QSpinBox>(panel, "Sprite slot bounding box maximum X");
    auto* maxYSpinBox = childByToolTip<QSpinBox>(panel, "Sprite slot bounding box maximum Y");
    auto* areaComboBox = childByToolTip<QComboBox>(panel, "Sprite detection area");
    auto* clearAreaButton =
        childByToolTip<QToolButton>(panel, "Disable selected sprite detection area");
    auto* areaXSpinBox = childByToolTip<QSpinBox>(panel, "Sprite detection area X");
    auto* areaYSpinBox = childByToolTip<QSpinBox>(panel, "Sprite detection area Y");
    auto* areaWidthSpinBox = childByToolTip<QSpinBox>(panel, "Sprite detection area width");
    auto* areaHeightSpinBox = childByToolTip<QSpinBox>(panel, "Sprite detection area height");
    auto* originalPreviewLabel = childByToolTip<QLabel>(panel, "Sprite original mask preview");
    QLabel* selectedLabel = nullptr;
    QLabel* frameSpritesLabel = nullptr;
    for (QLabel* label : panel.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Selected: None")) {
            selectedLabel = label;
        } else if (label->text() == QStringLiteral("No frame sprites")) {
            frameSpritesLabel = label;
        }
    }
    expect(selectedLabel && frameSpritesLabel,
           "sprite panel should start with its empty selection and frame labels");
    expect(assignButton->text() == QStringLiteral("Assign") &&
               captureButton->text() == QStringLiteral("Capture") &&
               removeButton->text() == QStringLiteral("Remove") &&
               captureSelectionButton->text() == QStringLiteral("Select Capture") &&
               clearCaptureButton->text() == QStringLiteral("Clear Sel") &&
               boundsOverlayButton->text() == QStringLiteral("Show Bounds") &&
               detectionAreasButton->text() == QStringLiteral("Show Det Areas") &&
               spriteCanvasButton->text() == QStringLiteral("Edit Sprite") &&
               applyButton->text() == QStringLiteral("Apply Sprite") &&
               clearAreaButton->text() == QStringLiteral("Clear Area"),
           "sprite panel should keep its button texts");
    expect(captureSelectionButton->isCheckable() && boundsOverlayButton->isCheckable() &&
               detectionAreasButton->isCheckable() && spriteCanvasButton->isCheckable() &&
               !assignButton->isCheckable(),
           "sprite panel should keep its toggle buttons");
    expect(nameLineEdit->maxLength() == 31 && minXSpinBox->maximum() == 127 &&
               maxYSpinBox->maximum() == 31 && areaXSpinBox->maximum() == 255 &&
               areaYSpinBox->maximum() == 63 && areaWidthSpinBox->minimum() == 1 &&
               areaWidthSpinBox->maximum() == 256 && areaHeightSpinBox->maximum() == 64,
           "sprite panel should keep its input limits");
    expect(areaComboBox->count() ==
                   static_cast<int>(colorizing::document::kMaxSpriteDetectAreas) &&
               areaComboBox->itemText(1) == QStringLiteral("Area 1"),
           "sprite panel should list every detection area");
    expect(!originalPreviewLabel->isVisibleTo(&panel),
           "sprite panel should keep the sprite previews hidden");

    panel.refresh(SpritePanelState{});
    expect(!nameLineEdit->isEnabled() && !slotComboBox->isEnabled() &&
               !assignButton->isEnabled() && !captureButton->isEnabled() &&
               !removeButton->isEnabled() && !captureSelectionButton->isEnabled() &&
               !clearCaptureButton->isEnabled() && !boundsOverlayButton->isEnabled() &&
               !detectionAreasButton->isEnabled() && !spriteCanvasButton->isEnabled() &&
               !applyButton->isEnabled() && !minXSpinBox->isEnabled() &&
               !areaComboBox->isEnabled() && !clearAreaButton->isEnabled() &&
               !areaXSpinBox->isEnabled(),
           "sprite panel should be disabled without a document");
    expect(slotComboBox->count() == 1 &&
               slotComboBox->itemText(0) == QStringLiteral("No slots") &&
               !panel.selectedSlotIndex().has_value(),
           "sprite panel should offer no slot without a document");
    expect(maxXSpinBox->value() == 127 && maxYSpinBox->value() == 31,
           "sprite panel should show the whole frame as bounds without a slot");

    panel.refresh(stateFor(0, 0));
    expect(selectedLabel->text() == QStringLiteral("Selected: 0 - hero (HD)") &&
               nameLineEdit->text() == QStringLiteral("hero") && nameLineEdit->isEnabled(),
           "sprite panel should show the selected named HD sprite");
    expect(slotComboBox->isEnabled() &&
               slotComboBox->count() ==
                   static_cast<int>(colorizing::document::kMaxSpritesPerFrame) &&
               slotComboBox->itemText(0) == QStringLiteral("Slot 0: Sprite 0 - hero") &&
               slotComboBox->itemText(1) == QStringLiteral("Slot 1: Empty") &&
               slotComboBox->itemText(2) == QStringLiteral("Slot 2: Sprite 1") &&
               panel.selectedSlotIndex() == std::size_t{0},
           "sprite panel should list the sprite slots of the frame");
    expect(frameSpritesLabel->text() ==
               QStringLiteral("0: Sprite 0 - hero [10,5-40,20]\n2: Sprite 1 [50,2-70,12]"),
           "sprite panel should summarize the sprites the frame uses");
    expect(assignButton->isEnabled() && captureButton->isEnabled() &&
               captureSelectionButton->isEnabled() && removeButton->isEnabled() &&
               boundsOverlayButton->isEnabled() && spriteCanvasButton->isEnabled() &&
               detectionAreasButton->isEnabled() && !clearCaptureButton->isEnabled() &&
               !applyButton->isEnabled(),
           "sprite panel should enable the actions of an assigned slot");
    SpriteSlotBounds shownBounds = panel.slotBounds();
    expect(minXSpinBox->isEnabled() && shownBounds.minX == 10 && shownBounds.minY == 5 &&
               shownBounds.maxX == 40 && shownBounds.maxY == 20,
           "sprite panel should show the bounds of the selected slot");
    SpriteDetectionAreaInput shownArea = panel.detectionAreaInput();
    expect(areaComboBox->isEnabled() && panel.selectedDetectionAreaIndex() == 0 &&
               clearAreaButton->isEnabled() && areaXSpinBox->isEnabled() &&
               shownArea.x == 4 && shownArea.y == 5 && shownArea.width == 20 &&
               shownArea.height == 8,
           "sprite panel should show the selected detection area");
    expect(!originalPreviewLabel->pixmap().isNull(),
           "sprite panel should render the hidden sprite preview");

    SpritePanelState pendingState = stateFor(0, 0);
    pendingState.hasCaptureSelection = true;
    pendingState.spriteEditsPending = true;
    panel.refresh(pendingState);
    expect(clearCaptureButton->isEnabled() && applyButton->isEnabled(),
           "sprite panel should show a capture selection and pending sprite edits");
    panel.refresh(stateFor(0, 0));
    panel.setApplySpriteEditsEnabled(true);
    expect(!clearCaptureButton->isEnabled() && applyButton->isEnabled(),
           "pending sprite edits should be settable without a refresh");
    panel.setApplySpriteEditsEnabled(false);

    panel.refresh(stateFor(0, 1));
    shownArea = panel.detectionAreaInput();
    expect(selectedLabel->text() == QStringLiteral("Selected: 1") &&
               nameLineEdit->text().isEmpty() && nameLineEdit->isEnabled() &&
               spriteCanvasButton->isEnabled() && !detectionAreasButton->isEnabled() &&
               !clearAreaButton->isEnabled() && areaXSpinBox->isEnabled() &&
               shownArea.x == 0 && shownArea.y == 0 && shownArea.width == 1 &&
               shownArea.height == 1,
           "sprite panel should show an unnamed SD sprite without detection areas");

    panel.refresh(stateFor(1, 0));
    shownBounds = panel.slotBounds();
    expect(frameSpritesLabel->text() == QStringLiteral("Current frame uses no sprites") &&
               slotComboBox->itemText(0) == QStringLiteral("Slot 0: Empty") &&
               assignButton->isEnabled() && !removeButton->isEnabled() &&
               boundsOverlayButton->isEnabled() && minXSpinBox->isEnabled() &&
               shownBounds.maxX == 0 && shownBounds.maxY == 0,
           "sprite panel should show a frame whose slots are all empty");

    panel.refresh(stateFor(2, 0));
    shownBounds = panel.slotBounds();
    expect(slotComboBox->itemText(0) == QStringLiteral("No slots") &&
               slotComboBox->isEnabled() && !panel.selectedSlotIndex().has_value() &&
               !assignButton->isEnabled() && !captureButton->isEnabled() &&
               !captureSelectionButton->isEnabled() && !boundsOverlayButton->isEnabled() &&
               !minXSpinBox->isEnabled() && shownBounds.minX == 0 &&
               shownBounds.maxX == 127 && shownBounds.maxY == 31 &&
               spriteCanvasButton->isEnabled(),
           "sprite panel should show a frame without sprite slots");

    panel.refresh(stateFor(std::nullopt, 0));
    expect(frameSpritesLabel->text() == QStringLiteral("No frame sprites") &&
               !slotComboBox->isEnabled() && !assignButton->isEnabled() &&
               nameLineEdit->isEnabled() && areaComboBox->isEnabled(),
           "sprite panel should keep the sprite editable without a frame");

    SpritePanelState pendingWithoutSprite = stateFor(0, std::nullopt);
    pendingWithoutSprite.hasCaptureSelection = true;
    pendingWithoutSprite.spriteEditsPending = true;
    panel.refresh(pendingWithoutSprite);
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               !nameLineEdit->isEnabled() && nameLineEdit->text().isEmpty() &&
               slotComboBox->isEnabled() && removeButton->isEnabled() &&
               boundsOverlayButton->isEnabled() && !assignButton->isEnabled() &&
               !captureButton->isEnabled() && !captureSelectionButton->isEnabled() &&
               !clearCaptureButton->isEnabled() && !applyButton->isEnabled() &&
               !spriteCanvasButton->isEnabled() && !detectionAreasButton->isEnabled() &&
               !areaComboBox->isEnabled() && !areaXSpinBox->isEnabled() &&
               originalPreviewLabel->text() == QStringLiteral("Original"),
           "sprite panel should keep only the slot controls without a sprite");
    panel.refresh(stateFor(0, 7));
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               !assignButton->isEnabled(),
           "sprite panel should treat an unknown sprite as no sprite");
    expect(callbacks == 0, "refreshing the sprite panel should not notify");

    panel.refresh(stateFor(0, 0));
    slotComboBox->setCurrentIndex(2);
    expect(slotChanges == 1 && callbacks == 1 && panel.selectedSlotIndex() == std::size_t{2},
           "selecting a sprite slot should notify once");
    expect(panel.slotBounds().minX == 10,
           "sprite panel should wait for the owner to refresh after a slot change");
    panel.refresh(stateFor(0, 0));
    shownBounds = panel.slotBounds();
    expect(panel.selectedSlotIndex() == std::size_t{2} && shownBounds.minX == 50 &&
               shownBounds.minY == 2 && shownBounds.maxX == 70 && shownBounds.maxY == 12,
           "sprite panel should keep the selected slot across a refresh");
    areaComboBox->setCurrentIndex(1);
    expect(areaChanges == 1 && callbacks == 2 && panel.selectedDetectionAreaIndex() == 1,
           "selecting a detection area should notify once");
    panel.refresh(stateFor(0, 0));
    shownArea = panel.detectionAreaInput();
    expect(panel.selectedDetectionAreaIndex() == 1 && !clearAreaButton->isEnabled() &&
               shownArea.x == 0 && shownArea.width == 1,
           "sprite panel should keep the selected detection area across a refresh");
    panel.setSelectedDetectionAreaIndex(3);
    expect(panel.selectedDetectionAreaIndex() == 3, "the detection area should be settable");
    panel.setSelectedDetectionAreaIndex(colorizing::document::kMaxSpriteDetectAreas);
    expect(panel.selectedDetectionAreaIndex() == 3 && callbacks == 2,
           "setting the detection area should neither notify nor accept an unknown area");

    nameLineEdit->setText(QStringLiteral("renamed"));
    expect(!name.has_value(), "typing a sprite name should not notify before it is finished");
    Q_EMIT nameLineEdit->editingFinished();
    expect(name == QStringLiteral("renamed") && document.sprite(0).name == "hero",
           "finishing the sprite name should report it without renaming the sprite");
    assignButton->click();
    captureButton->click();
    removeButton->click();
    clearCaptureButton->click();
    applyButton->click();
    clearAreaButton->click();
    expect(assignRequests == 1 && captureRequests == 1 && removeRequests == 1 &&
               clearCaptureRequests == 0 && applyRequests == 0 && clearAreaRequests == 0,
           "only enabled sprite buttons should notify");
    panel.refresh(pendingState);
    areaComboBox->setCurrentIndex(0);
    panel.refresh(pendingState);
    clearCaptureButton->click();
    applyButton->click();
    clearAreaButton->click();
    expect(clearCaptureRequests == 1 && applyRequests == 1 && clearAreaRequests == 1 &&
               areaChanges == 2,
           "the remaining sprite buttons should notify once each");

    captureSelectionButton->click();
    boundsOverlayButton->click();
    detectionAreasButton->click();
    spriteCanvasButton->click();
    expect(captureSelection == true && boundsOverlay == true && detectionAreas == true &&
               spriteCanvas == true,
           "sprite toggles should report being switched on");
    const int callbacksBeforeSetters = callbacks;
    panel.setSpriteCanvasChecked(false);
    expect(!panel.isSpriteCanvasChecked() && spriteCanvas == true &&
               callbacks == callbacksBeforeSetters,
           "leaving the sprite canvas from the owner should not notify");
    panel.refresh(stateFor(2, std::nullopt));
    expect(panel.isCaptureSelectionChecked() && panel.isBoundsOverlayChecked() &&
               panel.isDetectionAreasChecked() && !captureSelectionButton->isEnabled() &&
               callbacks == callbacksBeforeSetters,
           "sprite toggles should stay switched on while they are unavailable");
    panel.refresh(stateFor(0, 0));
    expect(panel.selectedSlotIndex() == std::size_t{0},
           "sprite panel should fall back to slot 0 after a frame without slots");
    captureSelectionButton->click();
    boundsOverlayButton->click();
    detectionAreasButton->click();
    spriteCanvasButton->click();
    spriteCanvasButton->click();
    expect(captureSelection == false && boundsOverlay == false && detectionAreas == false &&
               spriteCanvas == false,
           "sprite toggles should report being switched off");

    maxXSpinBox->setValue(3);
    expect(bounds.has_value() && bounds->minX == 10 && bounds->minY == 5 &&
               bounds->maxX == 3 && bounds->maxY == 20,
           "editing the slot bounds should report them as entered");
    bounds.reset();
    minYSpinBox->setValue(9);
    expect(bounds.has_value() && bounds->minY == 9, "each slot bound should notify");
    areaWidthSpinBox->setValue(250);
    expect(area.has_value() && area->x == 4 && area->y == 5 && area->width == 250 &&
               area->height == 8,
           "editing the detection area should report it as entered");
    area.reset();
    areaYSpinBox->setValue(60);
    expect(area.has_value() && area->y == 60, "each detection area value should notify");

    const int callbacksBeforeReset = callbacks;
    panel.refresh(stateFor(0, 0));
    panel.refresh(SpritePanelState{});
    expect(callbacks == callbacksBeforeReset, "resetting the sprite panel should not notify");
    expect(!document.isDirty() && document.sprite(0).name == "hero" &&
               document.sprite(0).detectionAreas[0].width == 20 &&
               document.frameSpriteReferences(0)[2].maxX == 70,
           "sprite panel should leave the document untouched");
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
    testDynamicShadowsRender();
    testSpriteCompositionRendersInSdAndHd();
    testBackgroundImageEditModeRendersInSdAndHd();
    testFramePreviewDoesNotShowHdReferenceBelowOriginal();
    testSpriteEditorRendersAndEditsDetectionArea();
    testSpriteEditorHdKeepsSdFootprint();
    testColorRotationPreviewTransform();
    testColorRotationPanel();
    testPalettePanel();
    testSpritePanel();
    std::cout << "colorizing_qt_v2_widget_tests passed\n";
    return 0;
}
