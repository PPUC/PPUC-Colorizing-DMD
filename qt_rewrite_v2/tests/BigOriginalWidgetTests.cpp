// Rendering, hit testing and editing of a project whose original frames are 256x64.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPixmap>

#include "CanvasEditController.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "FramePreviewWidget.h"
#include "FrameThumbnail.h"

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

document::SpriteRecord makeSprite()
{
    const colorizing::render::Size cell{static_cast<int>(document::kMaxSpriteWidth),
                                        static_cast<int>(document::kMaxSpriteHeight)};
    document::SpriteRecord sprite;
    sprite.hasHd = true;
    sprite.originalSd = document::makeIndexedFrame(cell, 255);
    sprite.colorizedSd = document::makeRgb565Frame(cell, 0);
    sprite.maskHd = document::makeIndexedFrame(cell, 255);
    sprite.colorizedHd = document::makeRgb565Frame(cell, 0);
    for (int x = 0; x < 4; ++x) {
        sprite.originalSd.pixels[static_cast<std::size_t>(x)] =
            static_cast<uint8_t>(x == 3 ? 0 : x + 1);
        sprite.colorizedSd.pixels[static_cast<std::size_t>(x)] = kMagenta;
    }
    for (int x = 0; x < 8; ++x) {
        sprite.maskHd.pixels[static_cast<std::size_t>(x)] = 0;
        sprite.colorizedHd.pixels[static_cast<std::size_t>(x)] = kCyan;
    }
    sprite.detectionDwords = {0x00030201, 0xffffffff, 0xffffffff, 0xffffffff};
    sprite.detectionDwordPositions = {0, 0xffff, 0xffff, 0xffff};
    sprite.detectionAreas = {
        document::SpriteDetectionArea{0, 0, 4, 1},
        document::SpriteDetectionArea{},
        document::SpriteDetectionArea{},
        document::SpriteDetectionArea{},
    };
    return sprite;
}

// One frame. Original (10,6)=1, (11,6)=2, (10,7)=3 and (40,20)=1; row 40 holds 1,3,2,3,3,3,0
// from x=100, which reads 1,2,3,0 on the SD grid. Everything else is 0.
//   dynamic SD: set 2 at (5,3), set 4 (shadow to the right) at (20,10)
//   dynamic HD: set 2 at (11,6) and (10,7)
//   background 0: SD mask at (5,3) and (6,3), HD mask at (10,6) and (12,6)
//   comparison mask 0: (200,50) and (255,63)
//   sprite 0 in slot 0 with the full SD frame as bounds
ProjectDocument makeBigOriginalDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "big_original_preview";
    snapshot.originalIs256x64 = true;

    document::IndexedFrame mask = document::makeIndexedFrame(kHdFrameSize, 0);
    mask.pixels[at(kHdFrameSize, 200, 50)] = 1;
    mask.pixels[at(kHdFrameSize, 255, 63)] = 1;
    snapshot.comparisonMasks.push_back(mask);

    document::IndexedFrame original = document::makeIndexedFrame(kHdFrameSize, 0);
    original.pixels[at(kHdFrameSize, 10, 6)] = 1;
    original.pixels[at(kHdFrameSize, 11, 6)] = 2;
    original.pixels[at(kHdFrameSize, 10, 7)] = 3;
    original.pixels[at(kHdFrameSize, 40, 20)] = 1;
    const uint8_t spriteRow[7] = {1, 3, 2, 3, 3, 3, 0};
    for (int x = 0; x < 7; ++x) {
        original.pixels[at(kHdFrameSize, 100 + x, 40)] = spriteRow[x];
    }

    document::FrameDynamicData dynamicData{
        document::makeIndexedFrame(kSdFrameSize, 255),
        document::makeIndexedFrame(kHdFrameSize, 255),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    dynamicData.maskSd.pixels[at(kSdFrameSize, 5, 3)] = 2;
    dynamicData.maskSd.pixels[at(kSdFrameSize, 20, 10)] = 4;
    dynamicData.maskHd.pixels[at(kHdFrameSize, 11, 6)] = 2;
    dynamicData.maskHd.pixels[at(kHdFrameSize, 10, 7)] = 2;
    dynamicData.colorsSd[2 * 16 + 1] = kRed;
    dynamicData.colorsSd[4 * 16 + 1] = kYellow;
    dynamicData.colorsHd[2 * 16 + 2] = kGreen;
    dynamicData.colorsHd[2 * 16 + 3] = kBlue;
    dynamicData.shadowDirectionsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsSd.assign(document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowDirectionsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsHd.assign(document::kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowDirectionsSd[4] = 0x08;
    dynamicData.shadowColorsSd[4] = kWhite;

    document::FrameBackgroundData backgroundData{
        0,
        document::makeIndexedFrame(kSdFrameSize, 0),
        document::makeIndexedFrame(kHdFrameSize, 0),
    };
    backgroundData.maskSd.pixels[at(kSdFrameSize, 5, 3)] = 1;
    backgroundData.maskSd.pixels[at(kSdFrameSize, 6, 3)] = 1;
    backgroundData.maskHd.pixels[at(kHdFrameSize, 10, 6)] = 1;
    backgroundData.maskHd.pixels[at(kHdFrameSize, 12, 6)] = 1;
    snapshot.backgrounds.push_back(document::BackgroundRecord{
        document::makeRgb565Frame(kSdFrameSize, 0x0410),
        document::makeRgb565Frame(kHdFrameSize, 0x8410),
    });

    snapshot.sprites.push_back(makeSprite());
    std::vector<document::FrameSpriteReference> spriteReferences(document::kMaxSpritesPerFrame);
    spriteReferences[0] = document::FrameSpriteReference{0, 0, 0, 127, 31};

    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        original,
        document::makeRgb565Frame(kSdFrameSize, 0x2104),
        document::makeRgb565Frame(kHdFrameSize, 0x4208),
        0,
        0,
        dynamicData,
        backgroundData,
        spriteReferences,
    });
    std::string error;
    expect(document::isValidProjectSnapshot(snapshot, &error), "fixture is valid: " + error);
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
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

    // The original is displayed as large as the SD frame above it.
    const QPoint sdFirst = positionOf(widget, CanvasRegion::ColorizedFrame, 0, 0);
    const QPoint sdSecond = positionOf(widget, CanvasRegion::ColorizedFrame, 1, 0);
    const QPoint originalFirst = positionOf(widget, CanvasRegion::OriginalFrame, 0, 0);
    const QPoint originalThird = positionOf(widget, CanvasRegion::OriginalFrame, 2, 0);
    expect(sdFirst.x() == originalFirst.x() &&
               sdSecond.x() - sdFirst.x() == originalThird.x() - originalFirst.x(),
           "two original pixels are as wide as one SD pixel");

    const QImage image = grab(widget);
    const RegionLocator position = locate(widget, CanvasRegion::OriginalFrame);
    const auto shown = [&](int x, int y) { return image.pixelColor(position(x, y)); };
    expect(shown(10, 6) == colorizing::app::originalIndexToColor(1) &&
               shown(11, 6) == colorizing::app::originalIndexToColor(2) &&
               shown(10, 7) == colorizing::app::originalIndexToColor(3) &&
               shown(11, 7) == colorizing::app::originalIndexToColor(0) &&
               shown(101, 40) == colorizing::app::originalIndexToColor(3) &&
               shown(5, 3) == colorizing::app::originalIndexToColor(0),
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
    const auto isRedTinted = [](const QColor& color) {
        return color.red() > 150 && color.green() < 80 && color.blue() < 80;
    };

    const QImage plain = grab(widget);
    widget.setComparisonMaskOverlayEnabled(true);
    const QImage masked = grab(widget);
    expect(isRedTinted(masked.pixelColor(position(200, 50))) &&
               isRedTinted(masked.pixelColor(position(255, 63))),
           "comparison mask overlay marks 256x64 mask pixels");
    expect(masked.pixelColor(position(201, 50)) == plain.pixelColor(position(201, 50)) &&
               masked.pixelColor(position(100, 25)) == plain.pixelColor(position(100, 25)) &&
               masked.pixelColor(position(200, 51)) == plain.pixelColor(position(200, 51)),
           "comparison mask overlay marks nothing else");
    widget.setComparisonMaskOverlayEnabled(false);

    // Slot bounds are SD coordinates: 10,5-30,12 covers original 20,10-61,25.
    widget.setSelectedSpriteBoundsOverlayEnabled(true);
    widget.setSelectedSpriteReference(document::FrameSpriteReference{0, 10, 5, 30, 12});
    const QImage bounded = grab(widget);
    const auto changed = [&](int x, int y) {
        return bounded.pixelColor(position(x, y)) != plain.pixelColor(position(x, y));
    };
    expect(changed(20, 10) && changed(61, 25) && changed(61, 10) && changed(20, 25),
           "sprite slot bounds cover the blocks of their SD corners");
    expect(!changed(19, 10) && !changed(20, 9) && !changed(62, 25) && !changed(61, 26),
           "sprite slot bounds end at their SD corners");
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

void testComposedFrameReadsOriginalThroughMapping()
{
    const ProjectDocument document = makeBigOriginalDocument();
    const auto sd = [&](bool background) {
        return colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, background);
    };
    const auto hd = [&](bool background) {
        return colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, background);
    };

    const document::Rgb565Frame sdFrame = sd(false);
    expect(sdFrame.pixels[at(kSdFrameSize, 5, 3)] == kRed,
           "SD dynamic colour comes from original (2x, 2y)");
    expect(sdFrame.pixels[at(kSdFrameSize, 4, 3)] == 0x2104 &&
               sdFrame.pixels[at(kSdFrameSize, 6, 3)] == 0x2104,
           "SD pixels outside the dynamic mask keep their colour");
    expect(sdFrame.pixels[at(kSdFrameSize, 20, 10)] == kYellow &&
               sdFrame.pixels[at(kSdFrameSize, 21, 10)] == kWhite &&
               sdFrame.pixels[at(kSdFrameSize, 19, 10)] == 0x2104,
           "SD dynamic shadow is cast by the lit original pixel (2x, 2y)");
    expect(colorizing::app::resolvedFramePixel(document, 0, Resolution::Sd, 5, 3, false) == kRed,
           "single resolved SD pixel");

    const document::Rgb565Frame hdFrame = hd(false);
    expect(hdFrame.pixels[at(kHdFrameSize, 11, 6)] == kGreen &&
               hdFrame.pixels[at(kHdFrameSize, 10, 7)] == kBlue,
           "HD dynamic colour comes from original (x, y)");
    expect(hdFrame.pixels[at(kHdFrameSize, 10, 6)] == 0x4208 &&
               hdFrame.pixels[at(kHdFrameSize, 11, 7)] == 0x4208,
           "HD pixels outside the dynamic mask keep their colour");

    // The background shows where the original is 0: SD (6,3) reads original (12,6), SD (5,3)
    // reads the lit (10,6); HD (12,6) is unlit, HD (10,6) is lit.
    const document::Rgb565Frame sdBackground = sd(true);
    expect(sdBackground.pixels[at(kSdFrameSize, 6, 3)] == 0x0410 &&
               sdBackground.pixels[at(kSdFrameSize, 5, 3)] == kRed,
           "SD background applies where original (2x, 2y) is unlit");
    const document::Rgb565Frame hdBackground = hd(true);
    expect(hdBackground.pixels[at(kHdFrameSize, 12, 6)] == 0x8410 &&
               hdBackground.pixels[at(kHdFrameSize, 10, 6)] == 0x4208,
           "HD background applies where original (x, y) is unlit");
}

void testSpriteIsMatchedOnSdGrid()
{
    ProjectDocument document = makeBigOriginalDocument();
    const document::Rgb565Frame sdFrame =
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false);
    for (int x = 50; x < 54; ++x) {
        expect(sdFrame.pixels[at(kSdFrameSize, x, 20)] == kMagenta,
               "the sprite is found in the SD view of the original and drawn there");
    }
    expect(sdFrame.pixels[at(kSdFrameSize, 49, 20)] == 0x2104 &&
               sdFrame.pixels[at(kSdFrameSize, 54, 20)] == 0x2104 &&
               sdFrame.pixels[at(kSdFrameSize, 50, 21)] == 0x2104,
           "nothing else is drawn in SD");

    const document::Rgb565Frame hdFrame =
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, false);
    for (int x = 100; x < 108; ++x) {
        expect(hdFrame.pixels[at(kHdFrameSize, x, 40)] == kCyan,
               "the HD sprite is drawn at twice its SD position");
    }
    expect(hdFrame.pixels[at(kHdFrameSize, 99, 40)] == 0x4208 &&
               hdFrame.pixels[at(kHdFrameSize, 108, 40)] == 0x4208,
           "nothing else is drawn in HD");

    // Bounds are SD coordinates as well.
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{0, 60, 0, 127, 31});
    expect(colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false)
                   .pixels[at(kSdFrameSize, 50, 20)] == 0x2104,
           "a sprite outside its SD bounds is not drawn");
}

// The calls MainWindow::captureSelectedSpriteFromCurrentFrame makes.
void testSpriteCaptureKeepsOriginalAndColourInStep()
{
    ProjectDocument document = makeBigOriginalDocument();
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{});
    document.setColorizedPixel(0, Resolution::Sd, 50, 20, 0x1111);
    document.setColorizedPixel(0, Resolution::Sd, 51, 20, 0x2222);
    document.setColorizedPixel(0, Resolution::Sd, 52, 20, 0x3333);
    document.setColorizedPixel(0, Resolution::Sd, 53, 20, 0x4444);

    CanvasEditController controller;
    CanvasEditContext context;
    context.document = &document;
    context.currentFrame = 0;
    context.selectedFrames = {0};
    context.spriteCaptureSelection = true;
    context.tool = DrawingTool::FilledRectangle;
    // Original 100,40-107,41 touches SD 50,20-53,20.
    controller.strokeFinished(context,
                              CanvasHit{CanvasRegion::OriginalFrame, Resolution::Hd, 100, 40},
                              CanvasHit{CanvasRegion::OriginalFrame, Resolution::Hd, 107, 41},
                              false);
    const std::vector<Point> selection = controller.captureSelectionPixels(0);
    expect(selection.size() == 4 && selection.front().x == 50 && selection.front().y == 20 &&
               selection.back().x == 53 && selection.back().y == 20,
           "capture selection is on the SD grid");

    document.replaceSpriteFromFrameSelection(
        0,
        document.originalFrameAtSd(0),
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false),
        selection);
    const document::SpriteRecord& sprite = document.sprite(0);
    const uint8_t expectedOriginal[4] = {1, 2, 3, 0};
    const uint16_t expectedColour[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    for (int x = 0; x < 4; ++x) {
        expect(sprite.originalSd.pixels[static_cast<std::size_t>(x)] == expectedOriginal[x],
               "captured original pixel is original (2x, 2y) of its SD pixel");
        expect(sprite.colorizedSd.pixels[static_cast<std::size_t>(x)] == expectedColour[x],
               "captured colour is the SD colour of the same SD pixel");
    }
    expect(sprite.originalSd.pixels[4] == 255 &&
               sprite.originalSd.pixels[document::kMaxSpriteWidth] == 255,
           "nothing else is captured");

    // The captured sprite is found again where it was taken from.
    document.setSpriteDetectionArea(0, 0, document::SpriteDetectionArea{0, 0, 4, 1});
    document.setFrameSpriteReference(0, 0, document::FrameSpriteReference{0, 40, 10, 60, 30});
    document.setColorizedPixel(0, Resolution::Sd, 51, 20, 0);
    expect(colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false)
                   .pixels[at(kSdFrameSize, 51, 20)] == 0x2222,
           "the captured sprite matches its source frame");

    document.replaceSpriteFromFrame(
        0,
        document.originalFrameAtSd(0),
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false),
        5,
        3,
        1,
        1);
    expect(document.sprite(0).originalSd.pixels[0] == 1 &&
               document.sprite(0).colorizedSd.pixels[0] == kRed,
           "capturing SD bounds reads original (2x, 2y) and the composed SD colour");
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
    snapshot.originalIs256x64 = true;
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
    testComposedFrameReadsOriginalThroughMapping();
    testSpriteIsMatchedOnSdGrid();
    testSpriteCaptureKeepsOriginalAndColourInStep();
    testMaskEditThroughWidgetAndController();
    testThumbnailShowsWholeOriginal();
}
