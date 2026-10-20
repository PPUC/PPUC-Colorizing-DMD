// Rendering, hit testing and editing of a project whose original frames are 256x64: only its
// HD plane exists, one to one with the original.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QToolButton>

#include "CanvasEditController.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "FramePreviewWidget.h"
#include "FrameThumbnail.h"
#include "panels/FramePanel.h"
#include "panels/SpritePanel.h"
#include "SpriteEditorWidget.h"

using colorizing::app::CanvasEditContext;
using colorizing::app::CanvasEditController;
using colorizing::app::CanvasEditLayer;
using colorizing::app::CanvasEditOutputs;
using colorizing::app::CanvasPixelOverlay;
using colorizing::app::FramePreviewWidget;
using colorizing::document::ProjectDocument;
using colorizing::edit::DrawingTool;
using colorizing::render::CanvasHit;
using colorizing::render::CanvasRegion;
using colorizing::render::Point;
using colorizing::render::Resolution;
using colorizing::render::kHdFrameSize;
using colorizing::render::kSdFrameSize;

namespace document = colorizing::document;
namespace edit = colorizing::edit;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: 256x64 original: " << message << '\n';
        std::exit(1);
    }
}

constexpr uint16_t kRed = 0xf800;
constexpr uint16_t kGreen = 0x07e0;
constexpr uint16_t kBlue = 0x001f;
constexpr uint16_t kYellow = 0xffe0;
constexpr uint16_t kMagenta = 0xf81f;
constexpr uint16_t kCyan = 0x07ff;
constexpr uint16_t kWhite = 0xffff;

std::size_t at(colorizing::render::Size size, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
           static_cast<std::size_t>(x);
}

document::SpriteRecord makeSprite(const uint8_t original[4], bool hasHd)
{
    const colorizing::render::Size cell{static_cast<int>(document::kMaxSpriteWidth),
                                        static_cast<int>(document::kMaxSpriteHeight)};
    document::SpriteRecord sprite;
    sprite.hasHd = hasHd;
    sprite.originalSd = document::makeIndexedFrame(cell, 255);
    sprite.colorizedSd = document::makeRgb565Frame(cell, 0);
    sprite.maskHd = document::makeIndexedFrame(cell, 255);
    sprite.colorizedHd = document::makeRgb565Frame(cell, 0);
    uint32_t dword = 0;
    for (int x = 0; x < 4; ++x) {
        sprite.originalSd.pixels[static_cast<std::size_t>(x)] = original[x];
        // Never shown in such a project.
        sprite.colorizedSd.pixels[static_cast<std::size_t>(x)] = kMagenta;
        if (hasHd) {
            sprite.maskHd.pixels[static_cast<std::size_t>(x)] = original[x];
            sprite.colorizedHd.pixels[static_cast<std::size_t>(x)] = kCyan;
        }
        dword |= static_cast<uint32_t>(original[x]) << (8 * x);
    }
    sprite.detectionDwords = {dword, 0xffffffff, 0xffffffff, 0xffffffff};
    sprite.detectionDwordPositions = {0, 0xffff, 0xffff, 0xffff};
    sprite.detectionAreas = {
        document::SpriteDetectionArea{0, 0, 4, 1},
        document::SpriteDetectionArea{},
        document::SpriteDetectionArea{},
        document::SpriteDetectionArea{},
    };
    return sprite;
}

constexpr uint16_t kSdColor = 0x2104;
constexpr uint16_t kHdColor = 0x4208;
constexpr uint16_t kSdBackground = 0x0410;
constexpr uint16_t kHdBackground = 0x8410;

// Originals and comparison masks are 256x64; everything not listed is 0.
//
// Frame 0 has everything in HD:
//   original (10,6)=1, (11,6)=2, (10,7)=3, (40,20)=1 and 1,2,3,0 from (101,41): an odd row and
//     an odd column, which a (2x, 2y) subsample of the original never sees
//   dynamic HD: set 2 at (11,6) and (10,7), set 4 (shadow to the right) at (40,20)
//   background 0 (HD): HD mask at (10,6) and (12,6)
//   comparison mask 0: (200,50) and (255,63)
//   slot 0: sprite 0 (HD, original 1,2,3,0) with the whole original as bounds
//   its SD frame, SD dynamic data and SD background mask hold other content, never shown
// Frame 1 has no HD frame.
// Frame 2 has an HD frame, background 1 without HD and, in slot 0, sprite 1 without HD, whose
//   original 2,2,1,1 is in the frame from (31,11).
ProjectDocument makeBigOriginalDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "big_original_preview";
    snapshot.geometry =
        colorizing::render::ProjectGeometry(colorizing::render::PlaneLayout::NativeExtra);

    document::IndexedFrame mask = document::makeIndexedFrame(kHdFrameSize, 0);
    mask.pixels[at(kHdFrameSize, 200, 50)] = 1;
    mask.pixels[at(kHdFrameSize, 255, 63)] = 1;
    snapshot.comparisonMasks.push_back(mask);

    document::IndexedFrame original = document::makeIndexedFrame(kHdFrameSize, 0);
    original.pixels[at(kHdFrameSize, 10, 6)] = 1;
    original.pixels[at(kHdFrameSize, 11, 6)] = 2;
    original.pixels[at(kHdFrameSize, 10, 7)] = 3;
    original.pixels[at(kHdFrameSize, 40, 20)] = 1;
    const uint8_t spriteRow[4] = {1, 2, 3, 0};
    for (int x = 0; x < 4; ++x) {
        original.pixels[at(kHdFrameSize, 101 + x, 41)] = spriteRow[x];
    }

    document::FrameDynamicData dynamicData{
        document::makeIndexedFrame(kSdFrameSize, 255),
        document::makeIndexedFrame(kHdFrameSize, 255),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, kRed),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    dynamicData.maskSd.pixels[at(kSdFrameSize, 5, 3)] = 2;
    dynamicData.maskSd.pixels[at(kSdFrameSize, 20, 10)] = 4;
    dynamicData.maskHd.pixels[at(kHdFrameSize, 11, 6)] = 2;
    dynamicData.maskHd.pixels[at(kHdFrameSize, 10, 7)] = 2;
    dynamicData.maskHd.pixels[at(kHdFrameSize, 40, 20)] = 4;
    dynamicData.colorsHd[2 * 16 + 2] = kGreen;
    dynamicData.colorsHd[2 * 16 + 3] = kBlue;
    dynamicData.colorsHd[4 * 16 + 1] = kYellow;
    dynamicData.shadowDirectionsSd.assign(document::kMaxDynamicSetsPerFrame, 0x08);
    dynamicData.shadowColorsSd.assign(document::kMaxDynamicSetsPerFrame, kRed);
    dynamicData.shadowDirectionsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowDirectionsHd[4] = 0x08;
    dynamicData.shadowColorsHd[4] = kWhite;

    document::FrameBackgroundData backgroundData{
        0,
        document::makeIndexedFrame(kSdFrameSize, 1),
        document::makeIndexedFrame(kHdFrameSize, 0),
    };
    backgroundData.maskHd.pixels[at(kHdFrameSize, 10, 6)] = 1;
    backgroundData.maskHd.pixels[at(kHdFrameSize, 12, 6)] = 1;
    snapshot.backgrounds.push_back(document::BackgroundRecord{
        document::makeRgb565Frame(kSdFrameSize, kSdBackground),
        document::makeRgb565Frame(kHdFrameSize, kHdBackground),
    });
    snapshot.backgrounds.push_back(document::BackgroundRecord{
        document::makeRgb565Frame(kSdFrameSize, kSdBackground),
    });

    const uint8_t otherRow[4] = {2, 2, 1, 1};
    snapshot.sprites.push_back(makeSprite(spriteRow, true));
    snapshot.sprites.push_back(makeSprite(otherRow, false));
    std::vector<document::FrameSpriteReference> spriteReferences(document::kMaxSpritesPerFrame);
    spriteReferences[0] = document::FrameSpriteReference{0, 0, 0, 255, 63};

    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        original,
        document::makeRgb565Frame(kSdFrameSize, kSdColor),
        document::makeRgb565Frame(kHdFrameSize, kHdColor),
        0,
        0,
        dynamicData,
        backgroundData,
        spriteReferences,
    });
    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        original,
        document::makeRgb565Frame(kSdFrameSize, kSdColor),
        std::nullopt,
        0,
        0,
    });
    document::IndexedFrame otherOriginal = document::makeIndexedFrame(kHdFrameSize, 0);
    for (int x = 0; x < 4; ++x) {
        otherOriginal.pixels[at(kHdFrameSize, 31 + x, 11)] = otherRow[x];
    }
    backgroundData.backgroundId = 1;
    backgroundData.maskHd = document::makeIndexedFrame(kHdFrameSize, 1);
    spriteReferences[0].spriteId = 1;
    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        otherOriginal,
        document::makeRgb565Frame(kSdFrameSize, kSdColor),
        document::makeRgb565Frame(kHdFrameSize, kHdColor),
        0,
        0,
        std::nullopt,
        backgroundData,
        spriteReferences,
    });
    std::string error;
    expect(document::isValidProjectSnapshot(snapshot, &error), "fixture is valid: " + error);
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

template <typename Call>
bool throwsLogicError(Call call)
{
    try {
        call();
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

bool contains(const document::Rgb565Frame& frame, uint16_t color)
{
    return std::find(frame.pixels.begin(), frame.pixels.end(), color) != frame.pixels.end();
}

QImage grab(FramePreviewWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();
    return image;
}

// Where a canvas region is on the widget, found through the widget's own hit test.
struct RegionLocator {
    QPoint origin;
    int cell = 1;

    [[nodiscard]] QPoint operator()(int x, int y) const
    {
        return origin + QPoint(x * cell, y * cell);
    }
};

RegionLocator locate(const FramePreviewWidget& widget, CanvasRegion region)
{
    for (int py = 0; py < widget.height(); ++py) {
        for (int px = 0; px < widget.width(); ++px) {
            const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(px, py));
            if (!hit || hit->region != region) {
                continue;
            }
            expect(hit->x == 0 && hit->y == 0, "a region starts with its first pixel");
            RegionLocator locator{QPoint(px, py), 1};
            while (widget.hitTestAt(QPoint(px + locator.cell, py))->x == 0) {
                ++locator.cell;
            }
            return locator;
        }
    }
    expect(false, "region is not on the widget");
    return {};
}

QPoint positionOf(const FramePreviewWidget& widget, CanvasRegion region, int x, int y)
{
    const QPoint position = locate(widget, region)(x, y);
    const std::optional<CanvasHit> hit = widget.hitTestAt(position);
    expect(hit && hit->region == region && hit->x == x && hit->y == y,
           "widget position of pixel " + std::to_string(x) + "," + std::to_string(y));
    return position;
}

void testOriginalRegionShowsEveryOriginalPixel()
{
    ProjectDocument document = makeBigOriginalDocument();
    FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);

    const std::optional<CanvasHit> first = widget.hitTestAt(positionOf(
        widget, CanvasRegion::OriginalFrame, 0, 0));
    const QPoint lastPosition = positionOf(widget, CanvasRegion::OriginalFrame, 255, 63);
    const std::optional<CanvasHit> last = widget.hitTestAt(lastPosition);
    expect(first && first->resolution == Resolution::Hd && last &&
               last->resolution == Resolution::Hd,
           "hits on the original region are on the original's 256x64 grid");
    const int cell = locate(widget, CanvasRegion::OriginalFrame).cell;
    expect(!widget.hitTestAt(lastPosition + QPoint(cell, 0)) &&
               !widget.hitTestAt(lastPosition + QPoint(0, cell)),
           "the original region ends at 255,63");

    // The HD plane above it is the only plane and lines up with the original pixel for pixel.
    widget.setActiveResolution(Resolution::Sd);
    const std::optional<CanvasHit> planeHit =
        widget.hitTestAt(positionOf(widget, CanvasRegion::ColorizedFrame, 255, 63));
    expect(planeHit && planeHit->resolution == Resolution::Hd,
           "the colorized region is the HD plane, whatever resolution is asked for");
    const QPoint hdFirst = positionOf(widget, CanvasRegion::ColorizedFrame, 0, 0);
    const QPoint hdOdd = positionOf(widget, CanvasRegion::ColorizedFrame, 101, 41);
    const QPoint originalFirst = positionOf(widget, CanvasRegion::OriginalFrame, 0, 0);
    const QPoint originalOdd = positionOf(widget, CanvasRegion::OriginalFrame, 101, 41);
    expect(hdFirst.x() == originalFirst.x() && hdOdd - hdFirst == originalOdd - originalFirst,
           "a plane pixel is as large as the original pixel it shows");

    const QImage image = grab(widget);
    const RegionLocator position = locate(widget, CanvasRegion::OriginalFrame);
    const auto shown = [&](int x, int y) { return image.pixelColor(position(x, y)); };
    expect(shown(10, 6) == colorizing::app::originalIndexToColor(1, 16) &&
               shown(11, 6) == colorizing::app::originalIndexToColor(2, 16) &&
               shown(10, 7) == colorizing::app::originalIndexToColor(3, 16) &&
               shown(11, 7) == colorizing::app::originalIndexToColor(0, 16) &&
               shown(102, 41) == colorizing::app::originalIndexToColor(2, 16) &&
               shown(5, 3) == colorizing::app::originalIndexToColor(0, 16),
           "original pixels are drawn one to one");
}

void testOverlaysOnOriginalRegion()
{
    ProjectDocument document = makeBigOriginalDocument();
    FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);
    widget.setSelectedSpriteBoundsOverlayEnabled(false);
    const RegionLocator position = locate(widget, CanvasRegion::OriginalFrame);
    const auto isMaskTinted = [](const QColor& color) {
        return color.red() > 150 && color.green() < 80 && color.blue() > 120;
    };

    const QImage plain = grab(widget);
    widget.setComparisonMaskOverlayEnabled(true);
    const QImage masked = grab(widget);
    expect(isMaskTinted(masked.pixelColor(position(200, 50))) &&
               isMaskTinted(masked.pixelColor(position(255, 63))),
           "comparison mask overlay marks 256x64 mask pixels");
    expect(masked.pixelColor(position(201, 50)) == plain.pixelColor(position(201, 50)) &&
               masked.pixelColor(position(100, 25)) == plain.pixelColor(position(100, 25)) &&
               masked.pixelColor(position(200, 51)) == plain.pixelColor(position(200, 51)),
           "comparison mask overlay marks nothing else");
    widget.setComparisonMaskOverlayEnabled(false);

    // Slot bounds are pixels of the original.
    widget.setSelectedSpriteBoundsOverlayEnabled(true);
    widget.setSelectedSpriteReference(document::FrameSpriteReference{0, 21, 11, 201, 63});
    const QImage bounded = grab(widget);
    const auto changed = [&](int x, int y) {
        return bounded.pixelColor(position(x, y)) != plain.pixelColor(position(x, y));
    };
    expect(changed(21, 11) && changed(201, 63) && changed(201, 11) && changed(21, 63),
           "sprite slot bounds cover their native corners, beyond 127,31");
    expect(!changed(20, 11) && !changed(21, 10) && !changed(202, 63),
           "sprite slot bounds end at their native corners");
    widget.setSelectedSpriteReference(std::nullopt);

    const QColor previewColor(0, 255, 0, 255);
    widget.setPreviewPixels(CanvasRegion::OriginalFrame,
                            Resolution::Hd,
                            {Point{200, 50}, Point{255, 63}},
                            previewColor);
    const QImage previewed = grab(widget);
    expect(previewed.pixelColor(position(200, 50)) == previewColor &&
               previewed.pixelColor(position(255, 63)) == previewColor &&
               previewed.pixelColor(position(199, 50)) == plain.pixelColor(position(199, 50)),
           "previews on the original region are original coordinates");
    widget.setPreviewPixels(CanvasRegion::OriginalFrame,
                            Resolution::Sd,
                            {Point{100, 25}},
                            previewColor);
    expect(grab(widget) == plain, "an SD preview does not belong to a 256x64 original");
    widget.clearPreviewPixels();

    const QColor selectionColor(255, 0, 255, 255);
    widget.setSelectionOverlayPixels(CanvasRegion::OriginalFrame,
                                     Resolution::Hd,
                                     {Point{254, 62}, Point{255, 63}},
                                     selectionColor);
    const QImage selected = grab(widget);
    expect(selected.pixelColor(position(254, 62)) == selectionColor &&
               selected.pixelColor(position(255, 63)) == selectionColor &&
               selected.pixelColor(position(255, 62)) == plain.pixelColor(position(255, 62)),
           "selection overlays on the original region are original coordinates");
}

void testOnlyTheHdPlaneIsRendered()
{
    const ProjectDocument document = makeBigOriginalDocument();
    expect(throwsLogicError([&]() {
               (void)colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false);
           }) &&
               throwsLogicError([&]() {
                   (void)colorizing::app::resolvedFramePixel(document,
                                                             0,
                                                             Resolution::Sd,
                                                             5,
                                                             3,
                                                             false);
               }),
           "there is no SD plane to compose");

    const document::Rgb565Frame frame =
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, false);
    expect(frame.size.width == 256 && frame.size.height == 64, "the plane is 256x64");
    expect(frame.pixels[at(kHdFrameSize, 11, 6)] == kGreen &&
               frame.pixels[at(kHdFrameSize, 10, 7)] == kBlue,
           "HD dynamic colour comes from original (x, y)");
    expect(frame.pixels[at(kHdFrameSize, 10, 6)] == kHdColor &&
               frame.pixels[at(kHdFrameSize, 11, 7)] == kHdColor,
           "HD pixels outside the dynamic mask keep their colour");
    expect(frame.pixels[at(kHdFrameSize, 40, 20)] == kYellow &&
               frame.pixels[at(kHdFrameSize, 41, 20)] == kWhite &&
               frame.pixels[at(kHdFrameSize, 39, 20)] == kHdColor,
           "HD dynamic shadow is cast by the lit original pixel (x, y)");

    const document::Rgb565Frame withBackground =
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, true);
    expect(withBackground.pixels[at(kHdFrameSize, 12, 6)] == kHdBackground &&
               withBackground.pixels[at(kHdFrameSize, 10, 6)] == kHdColor,
           "HD background applies where original (x, y) is unlit");
    for (const document::Rgb565Frame* composed : {&frame, &withBackground}) {
        expect(!contains(*composed, kSdColor) && !contains(*composed, kSdBackground) &&
                   !contains(*composed, kRed) && !contains(*composed, kMagenta),
               "no SD frame, SD dynamic, SD background or SD sprite data is shown");
    }

    const QImage thumbnail = colorizing::app::makeFrameThumbnail(document, 0, QSize(160, 84))
                                 .pixmap(QSize(160, 84))
                                 .toImage();
    expect(thumbnail.pixelColor(20, 30) == colorizing::app::rgb565ToColor(kHdBackground) ||
               thumbnail.pixelColor(20, 30) == colorizing::app::rgb565ToColor(kHdColor),
           "the thumbnail shows the HD plane");
}

void testSpriteIsMatchedOnNativeOriginal()
{
    ProjectDocument document = makeBigOriginalDocument();
    const auto composed = [&]() {
        return colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, false);
    };
    const document::Rgb565Frame frame = composed();
    for (int x = 101; x < 105; ++x) {
        expect(frame.pixels[at(kHdFrameSize, x, 41)] == kCyan,
               "a sprite at an odd row and column is found on the native original and drawn "
               "there one to one");
    }
    expect(frame.pixels[at(kHdFrameSize, 100, 41)] == kHdColor &&
               frame.pixels[at(kHdFrameSize, 105, 41)] == kHdColor &&
               frame.pixels[at(kHdFrameSize, 101, 40)] == kHdColor &&
               frame.pixels[at(kHdFrameSize, 101, 42)] == kHdColor &&
               std::count(frame.pixels.begin(), frame.pixels.end(), kCyan) == 4,
           "the sprite is drawn at its own size, not doubled");

    // Bounds are native coordinates as well.
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{0, 101, 41, 255, 63});
    expect(composed().pixels[at(kHdFrameSize, 101, 41)] == kCyan,
           "bounds that start at the sprite's odd position contain it");
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{0, 102, 41, 255, 63});
    expect(!contains(composed(), kCyan), "a sprite outside its native bounds is not drawn");
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{0, 0, 0, 127, 31});
    expect(!contains(composed(), kCyan), "128x32 bounds do not reach a sprite at 101,41");
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{0, 0, 0, 255, 63});

    // The HD mask decides what is drawn, the sprite original what is detected.
    document::SpriteHdState hd = document.spriteHdState(0);
    hd.maskHd.pixels[1] = 255;
    hd.colorizedHd.pixels[2] = kWhite;
    document.setSpriteHdState(0, hd);
    const document::Rgb565Frame masked = composed();
    expect(masked.pixels[at(kHdFrameSize, 101, 41)] == kCyan &&
               masked.pixels[at(kHdFrameSize, 102, 41)] == kHdColor &&
               masked.pixels[at(kHdFrameSize, 103, 41)] == kWhite,
           "HD sprite mask and colours line up with the sprite original pixel for pixel");
}

// The calls MainWindow::captureSelectedSpriteFromCurrentFrame makes.
void testSpriteCaptureKeepsOriginalMaskAndColourInStep()
{
    ProjectDocument document = makeBigOriginalDocument();
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{});
    const uint16_t colours[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    for (int x = 0; x < 4; ++x) {
        document.setColorizedPixel(0, Resolution::Hd, 101 + x, 41, colours[x]);
    }

    CanvasEditController controller;
    CanvasEditContext context;
    context.document = &document;
    context.currentFrame = 0;
    context.selectedFrames = {0};
    context.spriteCaptureSelection = true;
    context.tool = DrawingTool::Line;
    controller.strokeFinished(context,
                              CanvasHit{CanvasRegion::OriginalFrame, Resolution::Hd, 101, 41},
                              CanvasHit{CanvasRegion::OriginalFrame, Resolution::Hd, 104, 41},
                              false);
    const std::vector<Point> selection = controller.captureSelectionPixels(0);
    expect(selection.size() == 4 && selection.front().x == 101 && selection.front().y == 41 &&
               selection.back().x == 104 && selection.back().y == 41,
           "capture selection is on the native grid");

    // Sprite 1 has no HD data and other SD content before the capture.
    document.replaceSpriteFromFrameSelection(
        1,
        document.originalFrame(0),
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, false),
        selection);
    const document::SpriteRecord& sprite = document.sprite(1);
    const uint8_t expectedOriginal[4] = {1, 2, 3, 0};
    for (int x = 0; x < 4; ++x) {
        const std::size_t index = static_cast<std::size_t>(x);
        expect(sprite.originalSd.pixels[index] == expectedOriginal[x],
               "captured sprite original is the native original pixel");
        expect(sprite.maskHd.pixels[index] != 255, "captured pixel is opaque in the HD mask");
        expect(sprite.colorizedHd.pixels[index] == colours[x],
               "captured colour is the HD colour of the same pixel");
    }
    expect(sprite.hasHd, "a captured sprite has HD data");
    expect(sprite.originalSd.pixels[4] == 255 && sprite.maskHd.pixels[4] == 255 &&
               sprite.originalSd.pixels[document::kMaxSpriteWidth] == 255 &&
               sprite.maskHd.pixels == sprite.originalSd.pixels,
           "the HD mask has the captured shape and nothing else");
    expect(std::all_of(sprite.colorizedSd.pixels.begin(),
                       sprite.colorizedSd.pixels.end(),
                       [](uint16_t value) { return value == 0; }) &&
               sprite.colorizedSd.pixels.size() == document::kMaxSpriteWidth *
                                                       document::kMaxSpriteHeight &&
               std::all_of(sprite.dynamicMaskSd.pixels.begin(),
                           sprite.dynamicMaskSd.pixels.end(),
                           [](uint8_t value) { return value == 255; }),
           "the unused SD sprite data is reset to its neutral content");

    // The captured sprite is found again where it was taken from.
    document.setSpriteDetectionArea(1, 0, document::SpriteDetectionArea{0, 0, 4, 1});
    expect(document.sprite(1).detectionDwords[0] == 0x00030201 &&
               document.sprite(1).detectionDwordPositions[0] == 0,
           "detection dword of the captured original");
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{1, 101, 41, 105, 42});
    document.setColorizedPixel(0, Resolution::Hd, 102, 41, 0);
    expect(colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, false)
                   .pixels[at(kHdFrameSize, 102, 41)] == 0x2222,
           "the captured sprite matches its source frame");

    document.replaceSpriteFromFrame(
        1,
        document.originalFrame(0),
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, false),
        11,
        6,
        1,
        2);
    expect(document.sprite(1).originalSd.pixels[0] == 2 &&
               document.sprite(1).colorizedHd.pixels[0] == kGreen &&
               document.sprite(1).originalSd.pixels[document::kMaxSpriteWidth] == 255 &&
               document.sprite(1).maskHd.pixels[document::kMaxSpriteWidth] == 255,
           "capturing native bounds reads original (x, y) and the composed HD colour; unlit "
           "pixels stay transparent");
}

QToolButton* buttonWithText(QWidget& parent, const QString& text)
{
    for (QToolButton* button : parent.findChildren<QToolButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    expect(false, "button " + text.toStdString());
    return nullptr;
}

QSpinBox* spinBoxWithToolTip(QWidget& parent, const QString& toolTip)
{
    for (QSpinBox* spinBox : parent.findChildren<QSpinBox*>()) {
        if (spinBox->toolTip() == toolTip) {
            return spinBox;
        }
    }
    expect(false, "spin box " + toolTip.toStdString());
    return nullptr;
}

void testMissingHdIsFlaggedAndNotRendered()
{
    ProjectDocument document = makeBigOriginalDocument();
    expect(!document.frameHdGaps(0).any(), "frame 0 is complete");
    expect(document.frameHdGaps(1).frame && !document.frameHdGaps(1).background &&
               document.frameHdGaps(1).sprites.empty(),
           "frame 1 lacks its HD frame");
    const document::FrameHdGaps gaps = document.frameHdGaps(2);
    expect(!gaps.frame && gaps.background && gaps.sprites == std::vector<uint8_t>{1},
           "frame 2 lacks the HD of its background and of sprite 1");

    const document::Rgb565Frame nothing =
        colorizing::app::composeResolvedFrame(document, 1, Resolution::Hd, true);
    expect(nothing.size.width == 256 &&
               std::all_of(nothing.pixels.begin(),
                           nothing.pixels.end(),
                           [](uint16_t value) { return value == 0; }),
           "a frame without HD renders nothing, not its SD frame");
    const document::Rgb565Frame partial =
        colorizing::app::composeResolvedFrame(document, 2, Resolution::Hd, true);
    expect(std::all_of(partial.pixels.begin(),
                       partial.pixels.end(),
                       [](uint16_t value) { return value == kHdColor; }),
           "a background and a sprite without HD are not drawn from their SD data");

    FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);
    const QPoint onPlane = positionOf(widget, CanvasRegion::ColorizedFrame, 3, 3);
    const QPoint onOriginal = positionOf(widget, CanvasRegion::OriginalFrame, 3, 3);
    widget.setFrameIndex(1);
    expect(!widget.hitTestAt(onPlane), "a missing HD frame cannot be painted on");
    const std::optional<CanvasHit> originalHit = widget.hitTestAt(onOriginal);
    expect(originalHit && originalHit->region == CanvasRegion::OriginalFrame,
           "its original can still be used");
    expect(grab(widget).pixelColor(onPlane) == QColor(0, 0, 0),
           "the missing HD frame is shown black");

    const auto thumbnail = [&](std::size_t frameIndex) {
        return colorizing::app::makeFrameThumbnail(document, frameIndex, QSize(160, 84))
            .pixmap(QSize(160, 84))
            .toImage();
    };
    // The badge ends four pixels left of the thumbnail's right edge; its text is centred.
    const QColor warning(190, 45, 45);
    expect(thumbnail(1).pixelColor(155, 4) == warning &&
               thumbnail(2).pixelColor(155, 4) == warning &&
               thumbnail(0).pixelColor(155, 4) != warning,
           "thumbnails of frames the runtime does not show carry a warning badge");
    expect(thumbnail(0).pixelColor(133, 5) != QColor(35, 95, 160),
           "a complete frame has no HD badge: the plane is the only one");

    colorizing::app::FramePanel framePanel;
    const auto refreshFramePanel = [&](std::size_t frameIndex) {
        colorizing::app::FramePanelState state;
        state.document = &document;
        state.frameIndex = frameIndex;
        framePanel.refresh(state);
    };
    QToolButton* create = buttonWithText(framePanel, QStringLiteral("Create HD"));
    QToolButton* remove = buttonWithText(framePanel, QStringLiteral("Remove HD"));
    refreshFramePanel(0);
    expect(create->text() == QStringLiteral("Create 256x64 Frame") && !create->isHidden() &&
               !create->isEnabled() && remove->isHidden(),
           "Remove is hidden; nothing to create on a complete frame");
    refreshFramePanel(1);
    expect(create->isEnabled() && remove->isHidden() &&
               create->toolTip().contains(QStringLiteral("original")),
           "Create 256x64 Frame repairs a frame without its picture");
    refreshFramePanel(2);
    expect(create->isEnabled() && remove->isHidden(),
           "Create 256x64 Frame repairs a background without its picture");
    // The sprite's in the Sprite section.
    colorizing::app::SpritePanel spritePanel;
    colorizing::app::SpritePanelState spriteState;
    spriteState.document = &document;
    spriteState.frameIndex = 0;
    QToolButton* createSprite = buttonWithText(spritePanel, QStringLiteral("Create HD Sprite"));
    QToolButton* removeSprite = buttonWithText(spritePanel, QStringLiteral("Remove HD Sprite"));
    for (std::size_t sprite = 0; sprite < document.spriteCount(); ++sprite) {
        spriteState.spriteIndex = sprite;
        spritePanel.refresh(spriteState);
        expect(createSprite->text() == QStringLiteral("Create 256x64 Sprite") &&
                   !createSprite->isHidden() && removeSprite->isHidden() &&
                   createSprite->isEnabled() == !document.hasHdSprite(sprite),
               "Create 256x64 Sprite repairs a sprite without it; nothing to remove");
    }

    expect(colorizing::app::renderSpriteColorizedPreviewImage(document.sprite(0),
                                                              Resolution::Hd,
                                                              true)
                       .pixelColor(0, 0) == colorizing::app::rgb565ToColor(kCyan) &&
               colorizing::app::renderSpriteColorizedPreviewImage(document.sprite(1),
                                                                  Resolution::Hd,
                                                                  true)
                       .pixelColor(0, 0) == QColor(0, 0, 0) &&
               colorizing::app::renderBackgroundPreviewImage(document.background(1),
                                                             Resolution::Hd,
                                                             document.geometry())
                       .pixelColor(0, 0) == QColor(0, 0, 0),
           "sprite and background previews do not fall back to SD");

    // Repair, as Create HD does.
    document.createHdFrame(1);
    widget.update();
    const std::optional<CanvasHit> repairedHit = widget.hitTestAt(onPlane);
    expect(repairedHit && repairedHit->resolution == Resolution::Hd &&
               !document.frameHdGaps(1).any(),
           "after Create HD the frame can be painted on");
    const std::vector<uint16_t> plain = document::plainOriginalColors(16);
    const document::Rgb565Frame& created = document.colorizedFrame(1, Resolution::Hd);
    expect(created.pixels[at(kHdFrameSize, 10, 7)] == plain[3] &&
               created.pixels[at(kHdFrameSize, 103, 41)] == plain[3] &&
               created.pixels[at(kHdFrameSize, 0, 0)] == plain[0] &&
               document.colorizedFrame(1, Resolution::Sd).pixels[0] == kSdColor,
           "the created HD frame is the native original in its plain colours; SD is kept");
    refreshFramePanel(1);
    expect(!create->isEnabled(), "nothing left to create");
}

void testSpriteBoundsAndSpriteCanvasAreNative()
{
    const ProjectDocument document = makeBigOriginalDocument();
    colorizing::app::SpritePanel panel;
    colorizing::app::SpritePanelState state;
    state.document = &document;
    state.frameIndex = 0;
    state.spriteIndex = 0;
    panel.refresh(state);
    const auto bound = [&](const char* name) {
        return spinBoxWithToolTip(panel,
                                  QStringLiteral("Sprite slot bounding box %1").arg(name));
    };
    QSpinBox* minX = bound("minimum X");
    QSpinBox* maxX = bound("maximum X");
    QSpinBox* minY = bound("minimum Y");
    QSpinBox* maxY = bound("maximum Y");
    expect(minX->maximum() == 255 && maxX->maximum() == 255 && minY->maximum() == 63 &&
               maxY->maximum() == 63 && maxX->value() == 255 && maxY->value() == 63,
           "slot bounds of a 256x64 project range over the native original");
    const ProjectDocument ordinary = ProjectDocument::createEmpty({"ordinary", 1, false});
    state.document = &ordinary;
    state.spriteIndex.reset();
    panel.refresh(state);
    expect(minX->maximum() == 127 && maxX->maximum() == 127 && minY->maximum() == 31 &&
               maxY->maximum() == 31,
           "slot bounds of an ordinary project stay 0..127 and 0..31");

    // Scale 2: the four HD sprite pixels are drawn as large as the four of the original.
    colorizing::app::SpriteEditorWidget editor;
    editor.resize(536, 316);
    editor.setDocument(&document);
    editor.setSpriteIndex(0);
    editor.setActiveResolution(Resolution::Sd);
    QImage image(editor.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        editor.render(&painter);
    }
    int cyan = 0;
    int magenta = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            cyan += image.pixelColor(x, y) == colorizing::app::rgb565ToColor(kCyan) ? 1 : 0;
            magenta += image.pixelColor(x, y) == colorizing::app::rgb565ToColor(kMagenta) ? 1 : 0;
        }
    }
    expect(cyan == 4 * 2 * 2 && magenta == 0,
           "the sprite canvas shows the HD sprite one to one, never the SD sprite");
}

void testMaskEditThroughWidgetAndController()
{
    ProjectDocument document = makeBigOriginalDocument();
    FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);
    widget.setComparisonMaskOverlayEnabled(true);

    std::vector<std::unique_ptr<edit::EditCommand>> commands;
    CanvasEditOutputs outputs;
    outputs.commit = [&](std::unique_ptr<edit::EditCommand> command) {
        command->apply(document);
        commands.push_back(std::move(command));
    };
    outputs.showPreview = [&](CanvasPixelOverlay overlay) {
        widget.setPreviewPixels(overlay.region,
                                overlay.resolution,
                                std::move(overlay.pixels),
                                overlay.color);
    };
    CanvasEditController controller(outputs);
    CanvasEditContext context;
    context.document = &document;
    context.currentFrame = 0;
    context.selectedFrames = {0};
    context.layer = CanvasEditLayer::ComparisonMask;
    context.tool = DrawingTool::Point;

    const RegionLocator position = locate(widget, CanvasRegion::OriginalFrame);
    const QPoint target = positionOf(widget, CanvasRegion::OriginalFrame, 131, 41);
    const std::optional<CanvasHit> hit = widget.hitTestAt(target);
    expect(hit && hit->x == 131 && hit->y == 41, "hit on the original region");
    const QImage before = grab(widget);
    controller.originalPixelClicked(context, *hit, false);
    expect(commands.size() == 1 && document.comparisonMaskPixel(0, 131, 41) == 1 &&
               document.comparisonMaskPixel(0, 65, 20) == 0,
           "the clicked original pixel is the edited mask pixel");
    widget.update();
    const QImage after = grab(widget);
    expect(after.pixelColor(target) != before.pixelColor(target) &&
               after.pixelColor(target).red() > 150,
           "the edited mask pixel is drawn under the click");

    context.tool = DrawingTool::FilledRectangle;
    const CanvasHit start = *widget.hitTestAt(position(240, 60));
    const CanvasHit end = *widget.hitTestAt(position(255, 63));
    controller.strokePreviewed(context, start, end, false);
    const QImage previewed = grab(widget);
    expect(previewed.pixelColor(position(240, 60)) != after.pixelColor(position(240, 60)) &&
               previewed.pixelColor(position(239, 60)) == after.pixelColor(position(239, 60)),
           "the stroke preview is drawn on the original region");
    widget.clearPreviewPixels();
    controller.strokeFinished(context, start, end, false);
    expect(commands.size() == 2 && document.comparisonMaskPixel(0, 240, 60) == 1 &&
               document.comparisonMaskPixel(0, 255, 63) == 1 &&
               document.comparisonMaskPixel(0, 239, 60) == 0,
           "mask shape in original coordinates");

    commands.back()->revert(document);
    commands.pop_back();
    expect(document.comparisonMaskPixel(0, 240, 60) == 0 &&
               document.comparisonMaskPixel(0, 255, 63) == 1,
           "undoing the shape restores the mask, including pixels that were set before");
    commands.back()->revert(document);
    commands.pop_back();
    expect(document.comparisonMaskPixel(0, 131, 41) == 0, "undoing the point");
    widget.update();
    expect(grab(widget) == before, "after undo the canvas is as before");
}

void testThumbnailShowsWholeOriginal()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 4;
    snapshot.romName = "big_original_thumbnail";
    snapshot.geometry =
        colorizing::render::ProjectGeometry(colorizing::render::PlaneLayout::NativeExtra);
    document::IndexedFrame original = document::makeIndexedFrame(kHdFrameSize, 0);
    for (int y = 0; y < kHdFrameSize.height; ++y) {
        for (int x = kHdFrameSize.width / 2; x < kHdFrameSize.width; ++x) {
            original.pixels[at(kHdFrameSize, x, y)] = 3;
        }
    }
    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        original,
        document::makeRgb565Frame(kSdFrameSize, 0),
        std::nullopt,
        0,
        255,
    });
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);

    // The original is the lower of the two images, see makeFrameThumbnail().
    const QImage image = colorizing::app::makeFrameThumbnail(document, 0, QSize(160, 84))
                             .pixmap(QSize(160, 84))
                             .toImage();
    const QColor lit = colorizing::app::originalIndexToColor(3);
    const QColor unlit = colorizing::app::originalIndexToColor(0);
    for (int y = 50; y < 76; y += 5) {
        expect(image.pixelColor(20, y) == unlit && image.pixelColor(70, y) == unlit &&
                   image.pixelColor(90, y) == lit && image.pixelColor(150, y) == lit,
               "the thumbnail shows the 256x64 original over its whole width and height");
    }
}

}  // namespace

void runBigOriginalWidgetTests()
{
    testOriginalRegionShowsEveryOriginalPixel();
    testOverlaysOnOriginalRegion();
    testOnlyTheHdPlaneIsRendered();
    testSpriteIsMatchedOnNativeOriginal();
    testSpriteCaptureKeepsOriginalMaskAndColourInStep();
    testMissingHdIsFlaggedAndNotRendered();
    testSpriteBoundsAndSpriteCanvasAreNative();
    testMaskEditThroughWidgetAndController();
    testThumbnailShowsWholeOriginal();
}
