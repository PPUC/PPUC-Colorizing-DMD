#include "FramePreviewWidget.h"
#include "PickToolReturn.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDropEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QPointingDevice>
#include <QSpinBox>
#include <QToolButton>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "panels/BackgroundPanel.h"
#include "panels/ColorRotationPanel.h"
#include "panels/DynamicPanel.h"
#include "panels/FramePanel.h"
#include "panels/MaskPanel.h"
#include "panels/PalettePanel.h"
#include "panels/RecordListPage.h"
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
        colorizing::document::kLegacySpritesPerFrame);
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

bool hasVisibleMagentaPixel(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color(image.pixel(x, y));
            if (color.red() > 120 && color.blue() > 120 && color.green() < 40) {
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
    expect(hasNonBackgroundPixel(pixmap.toImage()),
           "thumbnail should render non-background pixels");
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

// The canvas zoomed: both pictures show the same part, and what is drawn at a point is the
// pixel a click there hits, also after scrolling and when zooming at the mouse.
void testFramePreviewZoom()
{
    using colorizing::render::CanvasHit;
    using colorizing::render::CanvasRegion;
    colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::createEmpty({"zoom", 1, true});
    document.setColorizedPixel(0, colorizing::render::Resolution::Sd, 64, 16, 0xf800);
    document.setColorizedPixel(0, colorizing::render::Resolution::Sd, 120, 4, 0x07e0);
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    widget.setFrameIndex(0);

    // Every few screen pixels: what is hit there, per region.
    const auto shownPixels = [&widget](CanvasRegion region) {
        std::set<std::pair<int, int>> pixels;
        for (int y = 0; y < widget.height(); y += 2) {
            for (int x = 0; x < widget.width(); x += 2) {
                const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(x, y));
                if (hit && hit->region == region) {
                    pixels.insert({hit->x, hit->y});
                }
            }
        }
        return pixels;
    };
    const auto render = [&widget]() {
        QImage image(widget.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        widget.render(&painter);
        return image;
    };
    // The color drawn where a click hits the pixel.
    const auto colorAtPixel = [&](int pixelX, int pixelY) -> std::optional<QColor> {
        const QImage image = render();
        for (int y = 0; y < widget.height(); ++y) {
            for (int x = 0; x < widget.width(); ++x) {
                const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(x, y));
                if (hit && hit->region == CanvasRegion::ColorizedFrame && hit->x == pixelX &&
                    hit->y == pixelY) {
                    // The middle of the pixel, a few points further.
                    const QPoint middle(x + 3, y + 3);
                    const std::optional<CanvasHit> there = widget.hitTestAt(middle);
                    if (there && there->x == pixelX && there->y == pixelY) {
                        return image.pixelColor(middle);
                    }
                }
            }
        }
        return std::nullopt;
    };

    const std::set<std::pair<int, int>> whole = shownPixels(CanvasRegion::ColorizedFrame);
    expect(whole.size() == 128 * 32, "at zoom 1 the whole frame is shown");

    widget.setZoom(4.0);
    const std::set<std::pair<int, int>> colorized = shownPixels(CanvasRegion::ColorizedFrame);
    const std::set<std::pair<int, int>> original = shownPixels(CanvasRegion::OriginalFrame);
    int minX = 1000;
    int maxX = -1;
    int minY = 1000;
    int maxY = -1;
    for (const auto& [x, y] : colorized) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    expect(widget.zoom() == 4.0 && colorized.size() < whole.size() / 4 &&
               std::abs(minX + maxX - 127) <= 2 && std::abs(minY + maxY - 31) <= 2,
           "zoom 4 shows a part around the middle of the frame");
    expect(original == colorized, "and the original picture the same part");
    const std::optional<QColor> red = colorAtPixel(64, 16);
    expect(red && red->red() > 200 && red->green() < 60,
           "the pixel drawn where a click hits it is that pixel");

    // Scrolled to the top right: pixel 120,4 shows, green.
    widget.setViewOffset(QPointF(1.0, 0.0));
    expect(widget.viewOffset().x() < 1.0 && widget.viewOffset().y() == 0.0,
           "the offset stays in range");
    const std::optional<QColor> green = colorAtPixel(120, 4);
    expect(green && green->green() > 200 && green->red() < 60, "and is drawn there after scrolling");
    expect(shownPixels(CanvasRegion::OriginalFrame) == shownPixels(CanvasRegion::ColorizedFrame),
           "both pictures scroll together");

    // Zooming at a point keeps the pixel under it.
    QPoint point;
    for (int y = 0; y < widget.height() && point.isNull(); y += 4) {
        for (int x = 0; x < widget.width() && point.isNull(); x += 4) {
            const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(x, y));
            if (hit && hit->region == CanvasRegion::ColorizedFrame && hit->x == 110) {
                point = QPoint(x + 2, y + 2);
            }
        }
    }
    const std::optional<CanvasHit> before = widget.hitTestAt(point);
    widget.setZoom(8.0, point);
    const std::optional<CanvasHit> after = widget.hitTestAt(point);
    expect(before && after && before->x == after->x && before->y == after->y,
           "zooming at a point keeps the pixel under it");

    // Each scroll bar moves its own direction only.
    QScrollBar* horizontal = nullptr;
    QScrollBar* vertical = nullptr;
    for (QScrollBar* bar : widget.findChildren<QScrollBar*>()) {
        (bar->orientation() == Qt::Horizontal ? horizontal : vertical) = bar;
    }
    expect(horizontal && vertical, "the canvas has its scroll bars");
    widget.setViewOffset(QPointF(0.3, 0.4));
    const QPointF set = widget.viewOffset();
    vertical->setValue(vertical->value() - 500);
    expect(widget.viewOffset().x() == set.x() && widget.viewOffset().y() < set.y(),
           "the vertical scroll bar leaves the horizontal position");
    const QPointF moved = widget.viewOffset();
    horizontal->setValue(horizontal->value() - 500);
    expect(widget.viewOffset().y() == moved.y() && widget.viewOffset().x() < moved.x(),
           "and the horizontal one the vertical position");

    // Space held (the window turns pan mode on): a drag moves the shown part with the mouse and
    // paints nothing.
    widget.setZoom(4.0);
    widget.setViewOffset(QPointF(0.5, 0.5));
    const QPointF panStart = widget.viewOffset();
    widget.setPanMode(true);
    expect(widget.cursor().shape() == Qt::OpenHandCursor, "an open hand while Space is held");
    // Hovering over a pixel (no helper line under the mouse) keeps the hand. The window shows
    // the position, so the canvas follows the mouse without a button.
    widget.setPositionHandler([](auto, auto) {});
    const QPointF overPixel(widget.width() / 2.0, widget.height() / 4.0);
    expect(widget.hitTestAt(overPixel.toPoint()).has_value(), "the point is over a pixel");
    QMouseEvent hover(QEvent::MouseMove, overPixel, overPixel, widget.mapToGlobal(overPixel),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &hover);
    expect(widget.cursor().shape() == Qt::OpenHandCursor,
           "moving the mouse over the pixels keeps the hand");
    const QPointF from(widget.width() / 2.0, widget.height() / 4.0);
    const QPointF to = from + QPointF(40.0, 0.0);
    QMouseEvent panPress(QEvent::MouseButtonPress, from, from, widget.mapToGlobal(from),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &panPress);
    expect(widget.cursor().shape() == Qt::ClosedHandCursor, "a closed hand while dragging");
    QMouseEvent panMove(QEvent::MouseMove, to, to, widget.mapToGlobal(to), Qt::NoButton,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &panMove);
    QMouseEvent panRelease(QEvent::MouseButtonRelease, to, to, widget.mapToGlobal(to),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &panRelease);
    expect(widget.viewOffset().x() < panStart.x() && widget.viewOffset().y() == panStart.y(),
           "dragging to the right shows more of the left");
    widget.setPanMode(false);
    expect(widget.cursor().shape() == Qt::ArrowCursor, "the cursor back when Space is let go");

    widget.setZoom(100.0);
    expect(widget.zoom() == colorizing::app::FramePreviewWidget::kMaxZoom, "zoom is at most 32");
    widget.resetZoom();
    expect(widget.zoom() == 1.0 && shownPixels(CanvasRegion::ColorizedFrame) == whole,
           "back to the whole frame");
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

// Issue #29: Pick picks once and gives the tool before it back; the click's release then
// finishes no stroke of that tool.
void testPickReturnsToTheTool()
{
    using colorizing::edit::DrawingTool;
    colorizing::app::PickToolReturn pick;
    pick.toolChosen(DrawingTool::Point, DrawingTool::Rectangle);
    pick.toolChosen(DrawingTool::Rectangle, DrawingTool::ColorPicker);
    expect(pick.afterPick(DrawingTool::ColorPicker) == DrawingTool::Rectangle,
           "after a pick the rectangle is back");
    expect(!pick.afterPick(DrawingTool::Rectangle), "a pick with another tool changes nothing");
    pick.toolChosen(DrawingTool::ColorPicker, DrawingTool::ColorPicker);
    expect(pick.afterPick(DrawingTool::ColorPicker) == DrawingTool::Rectangle,
           "choosing Pick again keeps the tool to go back to");
    pick.toolChosen(DrawingTool::ColorPicker, DrawingTool::Line);
    pick.toolChosen(DrawingTool::Line, DrawingTool::Line);
    expect(!pick.afterPick(DrawingTool::ColorPicker), "another tool chosen forgets it");

    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);
    int clicks = 0;
    int strokes = 0;
    widget.setPixelClickedHandler([&](colorizing::render::CanvasHit, bool) {
        ++clicks;
        widget.cancelStroke();
    });
    widget.setStrokeFinishedHandler(
        [&](colorizing::render::CanvasHit, colorizing::render::CanvasHit, bool) { ++strokes; });
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(90, 90), QPointF(90, 90),
                      QPointF(90, 90), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(90, 90), QPointF(90, 90),
                        QPointF(90, 90), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
    expect(clicks == 1 && strokes == 0, "the click's release finishes no stroke");
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

    // A drag may end outside the frame (a circle larger than it, as for gradients): the end is
    // the pixel there, past the frame's edge, on the same plane.
    QMouseEvent outsidePress(QEvent::MouseButtonPress, QPointF(90, 90), QPointF(90, 90),
                             QPointF(90, 90), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &outsidePress);
    QMouseEvent outsideRelease(QEvent::MouseButtonRelease, QPointF(2000, 1500),
                               QPointF(2000, 1500), QPointF(2000, 1500), Qt::LeftButton,
                               Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &outsideRelease);
    const colorizing::render::Size plane =
        document.geometry().planeSize(strokeStart->resolution);
    expect(strokeEnd && strokeEnd->resolution == strokeStart->resolution &&
               strokeEnd->x >= plane.width && strokeEnd->y >= plane.height &&
               strokeEnd->x <= 4 * plane.width && strokeEnd->y <= 4 * plane.height,
           "a drag ending outside the frame ends past its edge, a few frame sizes at most");
}

void testPositionHandler()
{
    colorizing::document::ProjectDocument document = makePreviewDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    widget.setFrameIndex(0);
    std::optional<colorizing::render::CanvasHit> reported;
    int reports = 0;
    widget.setPositionHandler([&](std::optional<colorizing::render::CanvasHit> hit,
                                  std::optional<colorizing::render::CanvasHit>) {
        reported = hit;
        ++reports;
    });
    expect(widget.hasMouseTracking(), "the canvas follows the mouse to show its position");
    // A point on the colorized frame, wherever the layout puts it.
    QPoint point(-1, -1);
    for (int y = 0; y < widget.height() && point.x() < 0; y += 4) {
        for (int x = 0; x < widget.width() && point.x() < 0; x += 4) {
            const auto hit = widget.hitTestAt(QPoint(x, y));
            if (hit && hit->region == colorizing::render::CanvasRegion::ColorizedFrame) {
                point = QPoint(x, y);
            }
        }
    }
    QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(point), QPointF(point),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);
    const auto expected = widget.hitTestAt(point);
    expect(reports == 1 && reported && expected && reported->x == expected->x &&
               reported->y == expected->y &&
               reported->region == colorizing::render::CanvasRegion::ColorizedFrame,
           "a move reports the pixel under the mouse");
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&widget, &leave);
    expect(reports == 2 && !reported, "leaving the canvas reports none");
}

void testDotPixelsHaveNoOutline()
{
    // A painter on a widget in a dark theme starts with a light pen; a dark dot must stay dark.
    QImage image(40, 40, QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setPen(QPen(Qt::white, 2));
    colorizing::app::DmdRenderOptions options;
    options.pixelShape = colorizing::app::DmdPixelShape::Dot;
    options.pixelCoverage = 0.8;
    colorizing::app::paintDmdPixel(painter, 0, 0, 40, QColor(10, 10, 10), options);
    painter.end();
    bool light = false;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            light = light || QColor(image.pixel(x, y)).lightness() > 60;
        }
    }
    expect(!light, "a dot pixel is drawn without an outline in the painter's pen");
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

    // A press and a release point inside the original frame, wherever the platform's scroll
    // bars (whose room the layout keeps) put it.
    const auto onOriginal = [&widget](QPoint point) {
        const std::optional<colorizing::render::CanvasHit> hit = widget.hitTestAt(point);
        return hit && hit->region == colorizing::render::CanvasRegion::OriginalFrame;
    };
    QPointF pressPoint;
    for (int y = widget.height() / 2; y < widget.height() && pressPoint.isNull(); y += 4) {
        for (int x = 0; x < widget.width() && pressPoint.isNull(); x += 4) {
            if (onOriginal(QPoint(x, y)) && onOriginal(QPoint(x + 30, y + 10))) {
                pressPoint = QPointF(x, y);
            }
        }
    }
    const QPointF releasePoint = pressPoint + QPointF(30, 10);
    const std::optional<colorizing::render::CanvasHit> originalHit =
        widget.hitTestAt(pressPoint.toPoint());
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
                      pressPoint,
                      pressPoint,
                      pressPoint,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::ShiftModifier);
    QApplication::sendEvent(&widget, &press);

    QMouseEvent release(QEvent::MouseButtonRelease,
                        releasePoint,
                        releasePoint,
                        releasePoint,
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

    expect(hasVisibleMagentaPixel(image),
           "comparison mask overlay should render visibly, in magenta");
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
    // Only the colorized picture: the original below shows the SD zones.
    const auto only = [&widget](const QImage& image, colorizing::render::CanvasRegion region) {
        QImage part(image.size(), QImage::Format_ARGB32);
        part.fill(Qt::transparent);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const std::optional<colorizing::render::CanvasHit> hit =
                    widget.hitTestAt(QPoint(x, y));
                if (hit && hit->region == region) {
                    part.setPixel(x, y, image.pixel(x, y));
                }
            }
        }
        return part;
    };
    expect(!hasVisibleCyanPixel(only(hdImage, colorizing::render::CanvasRegion::ColorizedFrame)),
           "no dynamic mask overlay over the HD frame of an ordinary project: the zones are "
           "those of the SD plane, and the stored HD mask is not shown");
    expect(hasVisibleCyanPixel(only(hdImage, colorizing::render::CanvasRegion::OriginalFrame)),
           "the original below shows the SD zones");

    // The selected set is outlined in yellow, the others stay cyan.
    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    widget.setSelectedDynamicSet(0);
    QImage selectedImage(widget.size(), QImage::Format_ARGB32);
    selectedImage.fill(Qt::transparent);
    QPainter selectedPainter(&selectedImage);
    widget.render(&selectedPainter);
    selectedPainter.end();
    expect(hasVisibleYellowPixel(selectedImage), "the selected set is outlined in yellow");
    widget.setSelectedDynamicSet(5);
    QImage otherImage(widget.size(), QImage::Format_ARGB32);
    otherImage.fill(Qt::transparent);
    QPainter otherPainter(&otherImage);
    widget.render(&otherPainter);
    otherPainter.end();
    expect(!hasVisibleYellowPixel(otherImage) && hasVisibleCyanPixel(otherImage),
           "a set the frame does not use leaves the others cyan");
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
    expect(hasVisibleRedPixel(hdImage) && !hasVisibleGreenPixel(hdImage),
           "the HD picture of an ordinary project takes its dynamic colors from the SD set, "
           "not from the stored HD colors");
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

// The background's own HD image decides the plane while it is edited, not the frame's.
void testBackgroundHdEditableOnFrameWithoutHd()
{
    colorizing::document::ProjectDocument document = makeBackgroundPreviewDocument();
    if (document.hasHdFrame(0)) {
        document.removeHdFrame(0);
    }
    expect(!document.hasHdFrame(0) && document.background(0).colorizedHd.has_value(),
           "fixture: a frame without HD, a background with HD");
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);
    widget.setSelectedBackgroundIndex(0);
    widget.setActiveResolution(colorizing::render::Resolution::Hd);
    const auto planeAtTop = [&]() {
        for (int y = 0; y < widget.height() / 2; y += 4) {
            const auto hit = widget.hitTestAt(QPoint(widget.width() / 2, y));
            if (hit && hit->region == colorizing::render::CanvasRegion::ColorizedFrame) {
                return std::optional<colorizing::render::Resolution>(hit->resolution);
            }
        }
        return std::optional<colorizing::render::Resolution>();
    };
    expect(planeAtTop() == colorizing::render::Resolution::Sd,
           "the frame without HD is shown in SD");
    widget.setBackgroundImageEditModeEnabled(true);
    expect(planeAtTop() == colorizing::render::Resolution::Hd,
           "its background's HD image is shown and edited in HD");
    document.removeBackgroundHdFrame(0);
    widget.update();
    expect(planeAtTop() == colorizing::render::Resolution::Sd,
           "a background without HD is edited in SD");
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
    const colorizing::document::Rgb565Frame withoutSprites =
        colorizing::app::composeResolvedFrame(document, 0, colorizing::render::Resolution::Sd,
                                              false, nullptr, false);
    expect(withoutSprites.pixels[5 * colorizing::render::kSdFrameSize.width + 10] != 0xf800,
           "Show Sprites off: the frame without its sprites");
    const std::vector<uint8_t> covered =
        colorizing::app::spriteCoverage(document, 0, colorizing::render::Resolution::Sd);
    expect(covered.size() == 128u * 32u &&
               covered[5 * colorizing::render::kSdFrameSize.width + 10] == 1 &&
               covered[0] == 0,
           "the coverage has the pixels the found sprite paints, and only those");
    {
        // Outline Sprites: a green border around them on the colorized frame.
        colorizing::app::FramePreviewWidget widget;
        widget.resize(640, 420);
        widget.setDocument(&document);
        widget.setFrameIndex(0);
        const auto greenOutline = [&widget]() {
            QImage image(widget.size(), QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            widget.render(&painter);
            painter.end();
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor color = image.pixelColor(x, y);
                    if (color.red() == 60 && color.green() == 230 && color.blue() == 60) {
                        return true;
                    }
                }
            }
            return false;
        };
        expect(!greenOutline(), "no outline unless asked for");
        widget.setSpriteOutlinesEnabled(true);
        expect(greenOutline(), "the found sprite is outlined in green");
    }

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

    // The original frame's half: the frame (shade 0, dark brown) normally, black while a
    // background is edited. Sampled at the middle of an original pixel.
    widget.setActiveResolution(colorizing::render::Resolution::Sd);
    QPoint inOriginal(-1, -1);
    for (int y = widget.height() / 2; y < widget.height() && inOriginal.x() < 0; y += 2) {
        for (int x = 0; x < widget.width() && inOriginal.x() < 0; x += 2) {
            const auto hit = widget.hitTestAt(QPoint(x, y));
            if (hit && hit->region == colorizing::render::CanvasRegion::OriginalFrame &&
                hit->x == 10 && hit->y == 10) {
                inOriginal = QPoint(x, y);
            }
        }
    }
    // The middle of that original pixel: the run of points that hit it.
    int right = inOriginal.x();
    while (right + 1 < widget.width()) {
        const auto hit = widget.hitTestAt(QPoint(right + 1, inOriginal.y()));
        if (!hit || hit->x != 10) {
            break;
        }
        ++right;
    }
    int bottom = inOriginal.y();
    while (bottom + 1 < widget.height()) {
        const auto hit = widget.hitTestAt(QPoint(inOriginal.x(), bottom + 1));
        if (!hit || hit->y != 10) {
            break;
        }
        ++bottom;
    }
    const QPoint middle((inOriginal.x() + right) / 2, (inOriginal.y() + bottom) / 2);
    const auto colorAt = [&widget](QPoint point) {
        QImage image(widget.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        widget.render(&painter);
        painter.end();
        return QColor(image.pixel(point));
    };
    expect(inOriginal.x() >= 0 && colorAt(middle) == QColor(0, 0, 0),
           "while a background is edited, the original frame is not shown");
    widget.setBackgroundImageEditModeEnabled(false);
    expect(colorAt(middle) != QColor(0, 0, 0), "otherwise the original frame is shown");
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

// The sprite editor zooms (issue #14): around a point the sprite pixel there stays, the view
// menu's entries and Ctrl+wheel zoom, and the zoom goes back to the fitted size.
void testSpriteEditorZoom()
{
    colorizing::document::ProjectDocument document = makeSpriteEditorDocument();
    colorizing::app::SpriteEditorWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setSpriteIndex(std::size_t{0});
    std::optional<std::pair<int, int>> painted;
    widget.setColorizedPixelEditedHandler(
        [&](std::size_t, colorizing::render::Resolution, int x, int y, bool) {
            painted = std::make_pair(x, y);
        });
    const auto pixelAt = [&](QPointF at) {
        painted.reset();
        QMouseEvent press(QEvent::MouseButtonPress, at, at, at, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(&widget, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, at, at, at, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &release);
        return painted;
    };
    // In the middle of the colorized sprite, the upper half: away from its edges, which the
    // zoom keeps in view.
    const QPointF at(320, 100);
    const QPointF right(360, 100);
    const auto before = pixelAt(at);
    const auto beforeRight = pixelAt(right);
    expect(before && beforeRight && widget.zoom() == 1.0, "the fitted sprite");
    widget.setZoom(3.0, at.toPoint());
    const auto after = pixelAt(at);
    const auto afterRight = pixelAt(right);
    expect(after && afterRight && after == before,
           "zoomed around a point, the sprite pixel there stays");
    expect(afterRight->first - after->first < beforeRight->first - before->first,
           "40 widget pixels to the right are fewer sprite pixels now");
    widget.resetZoom();
    expect(widget.zoom() == 1.0 && pixelAt(right) == beforeRight, "back to the fitted size");
    QWheelEvent wheel(at, widget.mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &wheel);
    expect(widget.zoom() > 1.0, "Ctrl+wheel zooms in");
    widget.zoomOut();
    widget.zoomOut();
    expect(widget.zoom() == 1.0, "and zooming out stops at the fitted size");

    // The zoom reports its changes, for the zoom display; Space and a drag move the sprite.
    int zoomChanges = 0;
    widget.onZoomChanged = [&zoomChanges]() { ++zoomChanges; };
    widget.setZoom(3.0, at.toPoint());
    expect(zoomChanges == 1, "a zoom change is reported");
    const auto zoomedAt = pixelAt(at);
    widget.setPanMode(true);
    const QPointF to = at + QPointF(-60.0, 0.0);
    QMouseEvent panPress(QEvent::MouseButtonPress, at, at, widget.mapToGlobal(at), Qt::LeftButton,
                         Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &panPress);
    QMouseEvent panMove(QEvent::MouseMove, to, to, widget.mapToGlobal(to), Qt::NoButton,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &panMove);
    QMouseEvent panRelease(QEvent::MouseButtonRelease, to, to, widget.mapToGlobal(to),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &panRelease);
    widget.setPanMode(false);
    const auto pannedAt = pixelAt(at);
    expect(zoomedAt && pannedAt && pannedAt->first > zoomedAt->first,
           "dragging to the left with Space shows more of the right");
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
                           widget.pixelCentre(false, {10, 5}),
                           widget.pixelCentre(false, {10, 5}),
                           widget.pixelCentre(false, {10, 5}),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier,
                           QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &paintPress);
    QMouseEvent paintRelease(QEvent::MouseButtonRelease,
                             widget.pixelCentre(false, {10, 5}),
                             widget.pixelCentre(false, {10, 5}),
                             widget.pixelCentre(false, {10, 5}),
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
                           widget.pixelCentre(false, {11, 6}),
                           widget.pixelCentre(false, {11, 6}),
                           widget.pixelCentre(false, {11, 6}),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::ShiftModifier,
                           QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &erasePress);
    QMouseEvent eraseRelease(QEvent::MouseButtonRelease,
                             widget.pixelCentre(false, {11, 6}),
                             widget.pixelCentre(false, {11, 6}),
                             widget.pixelCentre(false, {11, 6}),
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
                          widget.pixelCentre(false, {10, 5}),
                          widget.pixelCentre(false, {10, 5}),
                          widget.pixelCentre(false, {10, 5}),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier,
                          QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &linePress);
    QMouseEvent lineMove(QEvent::MouseMove,
                         widget.pixelCentre(false, {20, 5}),
                         widget.pixelCentre(false, {20, 5}),
                         widget.pixelCentre(false, {20, 5}),
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
                            widget.pixelCentre(false, {20, 5}),
                            widget.pixelCentre(false, {20, 5}),
                            widget.pixelCentre(false, {20, 5}),
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
                          widget.pixelCentre(false, {10, 5}),
                          widget.pixelCentre(false, {10, 5}),
                          widget.pixelCentre(false, {10, 5}),
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
                           widget.pixelCentre(false, {10, 5}),
                           widget.pixelCentre(false, {10, 5}),
                           widget.pixelCentre(false, {10, 5}),
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
                      widget.pixelCentre(true, {10, 5}),
                      widget.pixelCentre(true, {10, 5}),
                      widget.pixelCentre(true, {10, 5}),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &press);
    QMouseEvent move(QEvent::MouseMove,
                     widget.pixelCentre(true, {20, 15}),
                     widget.pixelCentre(true, {20, 15}),
                     widget.pixelCentre(true, {20, 15}),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier,
                     QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&widget, &move);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        widget.pixelCentre(true, {20, 15}),
                        widget.pixelCentre(true, {20, 15}),
                        widget.pixelCentre(true, {20, 15}),
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

    // The top left quarter of SD pixel 10,5 is HD pixel 20,10.
    const QPointF centre = widget.pixelCentre(false, {10, 5});
    const QPointF half = (widget.pixelCentre(false, {11, 6}) - centre) / 2.0;
    const QPointF corner = centre - half;
    const QColor sdFootprintPixel(image.pixel(static_cast<int>(corner.x()), static_cast<int>(corner.y())));
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
                          corner,
                          corner,
                          corner,
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

// True when the Tab key reaches the widgets of a page in the given order.
bool tabVisitsInOrder(QWidget& page, const std::vector<QWidget*>& widgets)
{
    QWidget* current = page.nextInFocusChain();
    for (QWidget* widget : widgets) {
        while (current != widget) {
            if (current == &page) {
                return false;
            }
            current = current->nextInFocusChain();
        }
    }
    return true;
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

// How many of the frame's pixels a slot moves: exactly its colors, a color listed twice
// belonging to the first slot; the section says it.
void testRotationSlotUse()
{
    using colorizing::render::Resolution;
    colorizing::document::ProjectDocument document = makePreviewDocument();
    // Frame 0 has two colored pixels, 0xf800 and 0x07e0.
    document.setColorRotationLength(0, Resolution::Sd, 0, 2);
    document.setColorRotationColor(0, Resolution::Sd, 0, 0, 0xf800);
    document.setColorRotationColor(0, Resolution::Sd, 0, 1, 0x001f);
    document.setColorRotationLength(0, Resolution::Sd, 1, 2);
    document.setColorRotationColor(0, Resolution::Sd, 1, 0, 0x07e0);
    document.setColorRotationColor(0, Resolution::Sd, 1, 1, 0xf800);
    const auto first = colorizing::app::rotationSlotUse(document, 0, Resolution::Sd, 0);
    const auto second = colorizing::app::rotationSlotUse(document, 0, Resolution::Sd, 1);
    const auto unused = colorizing::app::rotationSlotUse(document, 0, Resolution::Sd, 2);
    expect(first.rotating == 1 && first.colored == 2, "slot 0 moves the 0xf800 pixel");
    expect(second.rotating == 1 && second.colored == 2,
           "slot 1 moves the 0x07e0 pixel; 0xf800 belongs to slot 0");
    expect(unused.rotating == 0 && unused.colored == 0, "a slot without colors moves nothing");

    colorizing::app::ColorRotationPanel panel;
    QLabel* useLabel = nullptr;
    for (QLabel* label : panel.findChildren<QLabel*>()) {
        if (label->wordWrap()) {
            useLabel = label;
        }
    }
    expect(useLabel != nullptr, "the rotation section has a line about the slot's use");
    panel.refresh(&document, 0, Resolution::Sd);
    expect(!useLabel->isHidden() &&
               useLabel->text() ==
                   QStringLiteral("1 of the frame's 2 colored pixels have a color of this slot "
                                  "and rotate."),
           "it counts the pixels the slot moves");
    document.setColorRotationColor(0, Resolution::Sd, 0, 0, 0x1234);
    panel.refresh(&document, 0, Resolution::Sd);
    expect(useLabel->text().startsWith(QStringLiteral("No pixel of the frame has a color")),
           "and says when it moves none");
    document.setColorRotationLength(0, Resolution::Sd, 0, 0);
    panel.refresh(&document, 0, Resolution::Sd);
    expect(useLabel->isHidden(), "nothing to say about an unused slot");
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
    expect(tabVisitsInOrder(panel,
                            {slotComboBox, lengthSpinBox, delaySpinBox, gridButtons.first(),
                             gridButtons.last(), clearButton}),
           "rotation panel should tab through its controls as laid out");

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
        childByToolTip<QSpinBox>(
        panel, "First palette color of a gradient; Ctrl+drag across the colors below sets both "
               "(Shift reverses them)");
    auto* endSpinBox =
        childByToolTip<QSpinBox>(
        panel, "Last palette color of a gradient; Ctrl+drag across the colors below sets both "
               "(Shift reverses them)");
    auto* shapeComboBox = childByToolTip<QComboBox>(panel, "Shape of the Grad and Sel tools");
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
    expect(tabVisitsInOrder(panel,
                            {paletteComboBox, nameLineEdit, startSpinBox, endSpinBox,
                             shapeComboBox, patternComboBox, directionComboBox,
                             gridButtons.first(), gridButtons.last(), applyButton, fillButton,
                             directionButton, clearButton}),
           "palette panel should tab through its controls as laid out");

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
            frame.spriteReferences.assign(colorizing::document::kLegacySpritesPerFrame,
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

// More than 32 sprites on a frame: the Frame Sprites section warns.
void testSpritePanelWarnsAboutManySprites()
{
    using colorizing::document::FrameSpriteReference;
    colorizing::document::ProjectDocument document = makeSpritePanelDocument();
    colorizing::app::SpritePanel panel;
    colorizing::app::SpritePanelState state;
    state.document = &document;
    state.frameIndex = 1;
    for (std::size_t slot = 0; slot < 32; ++slot) {
        document.setFrameSpriteReference(1, slot, FrameSpriteReference{0, 0, 0, 127, 31});
    }
    panel.refresh(state);
    expect(panel.spriteCountWarning().isEmpty(), "32 sprites on a frame need no warning");
    document.setFrameSpriteReference(1, 32, FrameSpriteReference{1, 0, 0, 127, 31});
    panel.refresh(state);
    expect(panel.spriteCountWarning().contains(QStringLiteral("33 sprites")),
           "33 sprites on a frame are warned about: " +
               panel.spriteCountWarning().toStdString());
    state.frameIndex = 0;
    panel.refresh(state);
    expect(panel.spriteCountWarning().isEmpty(), "another frame with two sprites has none");
}

// The sprite editor's page has Source Frame too: it leaves the editor, as Back to Frame, and
// goes to the frame the sprite was taken from.
void testSpriteEditorSourceFrame()
{
    colorizing::document::ProjectDocument document = makeSpritePanelDocument();
    document.setSpriteSourceFrame(0, 2);
    colorizing::app::SpritePanel panel;
    colorizing::app::SpritePanelState state;
    state.document = &document;
    state.frameIndex = 0;
    state.spriteIndex = 0;
    std::vector<std::string> calls;
    panel.onSpriteCanvasToggled = [&](bool checked) {
        calls.push_back(checked ? "edit" : "leave");
    };
    panel.onSourceFrameRequested = [&]() { calls.push_back("source"); };
    panel.refresh(state);
    QToolButton* editorButton = nullptr;
    for (QToolButton* button : panel.recordPage()->findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Source Frame")) {
            editorButton = button;
        }
    }
    expect(editorButton && editorButton->isEnabled() &&
               editorButton->toolTip() ==
                   QStringLiteral("Go to frame 2, the frame the sprite was taken from"),
           "the sprite editor's page has Source Frame, naming the frame");
    panel.setSpriteCanvasChecked(true);
    editorButton->click();
    expect(calls == std::vector<std::string>{"leave", "source"},
           "it leaves the sprite editor first, then goes to the source frame");
    document.setSpriteSourceFrame(0, 7);
    panel.refresh(state);
    expect(!editorButton->isEnabled(), "a source frame the project lacks: disabled");
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
    auto* removeButton = childByToolTip<QToolButton>(
        panel,
        "Remove the selected slot's sprite from the selected frames; the slots behind it move "
        "up");
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
    {
        // The frame's sprites (up to 16 lines) scroll in a few lines' height.
        QWidget* viewport = frameSpritesLabel->parentWidget();
        auto* area = viewport ? qobject_cast<QScrollArea*>(viewport->parentWidget()) : nullptr;
        expect(area && area->maximumHeight() < 8 * frameSpritesLabel->fontMetrics().lineSpacing(),
               "the frame's sprite list scrolls in a short area");
    }
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
    // Two pages: the frame's slots (frame canvas) and the sprite itself (sprite editor).
    expect(slotComboBox->parentWidget() == panel.framePage() &&
               spriteCanvasButton->parentWidget() == panel.framePage() &&
               boundsOverlayButton->parentWidget() == panel.framePage() &&
               nameLineEdit->parentWidget() == panel.recordPage() &&
               detectionAreasButton->parentWidget() == panel.recordPage() &&
               applyButton->parentWidget() == panel.recordPage() &&
               areaComboBox->parentWidget() == panel.recordPage(),
           "sprite panel should split the frame's slots from the sprite itself");
    expect(tabVisitsInOrder(panel,
                            {slotComboBox, assignButton, captureButton, removeButton,
                             captureSelectionButton, clearCaptureButton, boundsOverlayButton,
                             minXSpinBox, minYSpinBox, maxXSpinBox, maxYSpinBox,
                             spriteCanvasButton}) &&
               tabVisitsInOrder(panel,
                                {nameLineEdit, detectionAreasButton, areaComboBox,
                                 clearAreaButton, areaXSpinBox, areaYSpinBox, areaWidthSpinBox,
                                 areaHeightSpinBox, applyButton}),
           "sprite panel should tab through each page as laid out");
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
    expect(panel.findChildren<QLabel*>().size() == 12,
           "sprite panel should hold only its three text labels (the selection on each page), "
           "the sprite count warning and eight field captions");

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
                   static_cast<int>(colorizing::document::kSpritesPerFrame) &&
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
               !clearAreaButton->isEnabled() && areaComboBox->isEnabled() &&
               !areaXSpinBox->isEnabled() && !areaYSpinBox->isEnabled() &&
               !areaWidthSpinBox->isEnabled() && !areaHeightSpinBox->isEnabled() &&
               shownArea.x == 0 && shownArea.y == 0 && shownArea.width == 1 &&
               shownArea.height == 1,
           "sprite panel should lock the values of a detection area that is not set");

    panel.refresh(stateFor(1, 0));
    shownBounds = panel.slotBounds();
    expect(frameSpritesLabel->text() == QStringLiteral("Current frame uses no sprites") &&
               slotComboBox->itemText(0) == QStringLiteral("Slot 0: Empty") &&
               assignButton->isEnabled() && !removeButton->isEnabled() &&
               boundsOverlayButton->isEnabled() && !minXSpinBox->isEnabled() &&
               !minYSpinBox->isEnabled() && !maxXSpinBox->isEnabled() &&
               !maxYSpinBox->isEnabled() && shownBounds.maxX == 0 && shownBounds.maxY == 0,
           "sprite panel should lock the bounds of an empty slot");

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
    // Issue #38: with several sprites selected, Assign puts each on the frames, no slot needed.
    SpritePanelState severalSprites = stateFor(2, 0);
    severalSprites.selectedSpriteCount = 2;
    panel.refresh(severalSprites);
    expect(assignButton->isEnabled() && !captureButton->isEnabled() &&
               assignButton->toolTip().startsWith(QStringLiteral("Put each selected sprite")),
           "several sprites can be assigned to a frame without slots");
    panel.refresh(stateFor(2, 0));
    expect(!assignButton->isEnabled() &&
               assignButton->toolTip() == QStringLiteral("Assign selected sprite to selected slot"),
           "one sprite again needs a slot");

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
               minXSpinBox->isEnabled(),
           "sprite panel should keep only the slot controls without a sprite");
    panel.refresh(stateFor(0, 7));
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               !assignButton->isEnabled(),
           "sprite panel should treat an unknown sprite as no sprite");
    expect(callbacks == 0, "refreshing the sprite panel should not notify");
    {
        auto* outlines = childByToolTip<QToolButton>(
            panel, "Outline the pixels of the sprites libserum finds on the frame in green");
        std::optional<bool> toggled;
        panel.onOutlinesToggled = [&](bool checked) { toggled = checked; };
        expect(outlines->parentWidget() == panel.framePage() && outlines->isCheckable() &&
                   outlines->isChecked() && outlines->isEnabled() && panel.isOutlinesChecked(),
               "Outline Sprites is on the frame page, on at first");
        outlines->click();
        expect(toggled == false && !panel.isOutlinesChecked(), "turning it off is reported");
        outlines->click();
        panel.onOutlinesToggled = nullptr;
    }
    panel.setSpriteCanvasChecked(true);
    childByToolTip<QToolButton>(panel, "Leave the sprite editor and show the frame again")
        ->click();
    expect(!spriteCanvasButton->isChecked() && callbacks == 1,
           "Back to Frame leaves the sprite editor, as Edit Sprite clicked up");
    callbacks = 0;

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
               !areaXSpinBox->isEnabled() && shownArea.x == 0 && shownArea.width == 1,
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
    panel.setCaptureSelectionChecked(false);
    expect(!panel.isCaptureSelectionChecked() && captureSelection == true &&
               callbacks == callbacksBeforeSetters,
           "leaving the capture selection from the owner should not notify");
    panel.setCaptureSelectionChecked(true);
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

    maxXSpinBox->setValue(30);
    expect(bounds.has_value() && bounds->minX == 10 && bounds->minY == 5 &&
               bounds->maxX == 30 && bounds->maxY == 20,
           "editing the slot bounds should report them");
    bounds.reset();
    minYSpinBox->setValue(9);
    expect(bounds.has_value() && bounds->minY == 9, "each slot bound should notify");
    bounds.reset();
    maxXSpinBox->setValue(3);
    expect(bounds.has_value() && bounds->minX == 10 && bounds->maxX == 10 &&
               maxXSpinBox->value() == 10,
           "a maximum should stop at its minimum instead of passing it");
    bounds.reset();
    minYSpinBox->setValue(31);
    expect(bounds.has_value() && bounds->minY == 20 && bounds->maxY == 20 &&
               minYSpinBox->value() == 20,
           "a minimum should stop at its maximum instead of passing it");
    areaWidthSpinBox->setValue(250);
    expect(area.has_value() && area->x == 4 && area->y == 5 && area->width == 250 &&
               area->height == 8,
           "editing the detection area should report it as entered");
    area.reset();
    areaYSpinBox->setValue(60);
    expect(area.has_value() && area->y == 60, "each detection area value should notify");

    expect(colorizing::app::isSpriteDetectionAreaSet(document.sprite(0).detectionAreas[0]) &&
               !colorizing::app::isSpriteDetectionAreaSet(document.sprite(0).detectionAreas[1]) &&
               !colorizing::app::isSpriteDetectionAreaSet({3, 4, 0, 5}),
           "only a detection area with a position and a size should count as set");

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

// Frame 0 has HD, mask 1, dynamic data and background 0; frame 1 has a shape mode, unassigned
// background data and the switch-to-monochrome trigger; frame 2 carries values the editor
// cannot represent; frame 3 has neither dynamic nor background data.
colorizing::document::ProjectDocument makeInspectorPanelDocument(uint32_t colorCount = 16)
{
    using colorizing::document::kMaxDynamicSetsPerFrame;
    using colorizing::render::kHdFrameSize;
    using colorizing::render::kSdFrameSize;
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.colorCount = colorCount;
    snapshot.romName = "inspector_panels";
    for (int maskIndex = 0; maskIndex < 3; ++maskIndex) {
        snapshot.comparisonMasks.push_back(
            colorizing::document::makeIndexedFrame(kSdFrameSize, 0));
    }
    snapshot.backgrounds.push_back(colorizing::document::BackgroundRecord{
        colorizing::document::makeRgb565Frame(kSdFrameSize, 0x07e0),
        colorizing::document::makeRgb565Frame(kHdFrameSize, 0x001f),
    });
    snapshot.backgrounds.push_back(colorizing::document::BackgroundRecord{
        colorizing::document::makeRgb565Frame(kSdFrameSize, 0xf800),
    });
    for (int frameIndex = 0; frameIndex < 4; ++frameIndex) {
        snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
            colorizing::document::makeIndexedFrame(kSdFrameSize, 0),
            colorizing::document::makeRgb565Frame(kSdFrameSize, 0),
            std::nullopt,
            0,
            255,
        });
    }
    const auto makeBackgroundData = [](uint16_t backgroundId) {
        return colorizing::document::FrameBackgroundData{
            backgroundId,
            colorizing::document::makeIndexedFrame(kSdFrameSize, 0),
            colorizing::document::makeIndexedFrame(kHdFrameSize, 0),
        };
    };
    colorizing::document::FrameDynamicData dynamicData{
        colorizing::document::makeIndexedFrame(kSdFrameSize, 255),
        colorizing::document::makeIndexedFrame(kHdFrameSize, 255),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * colorCount, 0),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * colorCount, 0),
    };
    dynamicData.colorsSd[1] = 0xf800;
    dynamicData.colorsSd[2 * colorCount] = 0x07e0;
    dynamicData.colorsHd[1] = 0x001f;
    snapshot.frames[2].dynamicData = dynamicData;
    dynamicData.shadowDirectionsSd.assign(kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsSd.assign(kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowDirectionsHd.assign(kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsHd.assign(kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowDirectionsSd[0] = 0x81;
    dynamicData.shadowColorsSd[0] = 0x07e0;
    dynamicData.shadowDirectionsSd[2] = 0x10;
    dynamicData.shadowDirectionsHd[0] = 0x08;
    dynamicData.shadowColorsHd[0] = 0xffff;

    snapshot.frames[0].colorizedHd = colorizing::document::makeRgb565Frame(kHdFrameSize, 0);
    snapshot.frames[0].comparisonMaskId = 1;
    snapshot.frames[0].dynamicData = dynamicData;
    snapshot.frames[0].backgroundData = makeBackgroundData(0);
    snapshot.frames[0].triggerId = 5;
    snapshot.frames[0].durationMs = 100;
    snapshot.frames[1].shapeComparisonMode = 1;
    snapshot.frames[1].backgroundData = makeBackgroundData(0xffff);
    snapshot.frames[1].triggerId = 65432;
    snapshot.frames[1].durationMs = 40000;
    snapshot.frames[2].comparisonMaskId = 9;
    snapshot.frames[2].shapeComparisonMode = 3;
    snapshot.frames[2].backgroundData = makeBackgroundData(9);
    snapshot.frames[2].triggerId = 70000;
    colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

void testMaskPanel()
{
    const colorizing::document::ProjectDocument document = makeInspectorPanelDocument();
    const colorizing::document::ProjectDocument noMasks = makePreviewDocument();

    colorizing::app::MaskPanel panel;
    int callbacks = 0;
    std::optional<int> editedMaskId;
    std::optional<int> editedShapeMode;
    int assignRequests = 0;
    int editRequests = 0;
    panel.onMaskIdEdited = [&](int maskId) {
        ++callbacks;
        editedMaskId = maskId;
    };
    panel.onShapeModeEdited = [&](int mode) {
        ++callbacks;
        editedShapeMode = mode;
    };
    panel.onAssignRequested = [&]() {
        ++callbacks;
        ++assignRequests;
    };
    panel.onEditRequested = [&]() {
        ++callbacks;
        ++editRequests;
    };

    auto* maskIdSpinBox = childByToolTip<QSpinBox>(panel, "Assigned comparison mask ID");
    auto* shapeComboBox = childByToolTip<QComboBox>(panel, "Shape comparison mode");
    auto* assignButton = childByToolTip<QToolButton>(
        panel, "Assign selected comparison mask to selected frames");
    auto* editButton =
        childByToolTip<QToolButton>(panel, "Show and edit the assigned comparison mask");
    QLabel* selectedLabel = nullptr;
    for (QLabel* label : panel.findChildren<QLabel*>()) {
        if (label->text().startsWith(QStringLiteral("Selected:"))) {
            selectedLabel = label;
        }
    }
    expect(selectedLabel != nullptr, "mask panel should show the selected mask label");
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               selectedLabel->wordWrap(),
           "mask panel should start without a selected mask");
    expect(assignButton->text() == QStringLiteral("Assign Mask") &&
               editButton->text() == QStringLiteral("Edit Mask"),
           "mask panel should keep its button texts");
    expect(!maskIdSpinBox->isEnabled() && !shapeComboBox->isEnabled() &&
               !assignButton->isEnabled() && !editButton->isEnabled(),
           "mask panel should start disabled");
    expect(maskIdSpinBox->minimum() == -1 && maskIdSpinBox->maximum() == -1 &&
               maskIdSpinBox->specialValueText() == QStringLiteral("None") &&
               panel.maskId() == -1,
           "mask panel should start without mask IDs");
    expect(shapeComboBox->count() == 0, "mask panel should start without shape modes");
    expect(tabVisitsInOrder(panel, {maskIdSpinBox, shapeComboBox, assignButton, editButton}),
           "mask panel should tab through its controls as laid out");

    panel.refreshFrame(nullptr, std::nullopt);
    expect(!maskIdSpinBox->isEnabled() && !shapeComboBox->isEnabled() &&
               shapeComboBox->count() == 2 && shapeComboBox->currentData().toInt() == 0,
           "mask panel should offer the two shape modes but stay disabled without a frame");

    panel.refreshFrame(&document, 0);
    expect(maskIdSpinBox->isEnabled() && maskIdSpinBox->maximum() == 2 && panel.maskId() == 1,
           "mask panel should show the assigned mask ID");
    expect(shapeComboBox->isEnabled() && shapeComboBox->count() == 2 &&
               shapeComboBox->currentText() == QStringLiteral("Full comparison"),
           "mask panel should show full comparison");
    panel.refreshFrame(&document, 1);
    expect(panel.maskId() == -1 && shapeComboBox->currentText() == QStringLiteral("Shape mode") &&
               shapeComboBox->currentData().toInt() == 1,
           "mask panel should show no mask and the shape mode");
    panel.refreshFrame(&document, 2);
    expect(panel.maskId() == -1, "mask panel should show an out-of-range mask ID as none");
    expect(shapeComboBox->count() == 3 &&
               shapeComboBox->currentText() == QStringLiteral("Unknown legacy mode 3") &&
               shapeComboBox->currentData().toInt() == 3,
           "mask panel should keep an unknown legacy shape mode visible");
    panel.refreshFrame(&noMasks, 0);
    expect(!maskIdSpinBox->isEnabled() && maskIdSpinBox->maximum() == -1 &&
               shapeComboBox->isEnabled() && shapeComboBox->count() == 2,
           "mask panel should disable the mask ID when the project has no masks");
    panel.refreshFrame(&document, std::nullopt);
    expect(!maskIdSpinBox->isEnabled() && maskIdSpinBox->maximum() == -1 &&
               !shapeComboBox->isEnabled(),
           "mask panel should reset without a frame");

    panel.refreshSelection(&document, 0, 1, true);
    expect(selectedLabel->text() == QStringLiteral("Selected: 1 (assigned)") &&
               assignButton->isEnabled() && editButton->isEnabled(),
           "mask panel should mark the selected mask as assigned");
    panel.refreshSelection(&document, 0, 2, true);
    expect(selectedLabel->text() == QStringLiteral("Selected: 2") && assignButton->isEnabled() &&
               editButton->isEnabled(),
           "mask panel should show another selected mask");
    panel.refreshSelection(&document, 0, 2, false);
    expect(!assignButton->isEnabled() && editButton->isEnabled(),
           "mask panel should not offer Assign without selected frames");
    panel.refreshSelection(&document, 0, std::nullopt, true);
    expect(selectedLabel->text() == QStringLiteral("Selected: 1 (assigned)") &&
               !assignButton->isEnabled() && editButton->isEnabled(),
           "mask panel should fall back to the assigned mask");
    panel.refreshSelection(&document, 0, 7, true);
    expect(selectedLabel->text() == QStringLiteral("Selected: 1 (assigned)") &&
               !assignButton->isEnabled(),
           "mask panel should ignore a selected mask that does not exist");
    panel.refreshSelection(&document, 2, std::nullopt, true);
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               !assignButton->isEnabled() && !editButton->isEnabled(),
           "mask panel should not offer Edit for an out-of-range mask ID");
    panel.refreshSelection(&document, std::nullopt, 0, false);
    expect(selectedLabel->text() == QStringLiteral("Selected: 0") && !assignButton->isEnabled() &&
               !editButton->isEnabled(),
           "mask panel should show the selected mask without a frame");
    panel.refreshSelection(nullptr, std::nullopt, 0, true);
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               !assignButton->isEnabled() && !editButton->isEnabled(),
           "mask panel should show no selection without a project");
    expect(callbacks == 0, "refreshing the mask panel should not notify");

    panel.refreshFrame(&document, 0);
    panel.refreshSelection(&document, 0, 2, true);
    panel.setMaskId(0);
    expect(panel.maskId() == 0, "mask panel should take a mask ID");
    panel.setMaskId(99);
    expect(panel.maskId() == 2, "mask panel should clamp a mask ID to the offered ones");
    expect(callbacks == 0, "setting the mask ID should not notify");

    maskIdSpinBox->setValue(1);
    expect(editedMaskId == 1, "editing the mask ID should report it");
    maskIdSpinBox->setValue(-1);
    expect(editedMaskId == -1, "clearing the mask ID should report -1");
    shapeComboBox->setCurrentIndex(1);
    expect(editedShapeMode == 1, "selecting a shape mode should report its value");
    assignButton->click();
    editButton->click();
    expect(assignRequests == 1 && editRequests == 1 && callbacks == 5,
           "each mask panel action should notify exactly once");
    expect(document.comparisonMaskId(0) == 1 && document.shapeComparisonMode(0) == 0 &&
               !document.isDirty(),
           "mask panel should leave the document untouched");
}

// "Only on the SD Picture": for libserum 2.9 only, per set, reported when switched.
void testDynamicPanelOriginalOnlySets()
{
    using colorizing::render::Resolution;
    colorizing::document::ProjectDocument document = makeInspectorPanelDocument();
    colorizing::app::DynamicPanel panel;
    std::vector<bool> toggles;
    panel.onOriginalOnlyToggled = [&](bool originalOnly) { toggles.push_back(originalOnly); };
    QCheckBox* box = nullptr;
    for (QCheckBox* candidate : panel.findChildren<QCheckBox*>()) {
        if (candidate->text() == QStringLiteral("Only on the SD Picture")) {
            box = candidate;
        }
    }
    expect(box != nullptr, "the dynamic panel has Only on the SD Picture");
    document.setOriginalOnlyDynamicSets(0, 1u << 2);
    panel.setDynamicSet(2);
    panel.refreshShadow(&document, 0, Resolution::Sd);
    expect(box->isEnabled() && box->isChecked(), "set 2 colors the SD picture only");
    panel.setDynamicSet(0);
    panel.refreshShadow(&document, 0, Resolution::Sd);
    expect(box->isEnabled() && !box->isChecked(), "set 0 does not");
    box->click();
    expect(toggles == std::vector<bool>{true}, "switching it reports it");
    panel.refreshShadow(&document, 3, Resolution::Sd);
    expect(!box->isEnabled() && !box->isChecked() && toggles.size() == 1,
           "a frame without dynamic data has nothing to switch; refreshing does not report");
}

void testDynamicPanel()
{
    using colorizing::render::Resolution;
    const colorizing::document::ProjectDocument document = makeInspectorPanelDocument();

    colorizing::app::DynamicPanel panel;
    int callbacks = 0;
    std::optional<int> changedSet;
    std::optional<std::size_t> clickedColor;
    std::optional<std::pair<int, bool>> toggledDirection;
    int shadowColorClicks = 0;
    panel.onDynamicSetChanged = [&](int setIndex) {
        ++callbacks;
        changedSet = setIndex;
    };
    panel.onColorClicked = [&](std::size_t originalColorIndex) {
        ++callbacks;
        clickedColor = originalColorIndex;
    };
    panel.onShadowDirectionToggled = [&](uint8_t bit, bool enabled) {
        ++callbacks;
        toggledDirection = {static_cast<int>(bit), enabled};
    };
    panel.onShadowColorClicked = [&]() {
        ++callbacks;
        ++shadowColorClicks;
    };

    auto* setSpinBox = childByToolTip<QSpinBox>(panel, "Dynamic mask set to paint");
    auto* shadowColorButton =
        childByToolTip<QToolButton>(panel, "Dynamic shadow color for active set");
    QList<QToolButton*> colorButtons;
    for (int colorIndex = 0; colorIndex < 16; ++colorIndex) {
        const QString toolTip =
            QStringLiteral("Dynamic color for original color %1").arg(colorIndex);
        colorButtons.push_back(childByToolTip<QToolButton>(panel, toolTip.toLatin1().constData()));
    }
    const auto directionButton = [&panel](const char* label) {
        const QString toolTip =
            QStringLiteral("Toggle dynamic shadow direction %1").arg(QLatin1String(label));
        return childByToolTip<QToolButton>(panel, toolTip.toLatin1().constData());
    };
    const std::vector<std::pair<const char*, int>> directions{
        {"TL", 0x01}, {"T", 0x02}, {"TR", 0x04}, {"L", 0x80},
        {"R", 0x08},  {"BL", 0x40}, {"B", 0x20}, {"BR", 0x10},
    };
    const auto checkedDirections = [&]() {
        int mask = 0;
        for (const auto& direction : directions) {
            mask |= directionButton(direction.first)->isChecked() ? direction.second : 0;
        }
        return mask;
    };
    const auto enabledDirectionCount = [&]() {
        int count = 0;
        for (const auto& direction : directions) {
            count += directionButton(direction.first)->isEnabled() ? 1 : 0;
        }
        return count;
    };

    expect(setSpinBox->minimum() == 0 &&
               setSpinBox->maximum() ==
                   static_cast<int>(colorizing::document::kMaxDynamicSetsPerFrame) - 1 &&
               !setSpinBox->isEnabled() && panel.dynamicSet() == 0,
           "dynamic panel should start on a disabled set 0");
    expect(colorButtons[7]->text() == QStringLiteral("7") &&
               colorButtons[7]->size() == QSize(32, 24) && enabledButtonCount(colorButtons) == 0,
           "dynamic panel should start with sixteen disabled color buttons");
    for (const auto& direction : directions) {
        const QToolButton* button = directionButton(direction.first);
        expect(button->text() == QLatin1String(direction.first) && button->isCheckable() &&
                   !button->isEnabled() && button->size() == QSize(32, 24),
               "dynamic panel should start with disabled shadow direction buttons");
    }
    expect(shadowColorButton->text() == QStringLiteral("Shadow Color") &&
               !shadowColorButton->isEnabled(),
           "dynamic panel should start with a disabled shadow color button");
    expect(tabVisitsInOrder(panel,
                            {setSpinBox, colorButtons[0], colorButtons[15],
                             directionButton("TL"), directionButton("T"), directionButton("TR"),
                             directionButton("L"), directionButton("R"), directionButton("BL"),
                             directionButton("B"), directionButton("BR"), shadowColorButton}),
           "dynamic panel should tab through its controls as laid out");

    panel.refreshColors(nullptr, std::nullopt, Resolution::Sd);
    panel.refreshShadow(nullptr, std::nullopt, Resolution::Sd);
    expect(enabledButtonCount(colorButtons) == 0 &&
               colorButtons[0]->toolTip() == QStringLiteral("Dynamic color is not available"),
           "dynamic panel should show no colors without a frame");
    expect(enabledDirectionCount() == 0 && checkedDirections() == 0 &&
               !shadowColorButton->isEnabled() &&
               shadowColorButton->toolTip() ==
                   QStringLiteral("Dynamic shadow color is not available"),
           "dynamic panel should show no shadow without a frame");

    panel.setDynamicSetEnabled(true);
    expect(setSpinBox->isEnabled(), "dynamic panel should enable the set on request");
    panel.refreshColors(&document, 0, Resolution::Sd);
    panel.refreshShadow(&document, 0, Resolution::Sd);
    expect(enabledButtonCount(colorButtons) == 16 &&
               colorButtons[1]->toolTip() == QStringLiteral("Set 0 original 1: 0xf800") &&
               colorButtons[1]->styleSheet().contains(QStringLiteral("#ff0000")) &&
               colorButtons[0]->toolTip() == QStringLiteral("Set 0 original 0: 0x0000"),
           "dynamic panel should show the SD colors of set 0");
    expect(enabledDirectionCount() == 8 && checkedDirections() == 0x81 &&
               shadowColorButton->isEnabled() &&
               shadowColorButton->toolTip() == QStringLiteral("Set 0 shadow color: 0x07e0") &&
               shadowColorButton->styleSheet().contains(QStringLiteral("#00ff00")),
           "dynamic panel should show the SD shadow of set 0");
    panel.refreshColors(&document, 0, Resolution::Hd);
    panel.refreshShadow(&document, 0, Resolution::Hd);
    expect(colorButtons[1]->toolTip() == QStringLiteral("Set 0 original 1: 0x001f") &&
               checkedDirections() == 0x08 &&
               shadowColorButton->toolTip() == QStringLiteral("Set 0 shadow color: 0xffff"),
           "dynamic panel should show the HD colors and shadow");

    panel.setDynamicSet(2);
    expect(panel.dynamicSet() == 2 && setSpinBox->value() == 2,
           "dynamic panel should take a set");
    expect(colorButtons[0]->toolTip() == QStringLiteral("Set 0 original 0: 0x0000"),
           "dynamic panel should wait for the owner to refresh after a set change");
    panel.refreshColors(&document, 0, Resolution::Sd);
    panel.refreshShadow(&document, 0, Resolution::Sd);
    expect(colorButtons[0]->toolTip() == QStringLiteral("Set 2 original 0: 0x07e0") &&
               checkedDirections() == 0x10 &&
               shadowColorButton->toolTip() == QStringLiteral("Set 2 shadow color: 0x0000"),
           "dynamic panel should show colors and shadow of the selected set");
    panel.setDynamicSet(99);
    expect(panel.dynamicSet() == 31, "dynamic panel should clamp the set");
    panel.setDynamicSet(0);

    panel.refreshColors(&document, 2, Resolution::Sd);
    panel.refreshShadow(&document, 2, Resolution::Sd);
    expect(enabledButtonCount(colorButtons) == 16 && enabledDirectionCount() == 8 &&
               checkedDirections() == 0 &&
               shadowColorButton->toolTip() == QStringLiteral("Set 0 shadow color: 0x0000"),
           "dynamic panel should show an empty shadow for dynamic data without shadow values");
    panel.refreshColors(&document, 3, Resolution::Sd);
    panel.refreshShadow(&document, 3, Resolution::Sd);
    expect(enabledButtonCount(colorButtons) == 0 && enabledDirectionCount() == 0 &&
               checkedDirections() == 0 && !shadowColorButton->isEnabled() &&
               colorButtons[1]->toolTip() == QStringLiteral("Dynamic color is not available"),
           "dynamic panel should be disabled for a frame without dynamic data");
    expect(callbacks == 0, "refreshing the dynamic panel should not notify");

    panel.refreshColors(&document, 0, Resolution::Sd);
    panel.refreshShadow(&document, 0, Resolution::Sd);
    setSpinBox->setValue(5);
    expect(changedSet == 5 && panel.dynamicSet() == 5,
           "changing the dynamic set should report it");
    colorButtons[3]->click();
    expect(clickedColor == std::size_t{3}, "clicking a dynamic color should report its index");
    directionButton("TR")->click();
    expect(toggledDirection == std::make_pair(0x04, true),
           "checking a shadow direction should report its bit");
    directionButton("TR")->click();
    expect(toggledDirection == std::make_pair(0x04, false),
           "unchecking a shadow direction should report its bit");
    shadowColorButton->click();
    expect(shadowColorClicks == 1 && callbacks == 5,
           "each dynamic panel action should notify exactly once");
    expect(document.dynamicColor(0, Resolution::Sd, 0, 1) == 0xf800 &&
               document.dynamicShadowDirection(0, Resolution::Sd, 0) == 0x81 &&
               !document.isDirty(),
           "dynamic panel should leave the document untouched");

    // A color dragged from the palette and dropped on a dynamic color is reported.
    std::optional<std::pair<std::size_t, uint16_t>> dropped;
    panel.onColorDropped = [&](std::size_t originalColorIndex, uint16_t rgb565) {
        dropped = std::make_pair(originalColorIndex, rgb565);
    };
    QMimeData mime;
    mime.setData("application/x-colorizing-rgb565", QByteArray::number(0x07e0));
    QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    // Straight to the button: QApplication would route a drop only during a real drag.
    static_cast<QObject*>(colorButtons[2])->event(&drop);
    expect(dropped == std::make_pair(std::size_t{2}, uint16_t{0x07e0}),
           "a palette color dropped on a dynamic color is reported with its index");

    // A set has a color per original color: 4 in a project with 4.
    const colorizing::document::ProjectDocument fourColors = makeInspectorPanelDocument(4);
    panel.refreshColors(&fourColors, 0, Resolution::Sd);
    int shown = 0;
    for (QToolButton* button : colorButtons) {
        shown += button->isVisibleTo(&panel) ? 1 : 0;
    }
    expect(shown == 4, "a 4-color project shows 4 dynamic colors");
    panel.refreshColors(&document, 0, Resolution::Sd);
    shown = 0;
    for (QToolButton* button : colorButtons) {
        shown += button->isVisibleTo(&panel) ? 1 : 0;
    }
    expect(shown == 16, "a 16-color project 16");
}

// The list of a frame's dynamic sets: one row per set it uses, a row for a new one, and a
// click selects the set.
void testDynamicSetList()
{
    colorizing::document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "dynamic_sets";
    colorizing::document::FrameDynamicData dynamicData{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        colorizing::document::makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(colorizing::document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    dynamicData.maskSd.pixels[0] = 2;
    dynamicData.maskSd.pixels[1] = 0;
    dynamicData.maskSd.pixels[2] = 0;
    dynamicData.maskSd.pixels[3] = 0;
    snapshot.frames.push_back(colorizing::document::ProjectFrameSnapshot{
        colorizing::document::makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        colorizing::document::makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        dynamicData,
    });
    const colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::fromSnapshot(snapshot);

    colorizing::app::DynamicPanel panel;
    std::optional<int> changedSet;
    panel.onDynamicSetChanged = [&](int setIndex) { changedSet = setIndex; };
    panel.refreshSets(&document, 0, colorizing::render::Resolution::Sd);
    auto* list = panel.findChild<QListWidget*>();
    expect(list && list->count() == 3 && list->item(0)->text() == QStringLiteral("0\n3 px") &&
               list->item(1)->text() == QStringLiteral("2\n1 px") &&
               list->item(2)->text() == QStringLiteral("New set (1)") &&
               !list->item(0)->icon().isNull(),
           "the list has the frame's sets with pictures and a row for a new one");
    expect(list->currentRow() == 0, "the selected set's row is the current one");
    list->itemClicked(list->item(1));
    expect(changedSet == 2 && panel.dynamicSet() == 2, "a click selects the set");
    panel.setDynamicSet(1);
    panel.refreshSets(&document, 0, colorizing::render::Resolution::Sd);
    expect(list->currentRow() == 2, "the new set's row follows the selection");
    panel.refreshSets(&document, std::nullopt, colorizing::render::Resolution::Sd);
    expect(list->count() == 0, "no frame, no sets");
}

void testBackgroundPanel()
{
    using colorizing::app::BackgroundPanelState;
    const colorizing::document::ProjectDocument document = makeInspectorPanelDocument();
    const auto stateFor = [&document](std::optional<std::size_t> backgroundIndex) {
        BackgroundPanelState state;
        state.document = &document;
        state.selectedBackgroundIndex = backgroundIndex;
        state.hasCurrentFrame = true;
        state.hasSelectedFrames = true;
        return state;
    };

    colorizing::app::BackgroundPanel panel;
    std::vector<int> requests(6, 0);
    panel.onAssignRequested = [&]() { ++requests[0]; };
    std::vector<bool> editRequests;
    panel.onEditRequested = [&](bool editing) {
        ++requests[1];
        editRequests.push_back(editing);
    };
    panel.onCopyFrameToBackgroundRequested = [&]() { ++requests[2]; };
    panel.onCopyBackgroundToFramesRequested = [&]() { ++requests[3]; };
    panel.onImportImageRequested = [&]() { ++requests[4]; };
    panel.onExportRequested = [&]() { ++requests[5]; };
    const auto requestCount = [&requests]() {
        int count = 0;
        for (int request : requests) {
            count += request;
        }
        return count;
    };

    auto* assignButton =
        childByToolTip<QToolButton>(panel, "Assign selected background to selected frames");
    auto* editButton = childByToolTip<QToolButton>(
        panel, "Edit the selected background on the canvas; click again to go back to the frame");
    auto* frameToBackgroundButton = childByToolTip<QToolButton>(
        panel, "Copy the current frame image into the selected background");
    auto* backgroundToFramesButton = childByToolTip<QToolButton>(
        panel, "Copy the selected background image into selected frames");
    auto* importButton =
        childByToolTip<QToolButton>(panel, "Import an image into the selected background");
    auto* exportButton =
        childByToolTip<QToolButton>(panel, "Export the selected background as PNG");
    const QList<QToolButton*> buttons{assignButton, editButton, frameToBackgroundButton,
                                      backgroundToFramesButton, importButton, exportButton};
    QLabel* selectedLabel = nullptr;
    for (QLabel* label : panel.findChildren<QLabel*>()) {
        if (label->text().startsWith(QStringLiteral("Selected:"))) {
            selectedLabel = label;
        }
    }
    expect(selectedLabel != nullptr, "background panel should show the selected background");
    expect(selectedLabel->text() == QStringLiteral("Selected: None") &&
               selectedLabel->wordWrap() && enabledButtonCount(buttons) == 0,
           "background panel should start without a background");
    expect(assignButton->text() == QStringLiteral("Assign Background") &&
               editButton->text() == QStringLiteral("Edit Background") &&
               frameToBackgroundButton->text() == QStringLiteral("Frame -> Background") &&
               backgroundToFramesButton->text() == QStringLiteral("Background -> Frame") &&
               importButton->text() == QStringLiteral("Import Image") &&
               exportButton->text() == QStringLiteral("Export Background"),
           "background panel should keep its button texts");
    expect(panel.findChildren<QLabel*>().size() == 2,
           "background panel should hold no label besides the selected background, once on "
           "each page");
    // Two pages: the frames' background (frame canvas) and the background itself (editing).
    expect(assignButton->parentWidget() == panel.framePage() &&
               editButton->parentWidget() == panel.framePage() &&
               frameToBackgroundButton->parentWidget() == panel.framePage() &&
               backgroundToFramesButton->parentWidget() == panel.framePage() &&
               importButton->parentWidget() == panel.recordPage() &&
               exportButton->parentWidget() == panel.recordPage(),
           "background panel should split the frames' background from the background itself");
    {
        const auto saved = panel.onEditRequested;
        bool editRequested = true;
        panel.onEditRequested = [&](bool editing) { editRequested = editing; };
        childByToolTip<QToolButton>(panel,
                                    "End editing the background and show the frame again")
            ->click();
        expect(!editRequested, "Back to Frame ends editing the background");
        panel.onEditRequested = saved;
    }
    expect(tabVisitsInOrder(panel,
                            {assignButton, editButton, frameToBackgroundButton,
                             backgroundToFramesButton, importButton, exportButton}),
           "background panel should tab through its controls as laid out");

    panel.refresh(BackgroundPanelState{});
    expect(enabledButtonCount(buttons) == 0 &&
               selectedLabel->text() == QStringLiteral("Selected: None"),
           "background panel should be disabled without a project");

    panel.refresh(stateFor(0));
    expect(enabledButtonCount(buttons) == 6 &&
               selectedLabel->text() == QStringLiteral("Selected: 0 (HD)"),
           "background panel should offer every action for a selected background");
    panel.refresh(stateFor(1));
    expect(selectedLabel->text() == QStringLiteral("Selected: 1"),
           "background panel should not mark a background without HD");

    // Edit BG is a toggle, as Edit Sprite: down while the background is edited, and clicked
    // again it asks to go back to the frame.
    expect(editButton->isCheckable() && !editButton->isChecked(), "Edit BG starts up");
    editButton->click();
    expect(editRequests == std::vector<bool>{true}, "Edit BG asks to edit the background");
    BackgroundPanelState editing = stateFor(1);
    editing.editing = true;
    panel.refresh(editing);
    expect(editButton->isChecked() && editRequests.size() == 1,
           "while it is edited, Edit BG is down (refresh reports nothing)");
    editButton->click();
    expect(editRequests == std::vector<bool>({true, false}),
           "clicked again, Edit BG asks to go back to the frame");
    panel.refresh(stateFor(1));
    expect(!editButton->isChecked(), "and is up when the frame is edited");
    requests[1] = 0;

    BackgroundPanelState withoutCurrentFrame = stateFor(1);
    withoutCurrentFrame.hasCurrentFrame = false;
    panel.refresh(withoutCurrentFrame);
    expect(!frameToBackgroundButton->isEnabled() && enabledButtonCount(buttons) == 5,
           "background panel should need a current frame to copy it into the background");
    BackgroundPanelState withoutSelectedFrames = stateFor(1);
    withoutSelectedFrames.hasSelectedFrames = false;
    panel.refresh(withoutSelectedFrames);
    expect(!assignButton->isEnabled() && !backgroundToFramesButton->isEnabled() &&
               frameToBackgroundButton->isEnabled() && editButton->isEnabled() &&
               importButton->isEnabled() && exportButton->isEnabled(),
           "background panel should need selected frames to assign or copy to them");

    panel.refresh(stateFor(5));
    expect(enabledButtonCount(buttons) == 0 &&
               selectedLabel->text() == QStringLiteral("Selected: None"),
           "background panel should ignore a background that does not exist");
    panel.refresh(stateFor(std::nullopt));
    expect(enabledButtonCount(buttons) == 0 &&
               selectedLabel->text() == QStringLiteral("Selected: None"),
           "background panel should be disabled without a selected background");
    expect(requestCount() == 0, "refreshing the background panel should not notify");

    panel.refresh(stateFor(0));
    for (int index = 0; index < buttons.size(); ++index) {
        buttons[index]->click();
        expect(requests[static_cast<std::size_t>(index)] == 1 && requestCount() == index + 1,
               "each background panel button should notify its own request exactly once");
    }
    expect(!document.isDirty(), "background panel should leave the document untouched");
}

// The HD of a sprite and of a background has its buttons in their own sections.
void testSpriteAndBackgroundHdButtons()
{
    const colorizing::document::ProjectDocument sprites = makeSpritePanelDocument();
    colorizing::app::SpritePanel spritePanel;
    int spriteCreates = 0;
    int spriteRemoves = 0;
    spritePanel.onCreateHdRequested = [&]() { ++spriteCreates; };
    spritePanel.onRemoveHdRequested = [&]() { ++spriteRemoves; };
    QToolButton* createSprite = nullptr;
    QToolButton* removeSprite = nullptr;
    for (QToolButton* button : spritePanel.findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Create HD Sprite")) {
            createSprite = button;
        } else if (button->text() == QStringLiteral("Remove HD Sprite")) {
            removeSprite = button;
        }
    }
    expect(createSprite && removeSprite, "the Sprite section has Create and Remove HD Sprite");
    colorizing::app::SpritePanelState spriteState;
    spriteState.document = &sprites;
    spriteState.frameIndex = 0;
    spriteState.spriteIndex = 0;
    spritePanel.refresh(spriteState);
    expect(!createSprite->isEnabled() && removeSprite->isEnabled(),
           "a sprite with HD: only Remove HD Sprite");
    removeSprite->click();
    spriteState.spriteIndex = 1;
    spritePanel.refresh(spriteState);
    expect(createSprite->isEnabled() && !removeSprite->isEnabled(),
           "a sprite without HD: only Create HD Sprite");
    createSprite->click();
    expect(spriteCreates == 1 && spriteRemoves == 1, "each reports once");

    const colorizing::document::ProjectDocument backgrounds = makeInspectorPanelDocument();
    colorizing::app::BackgroundPanel backgroundPanel;
    int backgroundCreates = 0;
    int backgroundRemoves = 0;
    backgroundPanel.onCreateHdRequested = [&]() { ++backgroundCreates; };
    backgroundPanel.onRemoveHdRequested = [&]() { ++backgroundRemoves; };
    QToolButton* createBackground = nullptr;
    QToolButton* removeBackground = nullptr;
    for (QToolButton* button : backgroundPanel.findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Create HD")) {
            createBackground = button;
        } else if (button->text() == QStringLiteral("Remove HD")) {
            removeBackground = button;
        }
    }
    expect(createBackground && removeBackground, "the Background section has Create and Remove HD");
    colorizing::app::BackgroundPanelState backgroundState;
    backgroundState.document = &backgrounds;
    backgroundState.selectedBackgroundIndex = 0;
    backgroundPanel.refresh(backgroundState);
    expect(!createBackground->isEnabled() && removeBackground->isEnabled(),
           "a background with HD: only Remove HD");
    removeBackground->click();
    backgroundState.selectedBackgroundIndex = 1;
    backgroundPanel.refresh(backgroundState);
    expect(createBackground->isEnabled() && !removeBackground->isEnabled(),
           "a background without HD: only Create HD");
    createBackground->click();
    backgroundState.selectedBackgroundIndex.reset();
    backgroundPanel.refresh(backgroundState);
    expect(!createBackground->isEnabled() && !removeBackground->isEnabled() &&
               backgroundCreates == 1 && backgroundRemoves == 1,
           "none without a selected background; each reports once");
}

void testFramePanel()
{
    using colorizing::app::FramePanelState;
    const colorizing::document::ProjectDocument document = makeInspectorPanelDocument();
    const colorizing::document::ProjectDocument noBackgrounds = makePreviewDocument();
    const auto stateFor = [](const colorizing::document::ProjectDocument& source,
                             std::optional<std::size_t> frameIndex,
                             bool hasSelectedFrames = true) {
        FramePanelState state;
        state.document = &source;
        state.frameIndex = frameIndex;
        state.hasSelectedFrames = hasSelectedFrames;
        return state;
    };

    colorizing::app::FramePanel panel;
    int callbacks = 0;
    int importRequests = 0;
    int createHdRequests = 0;
    int removeHdRequests = 0;
    std::optional<int> editedBackgroundId;
    std::vector<std::pair<colorizing::document::TriggerKind, uint32_t>> triggerEdits;
    std::optional<int> editedDuration;
    std::optional<bool> backgroundLayerToggled;
    panel.onBackgroundIdEdited = [&](int backgroundId) {
        ++callbacks;
        editedBackgroundId = backgroundId;
    };
    panel.onTriggerEdited = [&](colorizing::document::TriggerKind kind, uint32_t triggerId) {
        ++callbacks;
        triggerEdits.emplace_back(kind, triggerId);
    };
    panel.onDurationEdited = [&](int durationMs) {
        ++callbacks;
        editedDuration = durationMs;
    };
    panel.onBackgroundLayerToggled = [&](bool checked) {
        ++callbacks;
        backgroundLayerToggled = checked;
    };
    panel.onImportImageRequested = [&]() {
        ++callbacks;
        ++importRequests;
    };
    panel.onCreateHdRequested = [&]() {
        ++callbacks;
        ++createHdRequests;
    };
    panel.onRemoveHdRequested = [&]() {
        ++callbacks;
        ++removeHdRequests;
    };

    auto* backgroundIdSpinBox = childByToolTip<QSpinBox>(panel, "Assigned background ID");
    auto* triggerIdSpinBox =
        childByToolTip<QSpinBox>(panel, "PUP trigger the frame fires, 0 to 49999");
    auto* sceneComboBox = childByToolTip<QComboBox>(panel, "Scene of the project the frame starts");
    auto* durationSpinBox = childByToolTip<QSpinBox>(panel, "Per-frame cRP duration");
    auto* monochromeCheckBox = childByToolTip<QCheckBox>(
        panel, "From this frame on, libserum shows the frames in monochrome (trigger ID 65432)");
    auto* paletteMonochromeCheckBox = childByToolTip<QCheckBox>(
        panel,
        "From this frame on, libserum shows the frames in the colors of this frame's palette "
        "(trigger ID 65431)");
    using colorizing::document::TriggerKind;
    using colorizing::document::kNoTriggerId;
    auto* backgroundLayerButton =
        childByToolTip<QToolButton>(panel, "Show assigned background layer");
    auto* importButton =
        childByToolTip<QToolButton>(panel, "Import an image into selected frame pixels");
    auto* createHdButton = childByToolTip<QToolButton>(panel, "Create HD frame from SD frame");
    auto* removeHdButton = childByToolTip<QToolButton>(panel, "Remove HD frame data");

    // Whether the frame or a background is edited is Edit BG's (Frame Background section).
    bool hasTargetField = false;
    for (QComboBox* comboBox : panel.findChildren<QComboBox*>()) {
        hasTargetField = hasTargetField || comboBox->toolTip() == QStringLiteral("Drawing target");
    }
    expect(!hasTargetField, "frame panel should have no drawing target field");
    expect(backgroundIdSpinBox->minimum() == -1 && backgroundIdSpinBox->maximum() == -1 &&
               backgroundIdSpinBox->specialValueText() == QStringLiteral("None") &&
               !backgroundIdSpinBox->isEnabled(),
           "frame panel should start without background IDs");
    expect(triggerIdSpinBox->minimum() == -1 && triggerIdSpinBox->maximum() == 49999 &&
               triggerIdSpinBox->specialValueText() == QStringLiteral("None") &&
               !triggerIdSpinBox->isEnabled() && panel.triggerId() == kNoTriggerId &&
               !sceneComboBox->isEnabled() && sceneComboBox->count() == 1,
           "frame panel should start with disabled trigger inputs showing none");
    expect(durationSpinBox->maximum() == colorizing::app::kMaxFrameDurationMs &&
               durationSpinBox->singleStep() == 10 &&
               durationSpinBox->suffix() == QStringLiteral(" ms") && !durationSpinBox->isEnabled(),
           "frame panel should start with a disabled duration");
    expect(monochromeCheckBox->text() == QStringLiteral("Monochrome fallback") &&
               !monochromeCheckBox->isEnabled() && !monochromeCheckBox->isChecked() &&
               !paletteMonochromeCheckBox->isEnabled(),
           "frame panel should start with disabled monochrome fallbacks");
    {
        // Show Sprites beside Show Background, on at first, reported when toggled.
        QToolButton* spritesButton = nullptr;
        for (QToolButton* button : panel.findChildren<QToolButton*>()) {
            if (button->text() == QStringLiteral("Show Sprites")) {
                spritesButton = button;
            }
        }
        std::optional<bool> spritesToggled;
        panel.onSpritesLayerToggled = [&](bool checked) { spritesToggled = checked; };
        expect(spritesButton && spritesButton->isCheckable() && spritesButton->isChecked() &&
                   panel.isSpritesLayerChecked(),
               "frame panel should offer Show Sprites, on at first");
        spritesButton->setChecked(false);
        expect(spritesToggled == false && !panel.isSpritesLayerChecked(),
               "hiding the sprites should report it");
        spritesButton->setChecked(true);
        panel.onSpritesLayerToggled = nullptr;
    }
    expect(backgroundLayerButton->text() == QStringLiteral("Show Background") &&
               backgroundLayerButton->isCheckable() && panel.isBackgroundLayerChecked() &&
               !backgroundLayerButton->isEnabled(),
           "frame panel should start with the background layer shown but disabled");
    expect(importButton->text() == QStringLiteral("Import Image") && !importButton->isEnabled() &&
               createHdButton->text() == QStringLiteral("Create HD") &&
               !createHdButton->isEnabled() &&
               removeHdButton->text() == QStringLiteral("Remove HD") &&
               !removeHdButton->isEnabled(),
           "frame panel should start with disabled image and HD buttons");
    expect(tabVisitsInOrder(panel,
                            {backgroundIdSpinBox, triggerIdSpinBox,
                             sceneComboBox, monochromeCheckBox, paletteMonochromeCheckBox,
                             durationSpinBox, backgroundLayerButton, importButton,
                             createHdButton, removeHdButton}),
           "frame panel should tab through its controls as laid out");

    panel.refresh(stateFor(document, 0));
    expect(backgroundIdSpinBox->isEnabled() && backgroundIdSpinBox->maximum() == 1 &&
               backgroundIdSpinBox->value() == 0,
           "frame panel should show the assigned background");
    expect(triggerIdSpinBox->isEnabled() && triggerIdSpinBox->value() == 5 &&
               panel.triggerId() == 5 && !monochromeCheckBox->isEnabled() &&
               !paletteMonochromeCheckBox->isEnabled() && !sceneComboBox->isEnabled(),
           "frame panel should show the PUP trigger and close the other kinds");
    expect(durationSpinBox->isEnabled() && durationSpinBox->value() == 100 &&
               durationSpinBox->toolTip() == QStringLiteral("Per-frame cRP duration"),
           "frame panel should show the duration");
    expect(backgroundLayerButton->isEnabled() && importButton->isEnabled() &&
               !createHdButton->isEnabled() && removeHdButton->isEnabled(),
           "frame panel should offer Remove HD for an HD frame");

    panel.refresh(stateFor(document, 1, false));
    expect(backgroundIdSpinBox->isEnabled() && backgroundIdSpinBox->value() == -1 &&
               backgroundLayerButton->isEnabled(),
           "frame panel should show unassigned background data as none");
    expect(monochromeCheckBox->isChecked() && monochromeCheckBox->isEnabled() &&
               !triggerIdSpinBox->isEnabled() && triggerIdSpinBox->value() == -1 &&
               !paletteMonochromeCheckBox->isEnabled() && panel.triggerId() == 65432,
           "frame panel should show the monochrome fallback and close the other kinds");
    expect(durationSpinBox->value() == 0 &&
               durationSpinBox->toolTip() ==
                   QStringLiteral("Unsupported cRP duration 40000 ms is preserved until edited"),
           "frame panel should explain an unsupported duration");
    expect(!importButton->isEnabled() && createHdButton->isEnabled() &&
               !removeHdButton->isEnabled(),
           "frame panel should offer Create HD and no import without selected frames");

    panel.refresh(stateFor(document, 2));
    expect(backgroundIdSpinBox->value() == -1 && backgroundIdSpinBox->isEnabled(),
           "frame panel should show an out-of-range background ID as none");
    bool namesOther = false;
    for (QLabel* label : panel.findChildren<QLabel*>()) {
        namesOther = namesOther || (label->isVisibleTo(&panel) &&
                                    label->text() == QStringLiteral("Trigger ID 70000 is kept "
                                                                    "until another trigger is "
                                                                    "chosen"));
    }
    expect(triggerIdSpinBox->isEnabled() && triggerIdSpinBox->value() == -1 &&
               monochromeCheckBox->isEnabled() && paletteMonochromeCheckBox->isEnabled() &&
               panel.triggerId() == 70000 && namesOther,
           "frame panel should name a trigger ID of no kind and leave every input open");
    panel.refresh(stateFor(document, 3));
    expect(!backgroundIdSpinBox->isEnabled() && backgroundIdSpinBox->maximum() == 1 &&
               !backgroundLayerButton->isEnabled(),
           "frame panel should disable the background ID for a frame without background data");
    expect(triggerIdSpinBox->isEnabled() && triggerIdSpinBox->value() == 0 &&
               panel.triggerId() == 0,
           "frame panel should show trigger ID 0 as a trigger");
    colorizing::document::ProjectDocument withoutTrigger = document;
    withoutTrigger.setTriggerId(3, colorizing::document::kNoTriggerId);
    panel.refresh(stateFor(withoutTrigger, 3));
    expect(triggerIdSpinBox->isEnabled() && panel.triggerId() == kNoTriggerId &&
               triggerIdSpinBox->text() == QStringLiteral("None") &&
               !monochromeCheckBox->isChecked() && monochromeCheckBox->isEnabled() &&
               paletteMonochromeCheckBox->isEnabled() && !sceneComboBox->isEnabled(),
           "frame panel should show the file's no-trigger value as none, scenes need scenes");
    // Scenes: only the project's are offered; a scene ID without its scene is shown as it is.
    colorizing::document::ProjectDocument withScenes = withoutTrigger;
    colorizing::document::ColorizationSettings settings;
    settings.scenes.resize(2);
    settings.scenes[0].sceneId = 50001;
    settings.scenes[1].sceneId = 61999;
    withScenes.setColorizationSettings(settings);
    panel.refresh(stateFor(withScenes, 3));
    expect(sceneComboBox->isEnabled() && sceneComboBox->count() == 3 &&
               sceneComboBox->itemText(1) == QStringLiteral("50001") &&
               sceneComboBox->currentIndex() == 0,
           "frame panel should offer the project's scenes");
    withScenes.setTriggerId(3, 50088);
    panel.refresh(stateFor(withScenes, 3));
    expect(sceneComboBox->count() == 4 &&
               sceneComboBox->currentText() == QStringLiteral("50088 (no such scene)") &&
               !triggerIdSpinBox->isEnabled() && !monochromeCheckBox->isEnabled(),
           "frame panel should show a scene ID without its scene and close the other kinds");
    withScenes.setTriggerId(3, 65431);
    panel.refresh(stateFor(withScenes, 3));
    expect(paletteMonochromeCheckBox->isChecked() && !monochromeCheckBox->isEnabled() &&
               !sceneComboBox->isEnabled() && sceneComboBox->count() == 3,
           "frame panel should show the palette-based monochrome fallback");
    panel.refresh(stateFor(noBackgrounds, 0));
    expect(!backgroundIdSpinBox->isEnabled() && backgroundIdSpinBox->maximum() == -1 &&
               !backgroundLayerButton->isEnabled(),
           "frame panel should disable the background controls without backgrounds");

    panel.refresh(stateFor(document, std::nullopt));
    expect(createHdButton->text() == QStringLiteral("Create HD") &&
               createHdButton->toolTip() == QStringLiteral("Create HD frame from SD frame") &&
               removeHdButton->text() == QStringLiteral("Remove HD") &&
               removeHdButton->toolTip() == QStringLiteral("Remove HD frame data") &&
               !createHdButton->isEnabled() && !removeHdButton->isEnabled(),
           "frame panel should label the disabled HD buttons for the frame without a frame");
    expect(!backgroundIdSpinBox->isEnabled() &&
               backgroundIdSpinBox->maximum() == -1 && !triggerIdSpinBox->isEnabled() &&
               !durationSpinBox->isEnabled() && !monochromeCheckBox->isEnabled() &&
               !backgroundLayerButton->isEnabled() && !importButton->isEnabled() &&
               durationSpinBox->toolTip() == QStringLiteral("Per-frame cRP duration") &&
               panel.triggerId() == kNoTriggerId && !paletteMonochromeCheckBox->isEnabled() &&
               !sceneComboBox->isEnabled(),
           "frame panel should reset without a frame");
    panel.refresh(stateFor(document, 0));
    expect(createHdButton->text() == QStringLiteral("Create HD") &&
               createHdButton->toolTip() == QStringLiteral("Create HD frame from SD frame") &&
               removeHdButton->text() == QStringLiteral("Remove HD") &&
               removeHdButton->toolTip() == QStringLiteral("Remove HD frame data"),
           "frame panel should label the HD buttons for the frame again on refresh");

    panel.refresh(stateFor(noBackgrounds, 0));
    panel.refresh(stateFor(document, std::nullopt));
    panel.setImportImageEnabled(true);
    expect(importButton->isEnabled(), "frame panel should enable the import on request");
    expect(callbacks == 0, "refreshing and setting up the frame panel should not notify");

    panel.refresh(stateFor(document, 0));
    backgroundIdSpinBox->setValue(1);
    expect(editedBackgroundId == 1, "editing the background ID should report it");
    backgroundIdSpinBox->setValue(-1);
    expect(editedBackgroundId == -1, "clearing the background ID should report -1");
    triggerIdSpinBox->setValue(123);
    expect(triggerEdits.size() == 1 && triggerEdits.back().first == TriggerKind::Pup &&
               triggerEdits.back().second == 123,
           "editing the PUP trigger should report it");
    triggerIdSpinBox->setValue(-1);
    expect(triggerEdits.back().second == kNoTriggerId,
           "clearing the PUP trigger should report no trigger");
    durationSpinBox->setValue(250);
    expect(editedDuration == 250, "editing the duration should report it");
    // Frame 0 without its PUP trigger: every kind is open.
    withoutTrigger.setTriggerId(0, kNoTriggerId);
    panel.refresh(stateFor(withoutTrigger, 0));
    monochromeCheckBox->click();
    expect(triggerEdits.back().first == TriggerKind::Monochrome &&
               triggerEdits.back().second == 65432,
           "checking the monochrome fallback should report it");
    backgroundLayerButton->click();
    expect(backgroundLayerToggled == false && !panel.isBackgroundLayerChecked(),
           "hiding the background layer should report it");
    importButton->click();
    // Frame 0 has HD: Remove HD is offered, Create HD is not.
    createHdButton->click();
    removeHdButton->click();
    expect(importRequests == 1 && createHdRequests == 0 && removeHdRequests == 1 &&
               callbacks == 9,
           "each frame panel action should notify exactly once");

    panel.refresh(stateFor(document, 1));
    expect(!panel.isBackgroundLayerChecked(),
           "frame panel should keep its own state across refreshes");
    expect(callbacks == 9, "refreshing the frame panel should not notify");
    expect(document.triggerId(0) == 5 && document.frameDurationMs(0) == 100 &&
               document.backgroundId(0) == 0 && !document.isDirty(),
           "frame panel should leave the document untouched");

    // Tags: the frame's, as a comma list; a change is reported once.
    colorizing::document::ProjectDocument tagged = makeInspectorPanelDocument();
    tagged.setFrameTags(0, {"Multiball", "Wizard Mode"});
    panel.refresh(stateFor(tagged, 0));
    auto* tagsEdit = childByToolTip<QLineEdit>(
        panel, "The frame's tags, separated by commas; a change adds or removes them on all "
               "selected frames");
    expect(tagsEdit->isEnabled() && tagsEdit->text() == QStringLiteral("Multiball, Wizard Mode"),
           "the Tags field shows the frame's tags");
    std::vector<QString> tagEdits;
    panel.onTagsEdited = [&](const QString& text) { tagEdits.push_back(text); };
    tagsEdit->setText(QStringLiteral("Multiball, Jackpot"));
    Q_EMIT tagsEdit->editingFinished();
    Q_EMIT tagsEdit->editingFinished();
    expect(tagEdits == std::vector<QString>{QStringLiteral("Multiball, Jackpot")},
           "a changed Tags field is reported once");
    panel.refresh(stateFor(tagged, std::nullopt));
    expect(!tagsEdit->isEnabled() && tagsEdit->text().isEmpty(), "no frame, no tags");
}

void testRecordListPage()
{
    auto* list = new QListWidget();
    colorizing::app::RecordListPage page(list, QStringLiteral("Add a record"),
                                         QStringLiteral("Delete the selected record"));
    int newRequests = 0;
    int deleteRequests = 0;
    page.onNewRequested = [&]() { ++newRequests; };
    page.onDeleteRequested = [&]() { ++deleteRequests; };
    auto* newButton = childByToolTip<QToolButton>(page, "Add a record");
    auto* deleteButton = childByToolTip<QToolButton>(page, "Delete the selected record");
    expect(newButton->text() == QStringLiteral("New") &&
               deleteButton->text() == QStringLiteral("Delete"),
           "record list page should have New and Delete");
    expect(list->parentWidget() == &page, "record list page should hold the list");
    expect(!newButton->isEnabled() && !deleteButton->isEnabled(),
           "record list page should start with both buttons disabled");
    expect(page.tabOrder() == std::vector<QWidget*>({newButton, deleteButton, list}),
           "record list page should put New and Delete before the list");

    colorizing::app::RecordListPageState state;
    state.recordCount = 3;
    state.maxRecordCount = 255;
    state.hasSelection = true;
    page.refresh(state);
    expect(!newButton->isEnabled() && !deleteButton->isEnabled(),
           "record list page should offer nothing without a project");
    state.hasProject = true;
    page.refresh(state);
    expect(newButton->isEnabled() && deleteButton->isEnabled(),
           "record list page should offer New and Delete for a project with a selection");
    state.hasSelection = false;
    page.refresh(state);
    expect(newButton->isEnabled() && !deleteButton->isEnabled(),
           "record list page should not offer Delete without a selection");
    state.recordCount = 255;
    state.hasSelection = true;
    page.refresh(state);
    expect(!newButton->isEnabled() && deleteButton->isEnabled(),
           "record list page should not offer New at the record limit");
    state.recordCount = 254;
    state.hasSource = false;
    page.refresh(state);
    expect(!newButton->isEnabled(),
           "record list page should not offer New while there is nothing to make a record of");
    state.hasSource = true;
    state.recordCount = 0;
    state.hasSelection = false;
    page.refresh(state);
    expect(newButton->isEnabled() && !deleteButton->isEnabled(),
           "record list page should offer New for an empty list");

    // Issue #37: the count beside the buttons, and how many the filter leaves.
    for (int row = 0; row < 3; ++row) {
        list->addItem(QStringLiteral("record %1").arg(row));
    }
    state.recordCount = 3;
    page.refresh(state);
    expect(page.countText() == QStringLiteral("Total: 3"), "the records in all");
    list->item(0)->setHidden(true);
    list->item(2)->setHidden(true);
    page.updateCount();
    expect(page.countText() == QStringLiteral("1 of 3"), "how many the filter leaves");
    state.hasProject = false;
    page.refresh(state);
    expect(page.countText().isEmpty(), "no count without a project");
    list->clear();
    state.hasProject = true;
    state.recordCount = 0;
    page.refresh(state);

    newButton->click();
    state.hasSelection = true;
    page.refresh(state);
    deleteButton->click();
    expect(newRequests == 1 && deleteRequests == 1,
           "record list page should report New and Delete once each");
    expect(list->count() == 0, "record list page should not change the list itself");
}

void runAppHelpersWidgetTests();
void runAutosaveWidgetTests();
void runFramesClipboardWidgetTests();
void runGitHubWidgetTests();
void runTeamSessionWidgetTests();
void runTeamControllerWidgetTests();
void runDmdOutputWidgetTests();
void runEditorPluginsWidgetTests();
void runVideoClipWidgetTests();
void runImageImportWidgetTests();
void runHelperLinesWidgetTests();
void runScenesWidgetTests();
void runSpeakerLightWidgetTests();
void runPupHostWidgetTests();
void runSettingsWidgetTests();
void runHandbookWidgetTests();
void runBigOriginalWidgetTests();
void runCanvasEditControllerTests();
void runColorWidgetTests();
void runDumpImportWidgetTests();
void runEditorMatchesRuntimeTests();
void runFrameTesterWidgetTests();
void runFrameSettingsWidgetTests();
void runGifWriterWidgetTests();
void runGifExportWidgetTests();
void runNativeOriginalWidgetTests();
void runPlaneLabelsWidgetTests();
void runRuntimeViewWidgetTests();
void runSelectionWidgetTests();
void runProjectTaskWidgetTests();

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    testFramePreviewWidgetRendersPixels();
    testFrameThumbnailRendersPixels();
    testFrameThumbnailComposesBackgroundBeforeDynamic();
    testFramePreviewHitTesting();
    testFramePreviewZoom();
    testFramePreviewClickCallback();
    testFramePreviewStrokeCallback();
    testPickReturnsToTheTool();
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
    testBackgroundHdEditableOnFrameWithoutHd();
    testDotPixelsHaveNoOutline();
    testPositionHandler();
    testFramePreviewDoesNotShowHdReferenceBelowOriginal();
    testSpriteEditorZoom();
    testSpriteEditorRendersAndEditsDetectionArea();
    testSpriteEditorHdKeepsSdFootprint();
    testColorRotationPreviewTransform();
    testColorRotationPanel();
    testRotationSlotUse();
    testPalettePanel();
    testSpritePanel();
    testSpriteEditorSourceFrame();
    testSpritePanelWarnsAboutManySprites();
    testSpriteAndBackgroundHdButtons();
    testMaskPanel();
    testDynamicPanel();
    testDynamicPanelOriginalOnlySets();
    testDynamicSetList();
    testBackgroundPanel();
    testFramePanel();
    testRecordListPage();
    runCanvasEditControllerTests();
    runAutosaveWidgetTests();
    runFramesClipboardWidgetTests();
    runGitHubWidgetTests();
    runTeamSessionWidgetTests();
    runTeamControllerWidgetTests();
    runDmdOutputWidgetTests();
    runEditorPluginsWidgetTests();
    runVideoClipWidgetTests();
    runImageImportWidgetTests();
    runHelperLinesWidgetTests();
    runScenesWidgetTests();
    runSpeakerLightWidgetTests();
    runPupHostWidgetTests();
    runSettingsWidgetTests();
    runHandbookWidgetTests();
    runFrameTesterWidgetTests();
    runFrameSettingsWidgetTests();
    runGifWriterWidgetTests();
    runGifExportWidgetTests();
    runBigOriginalWidgetTests();
    runNativeOriginalWidgetTests();
    runPlaneLabelsWidgetTests();
    runRuntimeViewWidgetTests();
    runSelectionWidgetTests();
    runEditorMatchesRuntimeTests();
    runDumpImportWidgetTests();
    runAppHelpersWidgetTests();
    runColorWidgetTests();
    runProjectTaskWidgetTests();
    std::cout << "colorizing_qt_v2_widget_tests passed\n";
    return 0;
}
