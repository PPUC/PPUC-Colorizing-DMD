#include "render/OriginalFrameMapping.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/DrawingTools.h"
#include "edit/EditCommand.h"
#include "render/CanvasLayout.h"

using colorizing::render::CanvasHit;
using colorizing::render::CanvasLayout;
using colorizing::render::CanvasLayoutOptions;
using colorizing::render::CanvasRegion;
using colorizing::render::CanvasRegionLayout;
using colorizing::render::OriginalFrameMapping;
using colorizing::render::Point;
using colorizing::render::Rect;
using colorizing::render::Resolution;
using colorizing::render::Size;
using colorizing::render::kHdFrameSize;
using colorizing::render::kSdFrameSize;

namespace document = colorizing::document;
namespace edit = colorizing::edit;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: original frame mapping: " << message << '\n';
        std::exit(1);
    }
}

bool isPoint(Point point, int x, int y)
{
    return point.x == x && point.y == y;
}

bool isRect(Rect rect, int x, int y, int width, int height)
{
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

void testOrdinaryOriginal()
{
    const OriginalFrameMapping mapping;
    expect(mapping.originalSize().width == 128 && mapping.originalSize().height == 32,
           "default original size");
    expect(mapping.originalResolution() == Resolution::Sd, "128x32 original is on the SD grid");
    expect(mapping.originalDisplayScale() == 2, "128x32 original display scale");

    expect(isPoint(mapping.toOriginal(Resolution::Sd, Point{127, 31}), 127, 31),
           "SD maps one to one");
    expect(isPoint(mapping.toOriginal(Resolution::Hd, Point{255, 63}), 127, 31) &&
               isPoint(mapping.toOriginal(Resolution::Hd, Point{11, 6}), 5, 3) &&
               isPoint(mapping.toOriginal(Resolution::Hd, Point{10, 7}), 5, 3),
           "HD pixels share the original pixel of their 2x2 block");
    expect(isPoint(mapping.fromOriginal(Resolution::Sd, Point{5, 3}), 5, 3) &&
               isPoint(mapping.fromOriginal(Resolution::Hd, Point{5, 3}), 10, 6),
           "original to frame pixels");
    expect(isPoint(mapping.toOriginal(Resolution::Hd, Point{-1, -3}), -1, -2),
           "HD pixels outside the frame stay outside");
    expect(isRect(mapping.originalBlock(Resolution::Sd, Point{5, 3}), 5, 3, 1, 1) &&
               isRect(mapping.originalBlock(Resolution::Hd, Point{11, 7}), 5, 3, 1, 1),
           "every frame pixel stands for one original pixel");
    expect(isRect(mapping.originalBlocks(Resolution::Sd, Point{10, 5}, Point{30, 12}),
                  10,
                  5,
                  21,
                  8),
           "SD range covers the same original range");
}

void testBigOriginal()
{
    const OriginalFrameMapping mapping(kHdFrameSize);
    expect(mapping.originalSize().width == 256 && mapping.originalSize().height == 64,
           "256x64 original size");
    expect(mapping.originalResolution() == Resolution::Hd, "256x64 original is on the HD grid");
    expect(mapping.originalDisplayScale() == 1, "256x64 original display scale");

    expect(isPoint(mapping.toOriginal(Resolution::Sd, Point{0, 0}), 0, 0) &&
               isPoint(mapping.toOriginal(Resolution::Sd, Point{5, 3}), 10, 6) &&
               isPoint(mapping.toOriginal(Resolution::Sd, Point{127, 31}), 254, 62),
           "SD shows every second original pixel");
    expect(isPoint(mapping.toOriginal(Resolution::Hd, Point{255, 63}), 255, 63),
           "HD maps one to one");
    expect(isPoint(mapping.fromOriginal(Resolution::Sd, Point{10, 6}), 5, 3) &&
               isPoint(mapping.fromOriginal(Resolution::Sd, Point{11, 7}), 5, 3) &&
               isPoint(mapping.fromOriginal(Resolution::Sd, Point{255, 63}), 127, 31) &&
               isPoint(mapping.fromOriginal(Resolution::Hd, Point{11, 7}), 11, 7),
           "original to frame pixels");
    expect(isRect(mapping.originalBlock(Resolution::Sd, Point{5, 3}), 10, 6, 2, 2) &&
               isRect(mapping.originalBlock(Resolution::Hd, Point{5, 3}), 5, 3, 1, 1),
           "an SD pixel stands for a 2x2 block");
    expect(isRect(mapping.originalBlocks(Resolution::Sd, Point{10, 5}, Point{30, 12}),
                  20,
                  10,
                  42,
                  16) &&
               isRect(mapping.originalBlocks(Resolution::Sd, Point{0, 0}, Point{127, 31}),
                      0,
                      0,
                      256,
                      64),
           "SD range covers the blocks of both corners");

    expect(isPoint(mapping.fromOriginal(Resolution::Sd, Point{-1, -2}), -1, -1) &&
               isPoint(mapping.fromOriginal(Resolution::Sd, Point{256, 64}), 128, 32),
           "original pixels outside the frame stay outside");

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            const Point there = mapping.toOriginal(Resolution::Sd, Point{x, y});
            expect(isPoint(mapping.fromOriginal(Resolution::Sd, there), x, y),
                   "SD round trip");
        }
    }
}

void testUnsupportedSizes()
{
    expect(OriginalFrameMapping::isSupportedOriginalSize(kSdFrameSize) &&
               OriginalFrameMapping::isSupportedOriginalSize(kHdFrameSize) &&
               !OriginalFrameMapping::isSupportedOriginalSize(Size{192, 64}) &&
               !OriginalFrameMapping::isSupportedOriginalSize(Size{256, 32}) &&
               !OriginalFrameMapping::isSupportedOriginalSize(Size{}),
           "supported original sizes");
    bool thrown = false;
    try {
        const OriginalFrameMapping mapping(Size{192, 64});
        (void)mapping;
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    expect(thrown, "other sizes are refused");
}

void testLayoutShowsBigOriginalAtSdDisplaySize()
{
    CanvasLayoutOptions ordinaryOptions;
    CanvasLayoutOptions options;
    options.originalSize = kHdFrameSize;
    const CanvasLayout ordinary = CanvasLayout::buildFrameLayout(ordinaryOptions);
    const CanvasLayout layout = CanvasLayout::buildFrameLayout(options);

    const std::optional<CanvasRegionLayout> region = layout.region(CanvasRegion::OriginalFrame);
    const std::optional<CanvasRegionLayout> ordinaryRegion =
        ordinary.region(CanvasRegion::OriginalFrame);
    expect(region && ordinaryRegion, "both layouts have an original region");
    expect(region->logicalSize.width == 256 && region->logicalSize.height == 64 &&
               region->displayScale == 1 && region->resolution == Resolution::Hd,
           "256x64 original region");
    expect(ordinaryRegion->logicalSize.width == 128 && ordinaryRegion->logicalSize.height == 32 &&
               ordinaryRegion->displayScale == 2 && ordinaryRegion->resolution == Resolution::Sd,
           "128x32 original region is unchanged");
    expect(region->displayRect.x == ordinaryRegion->displayRect.x &&
               region->displayRect.y == ordinaryRegion->displayRect.y &&
               region->displayRect.width == ordinaryRegion->displayRect.width &&
               region->displayRect.height == ordinaryRegion->displayRect.height &&
               layout.displaySize().width == ordinary.displaySize().width &&
               layout.displaySize().height == ordinary.displaySize().height,
           "both originals are displayed at the same place and size");

    const std::optional<CanvasHit> hit = layout.hitTest(Point{255, 72 + 63});
    expect(hit && hit->region == CanvasRegion::OriginalFrame && hit->resolution == Resolution::Hd &&
               hit->x == 255 && hit->y == 63,
           "hits on a 256x64 original are original coordinates");
    const std::optional<CanvasHit> ordinaryHit = ordinary.hitTest(Point{255, 72 + 63});
    expect(ordinaryHit && ordinaryHit->resolution == Resolution::Sd && ordinaryHit->x == 127 &&
               ordinaryHit->y == 31,
           "hits on a 128x32 original are unchanged");
    const std::optional<CanvasHit> colorHit = layout.hitTest(Point{255, 63});
    expect(colorHit && colorHit->region == CanvasRegion::ColorizedFrame &&
               colorHit->resolution == Resolution::Sd && colorHit->x == 127 && colorHit->y == 31,
           "the colorized SD region does not depend on the original size");
}

document::ProjectDocument makeBigOriginalDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "big_original";
    snapshot.originalIs256x64 = true;
    snapshot.comparisonMasks.push_back(document::makeIndexedFrame(kHdFrameSize, 0));
    document::IndexedFrame original = document::makeIndexedFrame(kHdFrameSize, 0);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 256; ++x) {
            original.pixels[static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x)] =
                static_cast<uint8_t>((x + 3 * y) % 16);
        }
    }
    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        original,
        document::makeRgb565Frame(kSdFrameSize, 0),
        document::makeRgb565Frame(kHdFrameSize, 0),
        0,
        0,
    });
    std::string error;
    expect(document::isValidProjectSnapshot(snapshot, &error), "fixture is valid: " + error);
    return document::ProjectDocument::fromSnapshot(snapshot);
}

void testOriginalFrameAtSd()
{
    const document::ProjectDocument document = makeBigOriginalDocument();
    const document::IndexedFrame sd = document.originalFrameAtSd(0);
    expect(sd.size.width == 128 && sd.size.height == 32 && sd.pixels.size() == 128 * 32,
           "SD view of a 256x64 original is 128x32");
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            expect(sd.pixels[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)] ==
                       static_cast<uint8_t>((2 * x + 6 * y) % 16),
                   "SD view samples original (2x, 2y)");
        }
    }

    const document::ProjectDocument ordinary =
        document::ProjectDocument::createEmpty({"ordinary", 1, false});
    const document::IndexedFrame same = ordinary.originalFrameAtSd(0);
    expect(same.size.width == 128 && same.pixels == ordinary.originalFrame(0).pixels,
           "SD view of a 128x32 original is the original");
}

void testComparisonMaskEditsCoverBigOriginal()
{
    document::ProjectDocument document = makeBigOriginalDocument();
    document.markClean();
    const edit::EditLayer layer{edit::EditKind::ComparisonMask, 0, Resolution::Sd};
    const std::vector<edit::MaskPixelChange> changes =
        edit::makeMaskChanges(document, layer, {{200, 50}, {255, 63}, {0, 0}}, 1);
    expect(changes.size() == 3, "mask changes beyond 128x32");
    const edit::MaskEditCommand command(layer.kind, layer.index, layer.resolution, changes);
    command.apply(document);
    expect(document.comparisonMaskPixel(0, 200, 50) == 1 &&
               document.comparisonMaskPixel(0, 255, 63) == 1 &&
               document.comparisonMaskPixel(0, 0, 0) == 1 &&
               document.comparisonMaskPixel(0, 100, 25) == 0,
           "mask command edits 256x64 coordinates");
    command.revert(document);
    expect(document.comparisonMaskPixel(0, 200, 50) == 0 &&
               document.comparisonMaskPixel(0, 255, 63) == 0 &&
               document.comparisonMaskPixel(0, 0, 0) == 0,
           "mask command reverts 256x64 coordinates");

    const std::vector<Point> filled =
        edit::floodFillMatching(document.comparisonMask(0).pixels,
                                document.comparisonMask(0).size,
                                Point{130, 40},
                                uint8_t{0});
    expect(filled.size() == 256 * 64, "flood fill covers the whole 256x64 mask");
}

}  // namespace

void runOriginalFrameMappingTests()
{
    testOrdinaryOriginal();
    testBigOriginal();
    testUnsupportedSizes();
    testLayoutShowsBigOriginalAtSdDisplaySize();
    testOriginalFrameAtSd();
    testComparisonMaskEditsCoverBigOriginal();
}
