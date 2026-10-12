#include "edit/DrawingGeometry.h"
#include "edit/DrawingTools.h"
#include "edit/FramePixelSelection.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "render/CanvasLayout.h"

using colorizing::document::BackgroundRecord;
using colorizing::document::FrameBackgroundData;
using colorizing::document::FrameDynamicData;
using colorizing::document::IndexedFrame;
using colorizing::document::PaletteRecord;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::Rgb565Frame;
using colorizing::document::SpriteDetectionArea;
using colorizing::document::SpriteRecord;
using colorizing::document::kMaxDynamicSetsPerFrame;
using colorizing::document::kMaxSpriteHeight;
using colorizing::document::kMaxSpriteWidth;
using colorizing::document::kPaletteColorCount;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::edit::EditKind;
using colorizing::edit::EditLayer;
using colorizing::edit::MaskPixelChange;
using colorizing::edit::PaletteColorChange;
using colorizing::edit::PaletteGradient;
using colorizing::edit::PaletteGradientDirection;
using colorizing::edit::PaletteGradientPattern;
using colorizing::edit::PixelChange;
using colorizing::render::Point;
using colorizing::render::Resolution;
using colorizing::render::Size;
using colorizing::render::kHdFrameSize;
using colorizing::render::kSdFrameSize;

namespace edit = colorizing::edit;

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

template <typename ExceptionType, typename Func>
void expectThrows(Func func, const std::string& message)
{
    try {
        func();
    } catch (const ExceptionType&) {
        return;
    } catch (const std::exception& ex) {
        fail(message + " threw different exception: " + ex.what());
    }
    fail(message + " did not throw");
}

void expectNear(double actual, double expected, const std::string& message)
{
    if (std::abs(actual - expected) > 1e-9) {
        fail(message + ": expected " + std::to_string(expected) + ", got " +
             std::to_string(actual));
    }
}

bool samePoints(const std::vector<Point>& left, const std::vector<Point>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].x != right[index].x || left[index].y != right[index].y) {
            return false;
        }
    }
    return true;
}

std::set<std::pair<int, int>> pointSet(const std::vector<Point>& points)
{
    std::set<std::pair<int, int>> result;
    for (const Point& point : points) {
        result.insert({point.x, point.y});
    }
    return result;
}

bool sameChanges(const std::vector<PixelChange>& left, const std::vector<PixelChange>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].x != right[index].x || left[index].y != right[index].y ||
            left[index].oldColor != right[index].oldColor ||
            left[index].newColor != right[index].newColor) {
            return false;
        }
    }
    return true;
}

std::size_t spritePixelIndex(int x, int y)
{
    return static_cast<std::size_t>(y) * kMaxSpriteWidth + static_cast<std::size_t>(x);
}

// Sprite shape used by makeDocument: everything outside this rectangle is transparent.
constexpr int kSpriteShapeLeft = 2;
constexpr int kSpriteShapeTop = 1;
constexpr int kSpriteShapeRight = 9;
constexpr int kSpriteShapeBottom = 6;

IndexedFrame makeSpriteShape()
{
    IndexedFrame shape = makeIndexedFrame(
        {static_cast<int>(kMaxSpriteWidth), static_cast<int>(kMaxSpriteHeight)}, 255);
    for (int y = kSpriteShapeTop; y <= kSpriteShapeBottom; ++y) {
        for (int x = kSpriteShapeLeft; x <= kSpriteShapeRight; ++x) {
            shape.pixels[spritePixelIndex(x, y)] = 0;
        }
    }
    return shape;
}

// Two frames (only the first has HD, dynamic and background data), one comparison mask,
// background, palette and an HD sprite whose shape is a small rectangle.
ProjectDocument makeDocument()
{
    ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "drawing_tools";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(kSdFrameSize, 0),
        makeRgb565Frame(kSdFrameSize, 0),
        makeRgb565Frame(kHdFrameSize, 0),
        0,
        0,
        FrameDynamicData{
            makeIndexedFrame(kSdFrameSize, 255),
            makeIndexedFrame(kHdFrameSize, 255),
            std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0),
            std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0),
        },
        FrameBackgroundData{
            0,
            makeIndexedFrame(kSdFrameSize, 0),
            makeIndexedFrame(kHdFrameSize, 0),
        },
    });
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(kSdFrameSize, 0),
        makeRgb565Frame(kSdFrameSize, 0),
        std::nullopt,
        0,
        0,
        std::nullopt,
        std::nullopt,
    });
    snapshot.comparisonMasks.push_back(makeIndexedFrame(kSdFrameSize, 0));
    snapshot.backgrounds.push_back(BackgroundRecord{
        makeRgb565Frame(kSdFrameSize, 0x5050),
        makeRgb565Frame(kHdFrameSize, 0x6060),
    });
    const Size spriteSize{static_cast<int>(kMaxSpriteWidth), static_cast<int>(kMaxSpriteHeight)};
    snapshot.sprites.push_back(SpriteRecord{
        true,
        makeSpriteShape(),
        makeRgb565Frame(spriteSize, 0),
        makeSpriteShape(),
        makeRgb565Frame(spriteSize, 0),
        {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
        {0xffff, 0xffff, 0xffff, 0xffff},
        {{SpriteDetectionArea{}, SpriteDetectionArea{}, SpriteDetectionArea{},
          SpriteDetectionArea{}}},
    });
    PaletteRecord palette;
    palette.colors.assign(kPaletteColorCount, 0);
    snapshot.palettes.push_back(palette);

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

// The flood fill MainWindow used before it moved to core; kept as the reference the shared
// implementation must reproduce pixel for pixel and in the same order.
template <typename Value>
std::vector<Point> legacyFloodFill(const std::vector<Value>& values,
                                   Size size,
                                   Point seed,
                                   Value target)
{
    std::vector<uint8_t> visited(values.size(), 0);
    std::deque<Point> queue;
    std::vector<Point> pixels;
    queue.push_back(seed);

    while (!queue.empty()) {
        const Point point = queue.front();
        queue.pop_front();
        if (point.x < 0 || point.y < 0 || point.x >= size.width || point.y >= size.height) {
            continue;
        }
        const std::size_t index =
            static_cast<std::size_t>(point.y) * static_cast<std::size_t>(size.width) +
            static_cast<std::size_t>(point.x);
        if (visited[index]) {
            continue;
        }
        visited[index] = 1;
        if (values[index] != target) {
            continue;
        }

        pixels.push_back(point);
        queue.push_back(Point{point.x - 1, point.y});
        queue.push_back(Point{point.x + 1, point.y});
        queue.push_back(Point{point.x, point.y - 1});
        queue.push_back(Point{point.x, point.y + 1});
    }
    return pixels;
}

// MainWindow's former sprite flood fill, without its "already the draw color" early return.
std::vector<Point> legacySpriteFloodFill(const ProjectDocument& document,
                                         std::size_t spriteIndex,
                                         Resolution resolution,
                                         int x,
                                         int y)
{
    const SpriteRecord& sprite = document.sprite(spriteIndex);
    const bool useHd = resolution == Resolution::Hd && sprite.hasHd;
    const IndexedFrame& mask = useHd ? sprite.maskHd : sprite.originalSd;
    const Rgb565Frame& frame = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    const uint16_t targetColor = document.spriteColorizedPixel(spriteIndex, resolution, x, y);
    if (targetColor == 0 || mask.pixels[spritePixelIndex(x, y)] == 255) {
        return {};
    }

    std::vector<uint8_t> visited(frame.pixels.size(), 0);
    std::deque<Point> queue;
    std::vector<Point> pixels;
    queue.push_back(Point{x, y});

    while (!queue.empty()) {
        const Point point = queue.front();
        queue.pop_front();
        if (point.x < 0 || point.y < 0 ||
            point.x >= frame.size.width || point.y >= frame.size.height) {
            continue;
        }
        const std::size_t index = spritePixelIndex(point.x, point.y);
        if (visited[index]) {
            continue;
        }
        visited[index] = 1;
        if (mask.pixels[index] == 255 || frame.pixels[index] != targetColor) {
            continue;
        }

        pixels.push_back(point);
        queue.push_back(Point{point.x - 1, point.y});
        queue.push_back(Point{point.x + 1, point.y});
        queue.push_back(Point{point.x, point.y - 1});
        queue.push_back(Point{point.x, point.y + 1});
    }
    return pixels;
}

// The loop every MainWindow commit function repeated, here for colorized frame pixels.
std::vector<PixelChange> legacyFramePixelChanges(const ProjectDocument& document,
                                                 std::size_t frameIndex,
                                                 Resolution resolution,
                                                 const std::vector<Point>& pixels,
                                                 uint16_t drawColor)
{
    std::vector<PixelChange> changes;
    std::set<std::pair<int, int>> seen;
    for (const Point& pixel : pixels) {
        if (!seen.insert({pixel.x, pixel.y}).second) {
            continue;
        }
        const uint16_t oldColor = document.colorizedPixel(frameIndex, resolution, pixel.x, pixel.y);
        if (oldColor == drawColor) {
            continue;
        }
        changes.push_back(PixelChange{pixel.x, pixel.y, oldColor, drawColor});
    }
    return changes;
}

// Parses rows of equal length into a row-major buffer.
std::vector<uint8_t> gridValues(const std::vector<std::string>& rows)
{
    std::vector<uint8_t> values;
    for (const std::string& row : rows) {
        for (char cell : row) {
            values.push_back(static_cast<uint8_t>(cell));
        }
    }
    return values;
}

void testLineGeometry()
{
    const std::vector<Point> single = edit::makeLinePixels({3, 4}, {3, 4});
    expect(samePoints(single, {{3, 4}}), "zero-length line is one pixel");

    const std::vector<Point> horizontal = edit::makeLinePixels({1, 2}, {4, 2});
    expect(samePoints(horizontal, {{1, 2}, {2, 2}, {3, 2}, {4, 2}}), "horizontal line pixels");

    const std::vector<Point> diagonal = edit::makeLinePixels({3, 3}, {0, 0});
    expect(samePoints(diagonal, {{3, 3}, {2, 2}, {1, 1}, {0, 0}}),
           "diagonal line runs from start to end");

    const std::vector<Point> shallow = edit::makeLinePixels({0, 0}, {6, 2});
    expect(shallow.size() == 7, "shallow line has one pixel per major-axis step");
    expect(shallow.front().x == 0 && shallow.front().y == 0, "line starts at start");
    expect(shallow.back().x == 6 && shallow.back().y == 2, "line ends at end");
    for (std::size_t index = 1; index < shallow.size(); ++index) {
        expect(std::abs(shallow[index].x - shallow[index - 1].x) <= 1 &&
                   std::abs(shallow[index].y - shallow[index - 1].y) <= 1,
               "line pixels are 8-connected");
    }
}

void testRectangleGeometry()
{
    const std::vector<Point> outline = edit::makeRectanglePixels({5, 4}, {2, 2});
    expect(outline.size() == 10, "4x3 outline has ten distinct pixels");
    expect(pointSet(outline).size() == outline.size(), "outline has no duplicates");
    expect(pointSet(outline).count({3, 3}) == 0 && pointSet(outline).count({4, 3}) == 0,
           "outline leaves the interior alone");

    const std::vector<Point> filled = edit::makeFilledRectanglePixels({5, 4}, {2, 2});
    expect(filled.size() == 12, "4x3 filled rectangle pixel count");
    expect(filled.front().x == 2 && filled.front().y == 2, "filled rectangle starts top-left");
    expect(filled.back().x == 5 && filled.back().y == 4, "filled rectangle ends bottom-right");

    expect(samePoints(edit::makeRectanglePixels({7, 7}, {7, 7}), {{7, 7}}),
           "degenerate outline is one pixel");
    expect(samePoints(edit::makeFilledRectanglePixels({7, 7}, {7, 7}), {{7, 7}}),
           "degenerate filled rectangle is one pixel");
}

void testEllipseGeometry()
{
    const std::vector<Point> filled = edit::makeFilledEllipsePixels({0, 0}, {8, 4});
    const std::set<std::pair<int, int>> filledSet = pointSet(filled);
    expect(filledSet.size() == filled.size(), "filled ellipse has no duplicates");
    expect(filledSet.count({4, 2}) == 1, "filled ellipse contains its center");
    expect(filledSet.count({0, 2}) == 1 && filledSet.count({8, 2}) == 1 &&
               filledSet.count({4, 0}) == 1 && filledSet.count({4, 4}) == 1,
           "filled ellipse touches its bounding box");
    expect(filledSet.count({0, 0}) == 0 && filledSet.count({8, 4}) == 0,
           "filled ellipse leaves the box corners out");
    for (const Point& point : filled) {
        expect(point.x >= 0 && point.x <= 8 && point.y >= 0 && point.y <= 4,
               "filled ellipse stays inside its bounding box");
        expect(filledSet.count({8 - point.x, 4 - point.y}) == 1, "filled ellipse is symmetric");
    }

    const std::vector<Point> outline = edit::makeEllipsePixels({8, 4}, {0, 0});
    const std::set<std::pair<int, int>> outlineSet = pointSet(outline);
    expect(!outline.empty() && outline.size() < filled.size(), "outline is a strict subset");
    expect(outlineSet.count({4, 2}) == 0, "outline leaves the center out");
    for (const Point& point : outline) {
        expect(filledSet.count({point.x, point.y}) == 1, "outline pixels belong to the ellipse");
    }

    expect(samePoints(edit::makeFilledEllipsePixels({3, 3}, {3, 3}), {{3, 3}}),
           "degenerate filled ellipse is one pixel");
    expect(samePoints(edit::makeEllipsePixels({3, 3}, {3, 3}), {{3, 3}}),
           "degenerate ellipse outline is one pixel");
}

void testCircleGeometry()
{
    const Point constrained = edit::squareConstrainedEnd({10, 10}, {18, 13});
    expect(constrained.x == 13 && constrained.y == 13, "square constraint uses the shorter side");
    const Point upLeft = edit::squareConstrainedEnd({10, 10}, {4, 1});
    expect(upLeft.x == 4 && upLeft.y == 4, "square constraint keeps the drag direction");
    const Point flat = edit::squareConstrainedEnd({10, 10}, {20, 10});
    expect(flat.x == 10 && flat.y == 10, "square constraint collapses a flat drag");

    expect(samePoints(edit::makeCirclePixels({10, 10}, {18, 13}),
                      edit::makeEllipsePixels({10, 10}, {13, 13})),
           "circle is the ellipse of the constrained square");
    expect(samePoints(edit::makeFilledCirclePixels({10, 10}, {4, 1}),
                      edit::makeFilledEllipsePixels({10, 10}, {4, 4})),
           "filled circle is the filled ellipse of the constrained square");
}

void testFloodFillRegionAndBorders()
{
    const Size size{6, 4};
    const std::vector<uint8_t> values = gridValues({
        "aaab.a",
        "abab.a",
        "aaab..",
        "bbbbaa",
    });

    const std::vector<Point> region =
        edit::floodFillMatching(values, size, Point{0, 0}, uint8_t{'a'});
    expect(pointSet(region) ==
               std::set<std::pair<int, int>>{
                   {0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}},
           "fill takes the connected region only");
    expect(region.front().x == 0 && region.front().y == 0, "fill starts with the seed");
    expect(pointSet(region).size() == region.size(), "fill reports each pixel once");

    const std::vector<Point> hole =
        edit::floodFillMatching(values, size, Point{1, 1}, uint8_t{'b'});
    expect(samePoints(hole, {{1, 1}}), "enclosed pixel fills alone");

    const std::vector<Point> corner =
        edit::floodFillMatching(values, size, Point{5, 0}, uint8_t{'a'});
    expect(pointSet(corner) == std::set<std::pair<int, int>>{{5, 0}, {5, 1}},
           "fill stops at the canvas border");

    const std::vector<Point> dots =
        edit::floodFillMatching(values, size, Point{4, 0}, uint8_t{'.'});
    expect(pointSet(dots) == std::set<std::pair<int, int>>{{4, 0}, {4, 1}, {4, 2}, {5, 2}},
           "fill follows a bent region");
}

void testFloodFillConnectivity()
{
    const Size size{3, 3};
    const std::vector<uint8_t> values = gridValues({
        "a.a",
        ".a.",
        "a.a",
    });
    expect(samePoints(edit::floodFillMatching(values, size, Point{1, 1}, uint8_t{'a'}), {{1, 1}}),
           "diagonal neighbours are not connected");
    expect(samePoints(edit::floodFillMatching(values, size, Point{1, 0}, uint8_t{'.'}), {{1, 0}}),
           "diagonal neighbours of the other value are not connected either");

    const std::vector<Point> order =
        edit::floodFillMatching(std::vector<uint8_t>(9, 1), size, Point{1, 1}, uint8_t{1});
    expect(samePoints(order,
                      {{1, 1}, {0, 1}, {2, 1}, {1, 0}, {1, 2}, {0, 0}, {0, 2}, {2, 0}, {2, 2}}),
           "fill visits left, right, up, down breadth-first");
}

void testFloodFillNoOps()
{
    const Size size{4, 2};
    const std::vector<uint8_t> values(8, 3);
    expect(edit::floodFillMatching(values, size, Point{0, 0}, uint8_t{9}).empty(),
           "seed with another value fills nothing");
    expect(edit::floodFillMatching(values, size, Point{-1, 0}, uint8_t{3}).empty(),
           "seed left of the canvas fills nothing");
    expect(edit::floodFillMatching(values, size, Point{4, 0}, uint8_t{3}).empty(),
           "seed right of the canvas fills nothing");
    expect(edit::floodFillMatching(values, size, Point{0, 2}, uint8_t{3}).empty(),
           "seed below the canvas fills nothing");
    expect(edit::floodFillMatching(values, size, Point{3, 1}, uint8_t{3}).size() == 8,
           "uniform canvas fills completely");

    int calls = 0;
    expect(edit::floodFillRegion(Size{0, 0}, Point{0, 0}, [&calls](Point) {
               ++calls;
               return true;
           }).empty(),
           "empty canvas fills nothing");
    expect(calls == 0, "empty canvas never asks the predicate");
}

void testFloodFillPredicateContract()
{
    const Size size{5, 4};
    std::vector<int> calls(20, 0);
    const std::vector<Point> region = edit::floodFillRegion(size, Point{2, 2}, [&](Point point) {
        expect(point.x >= 0 && point.y >= 0 && point.x < size.width && point.y < size.height,
               "predicate only sees pixels inside the canvas");
        ++calls[static_cast<std::size_t>(point.y * size.width + point.x)];
        return point.x != 0;
    });
    expect(region.size() == 16, "predicate decides the region");
    for (int count : calls) {
        expect(count == 1, "predicate is asked exactly once per reachable pixel");
    }
}

void testFloodFillMatchesLegacyAlgorithm()
{
    std::mt19937 random(20240611);
    for (int round = 0; round < 400; ++round) {
        const Size size{1 + static_cast<int>(random() % 24), 1 + static_cast<int>(random() % 12)};
        const unsigned colorCount = 1 + random() % 3;
        std::vector<uint16_t> values(static_cast<std::size_t>(size.width * size.height));
        for (uint16_t& value : values) {
            value = static_cast<uint16_t>(0x1000 + random() % colorCount);
        }
        const Point seed{static_cast<int>(random() % static_cast<unsigned>(size.width + 2)) - 1,
                         static_cast<int>(random() % static_cast<unsigned>(size.height + 2)) - 1};
        const uint16_t target = static_cast<uint16_t>(0x1000 + random() % colorCount);

        expect(samePoints(edit::floodFillMatching(values, size, seed, target),
                          legacyFloodFill(values, size, seed, target)),
               "flood fill matches the former MainWindow algorithm, round " +
                   std::to_string(round));
    }
}

void testSpriteFloodFill()
{
    ProjectDocument document = makeDocument();
    for (int y = kSpriteShapeTop; y <= kSpriteShapeBottom; ++y) {
        for (int x = kSpriteShapeLeft; x <= kSpriteShapeRight; ++x) {
            document.setSpriteColorizedPixel(0, Resolution::Sd, x, y, x < 6 ? 0x1111 : 0x2222);
        }
    }
    document.setSpriteColorizedPixel(0, Resolution::Sd, 3, 3, 0);

    const std::vector<Point> left =
        edit::spriteFloodFillPixels(document, 0, Resolution::Sd, {2, 1});
    expect(left.size() == 4 * 6 - 1, "sprite fill takes the same-colored part of the shape");
    for (const Point& point : left) {
        expect(point.x >= kSpriteShapeLeft && point.x < 6 && point.y >= kSpriteShapeTop &&
                   point.y <= kSpriteShapeBottom,
               "sprite fill stays inside the shape and color");
    }
    expect(edit::spriteFloodFillPixels(document, 0, Resolution::Sd, {3, 3}).empty(),
           "transparent sprite pixel does not fill");
    expect(edit::spriteFloodFillPixels(document, 0, Resolution::Sd, {0, 0}).empty(),
           "pixel outside the sprite shape does not fill");
    expect(edit::spriteFloodFillPixels(document, 0, Resolution::Hd, {2, 1}).empty(),
           "HD sprite fill reads the HD pixels");
    expectThrows<std::out_of_range>(
        [&] { (void)edit::spriteFloodFillPixels(document, 0, Resolution::Sd, {-1, 0}); },
        "sprite fill outside the canvas");
    expectThrows<std::out_of_range>(
        [&] { (void)edit::spriteFloodFillPixels(document, 1, Resolution::Sd, {2, 1}); },
        "sprite fill with bad sprite index");

    // A color 0 island outside the shape must not leak in, and color outside the shape must
    // not be filled even when it matches.
    std::mt19937 random(7);
    for (int round = 0; round < 60; ++round) {
        for (int y = 0; y <= kSpriteShapeBottom + 2; ++y) {
            for (int x = 0; x <= kSpriteShapeRight + 2; ++x) {
                document.setSpriteColorizedPixel(0,
                                                 Resolution::Hd,
                                                 x,
                                                 y,
                                                 static_cast<uint16_t>(random() % 3));
            }
        }
        const int x = static_cast<int>(random() % (kSpriteShapeRight + 3));
        const int y = static_cast<int>(random() % (kSpriteShapeBottom + 3));
        expect(samePoints(edit::spriteFloodFillPixels(document, 0, Resolution::Hd, {x, y}),
                          legacySpriteFloodFill(document, 0, Resolution::Hd, x, y)),
               "sprite fill matches the former MainWindow algorithm, round " +
                   std::to_string(round));
    }
}

PaletteGradient makeGradient(Point shapeStart,
                             Point shapeEnd,
                             PaletteGradientDirection direction,
                             PaletteGradientPattern pattern = PaletteGradientPattern::Linear)
{
    PaletteGradient gradient;
    gradient.shapeStart = shapeStart;
    gradient.shapeEnd = shapeEnd;
    gradient.directionStart = shapeStart;
    gradient.directionEnd = shapeEnd;
    gradient.direction = direction;
    gradient.pattern = pattern;
    return gradient;
}

void testLinearGradientRatio()
{
    const PaletteGradient leftToRight =
        makeGradient({10, 5}, {19, 9}, PaletteGradientDirection::LeftToRight);
    expectNear(edit::paletteGradientRatio(leftToRight, {10, 7}), 0.0, "left edge");
    expectNear(edit::paletteGradientRatio(leftToRight, {19, 5}), 1.0, "right edge");
    expectNear(edit::paletteGradientRatio(leftToRight, {14, 9}), 4.0 / 9.0, "inside, any row");
    expectNear(edit::paletteGradientRatio(leftToRight, {0, 7}), 0.0, "clamped before the shape");
    expectNear(edit::paletteGradientRatio(leftToRight, {30, 7}), 1.0, "clamped after the shape");

    const PaletteGradient flipped =
        makeGradient({19, 9}, {10, 5}, PaletteGradientDirection::LeftToRight);
    expectNear(edit::paletteGradientRatio(flipped, {14, 6}), 4.0 / 9.0,
               "fixed directions ignore how the shape was dragged");

    const PaletteGradient rightToLeft =
        makeGradient({10, 5}, {19, 9}, PaletteGradientDirection::RightToLeft);
    expectNear(edit::paletteGradientRatio(rightToLeft, {19, 5}), 0.0, "right to left start");
    expectNear(edit::paletteGradientRatio(rightToLeft, {10, 5}), 1.0, "right to left end");

    const PaletteGradient topToBottom =
        makeGradient({10, 5}, {19, 9}, PaletteGradientDirection::TopToBottom);
    expectNear(edit::paletteGradientRatio(topToBottom, {15, 5}), 0.0, "top edge");
    expectNear(edit::paletteGradientRatio(topToBottom, {15, 7}), 0.5, "vertical middle");
    expectNear(edit::paletteGradientRatio(topToBottom, {15, 9}), 1.0, "bottom edge");

    const PaletteGradient bottomToTop =
        makeGradient({10, 5}, {19, 9}, PaletteGradientDirection::BottomToTop);
    expectNear(edit::paletteGradientRatio(bottomToTop, {15, 9}), 0.0, "bottom to top start");
    expectNear(edit::paletteGradientRatio(bottomToTop, {15, 6}), 0.75, "bottom to top inside");

    const PaletteGradient degenerate =
        makeGradient({4, 0}, {4, 8}, PaletteGradientDirection::LeftToRight);
    expectNear(edit::paletteGradientRatio(degenerate, {4, 3}), 0.0,
               "zero-length axis gives the start color");
}

void testDiagonalGradientRatio()
{
    const Point start{0, 0};
    const Point end{4, 4};
    const PaletteGradient down =
        makeGradient(start, end, PaletteGradientDirection::TopLeftToBottomRight);
    expectNear(edit::paletteGradientRatio(down, {0, 0}), 0.0, "top-left start");
    expectNear(edit::paletteGradientRatio(down, {2, 2}), 0.5, "diagonal middle");
    expectNear(edit::paletteGradientRatio(down, {4, 0}), 0.5, "off-diagonal projects onto it");
    expectNear(edit::paletteGradientRatio(down, {4, 4}), 1.0, "bottom-right end");

    const PaletteGradient up =
        makeGradient(start, end, PaletteGradientDirection::BottomRightToTopLeft);
    expectNear(edit::paletteGradientRatio(up, {4, 4}), 0.0, "bottom-right start");
    expectNear(edit::paletteGradientRatio(up, {1, 1}), 0.75, "towards top-left");

    const PaletteGradient topRight =
        makeGradient(start, end, PaletteGradientDirection::TopRightToBottomLeft);
    expectNear(edit::paletteGradientRatio(topRight, {4, 0}), 0.0, "top-right start");
    expectNear(edit::paletteGradientRatio(topRight, {0, 4}), 1.0, "bottom-left end");
    expectNear(edit::paletteGradientRatio(topRight, {0, 0}), 0.5, "top-left is halfway");

    const PaletteGradient bottomLeft =
        makeGradient(start, end, PaletteGradientDirection::BottomLeftToTopRight);
    expectNear(edit::paletteGradientRatio(bottomLeft, {0, 4}), 0.0, "bottom-left start");
    expectNear(edit::paletteGradientRatio(bottomLeft, {4, 0}), 1.0, "top-right end");
}

void testFreeGradientRatio()
{
    PaletteGradient gradient = makeGradient({20, 20}, {40, 30}, PaletteGradientDirection::Free);
    gradient.directionStart = {0, 0};
    gradient.directionEnd = {10, 0};
    expectNear(edit::paletteGradientRatio(gradient, {5, 7}), 0.5,
               "free direction follows its own line, not the shape");
    expectNear(edit::paletteGradientRatio(gradient, {-3, 0}), 0.0, "free direction clamps low");
    expectNear(edit::paletteGradientRatio(gradient, {25, 25}), 1.0, "free direction clamps high");

    gradient.directionStart = {10, 0};
    gradient.directionEnd = {0, 0};
    expectNear(edit::paletteGradientRatio(gradient, {2, 0}), 0.8, "free direction is oriented");

    gradient.directionStart = {0, 0};
    gradient.directionEnd = {3, 4};
    expectNear(edit::paletteGradientRatio(gradient, {3, 4}), 1.0, "free diagonal end");
    expectNear(edit::paletteGradientRatio(gradient, {4, -3}), 0.0, "perpendicular offset is free");

    gradient.directionEnd = gradient.directionStart;
    expectNear(edit::paletteGradientRatio(gradient, {30, 25}), 0.0,
               "zero-length direction gives the start color");

    const PaletteGradient radialOnly =
        makeGradient({10, 5}, {19, 9}, PaletteGradientDirection::CenterToEdge);
    expectNear(edit::paletteGradientRatio(radialOnly, {14, 7}), 4.0 / 9.0,
               "center/edge directions fall back to left-to-right for linear gradients");
}

void testRadialGradientRatio()
{
    const Point start{0, 0};
    const Point end{8, 4};
    const PaletteGradient circle = makeGradient(start,
                                                end,
                                                PaletteGradientDirection::CenterToEdge,
                                                PaletteGradientPattern::Circle);
    expectNear(edit::paletteGradientRatio(circle, {4, 2}), 0.0, "circle center");
    expectNear(edit::paletteGradientRatio(circle, {8, 2}), 1.0, "circle edge on the long axis");
    expectNear(edit::paletteGradientRatio(circle, {4, 4}), 0.5, "circle uses the larger radius");
    expectNear(edit::paletteGradientRatio(circle, {20, 2}), 1.0, "circle clamps outside");

    const PaletteGradient inward = makeGradient(start,
                                                end,
                                                PaletteGradientDirection::EdgeToCenter,
                                                PaletteGradientPattern::Circle);
    expectNear(edit::paletteGradientRatio(inward, {4, 2}), 1.0, "edge-to-center center");
    expectNear(edit::paletteGradientRatio(inward, {4, 4}), 0.5, "edge-to-center inside");
    expectNear(edit::paletteGradientRatio(inward, {0, 2}), 0.0, "edge-to-center edge");

    const PaletteGradient ellipse = makeGradient(start,
                                                 end,
                                                 PaletteGradientDirection::CenterToEdge,
                                                 PaletteGradientPattern::Ellipse);
    expectNear(edit::paletteGradientRatio(ellipse, {4, 4}), 1.0, "ellipse edge on the short axis");
    expectNear(edit::paletteGradientRatio(ellipse, {6, 2}), 0.5, "ellipse long axis");
    expectNear(edit::paletteGradientRatio(ellipse, {4, 3}), 0.5, "ellipse short axis");

    PaletteGradient freeCircle =
        makeGradient(start, end, PaletteGradientDirection::Free, PaletteGradientPattern::Circle);
    freeCircle.directionStart = {100, 100};
    freeCircle.directionEnd = {101, 100};
    expectNear(edit::paletteGradientRatio(freeCircle, {4, 4}), 0.5,
               "radial patterns ignore the free direction line");

    const PaletteGradient dot = makeGradient({3, 3},
                                             {3, 3},
                                             PaletteGradientDirection::CenterToEdge,
                                             PaletteGradientPattern::Ellipse);
    expectNear(edit::paletteGradientRatio(dot, {3, 3}), 0.0, "single-pixel shape center");
    expectNear(edit::paletteGradientRatio(dot, {4, 3}), 1.0, "single-pixel shape clamps");
}

void testPaletteGradientColorIndex()
{
    expect(edit::paletteGradientColorIndex(2, 5, 0.0) == 2, "ratio 0 is the start color");
    expect(edit::paletteGradientColorIndex(2, 5, 0.24) == 2, "first band");
    expect(edit::paletteGradientColorIndex(2, 5, 0.25) == 3, "second band starts at 1/4");
    expect(edit::paletteGradientColorIndex(2, 5, 0.5) == 4, "third band");
    expect(edit::paletteGradientColorIndex(2, 5, 0.75) == 5, "last band");
    expect(edit::paletteGradientColorIndex(2, 5, 1.0) == 5, "ratio 1 stays on the end color");
    expect(edit::paletteGradientColorIndex(2, 5, -1.0) == 2, "ratio below 0 clamps");
    expect(edit::paletteGradientColorIndex(2, 5, 2.0) == 5, "ratio above 1 clamps");

    expect(edit::paletteGradientColorIndex(5, 2, 0.0) == 5, "reversed range starts high");
    expect(edit::paletteGradientColorIndex(5, 2, 0.5) == 3, "reversed range counts down");
    expect(edit::paletteGradientColorIndex(5, 2, 1.0) == 2, "reversed range ends low");
    expect(edit::paletteGradientColorIndex(7, 7, 0.9) == 7, "single color range");

    PaletteGradient gradient = makeGradient({0, 0}, {7, 0}, PaletteGradientDirection::LeftToRight);
    gradient.startColorIndex = 10;
    gradient.endColorIndex = 13;
    const std::size_t expected[] = {10, 10, 11, 11, 12, 12, 13, 13};
    for (int x = 0; x < 8; ++x) {
        expect(edit::paletteGradientColorIndex(gradient, {x, 0}) == expected[x],
               "gradient spreads colors evenly, x " + std::to_string(x));
    }
}

void testInterpolateRgb565()
{
    expect(edit::interpolateRgb565(0x1234, 0xffff, 3, 0) == 0x1234,
           "no steps returns the start color untouched");
    expect(edit::interpolateRgb565(0x0000, 0xffff, 0, 4) == 0x0000, "black start");
    expect(edit::interpolateRgb565(0x0000, 0xffff, 4, 4) == 0xffff, "white end");
    expect(edit::interpolateRgb565(0x0000, 0xffff, 1, 2) == 0x8410,
           "grey midpoint rounds half up per component");
    expect(edit::interpolateRgb565(0xffff, 0x0000, 2, 3) == 0x52aa, "one third towards black");
    expect(edit::interpolateRgb565(0xf800, 0x001f, 1, 2) == 0x8010, "red to blue midpoint");
    expect(edit::interpolateRgb565(0x0000, 0x0003, 1, 4) == 0x0001,
           "three quarters of a step rounds up");
    expect(edit::interpolateRgb565(0x0000, 0x0001, 1, 4) == 0x0000,
           "a quarter of a step rounds down");
    expect(edit::interpolateRgb565(0x0841, 0xffff, 0, 2) == 0x0841,
           "darkest channel steps survive at the start");
    expect(edit::interpolateRgb565(0x1234, 0xabcd, 9, 4) == 0xabcd,
           "steps beyond the last one give the end color");
}

void testInterpolateRgb565KeepsEndpoints()
{
    const auto red = [](uint16_t color) { return (color >> 11) & 0x1f; };
    const auto green = [](uint16_t color) { return (color >> 5) & 0x3f; };
    const auto blue = [](uint16_t color) { return color & 0x1f; };
    const auto between = [](int value, int a, int b) {
        return value >= std::min(a, b) && value <= std::max(a, b);
    };
    uint32_t state = 12345;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    for (uint32_t color = 0; color <= 0xffff; ++color) {
        const uint16_t start = static_cast<uint16_t>(color);
        const uint16_t end = static_cast<uint16_t>(next());
        const std::size_t maxStep = 1 + next() % 63;
        const std::size_t step = next() % (maxStep + 1);
        expect(edit::interpolateRgb565(start, end, 0, maxStep) == start,
               "step 0 is exactly the start color");
        expect(edit::interpolateRgb565(end, start, maxStep, maxStep) == start,
               "the last step is exactly the end color");
        expect(edit::interpolateRgb565(start, start, step, maxStep) == start,
               "a ramp between equal colors is constant");
        const uint16_t blended = edit::interpolateRgb565(start, end, step, maxStep);
        expect(between(red(blended), red(start), red(end)) &&
                   between(green(blended), green(start), green(end)) &&
                   between(blue(blended), blue(start), blue(end)),
               "every component stays between its ends");
        expect(blended == edit::interpolateRgb565(end, start, maxStep - step, maxStep),
               "a ramp reads the same from both ends");
    }
}

void testPaletteRampChanges()
{
    ProjectDocument document = makeDocument();
    document.setPaletteColor(0, 2, 0x0000);
    document.setPaletteColor(0, 3, 0x1234);
    document.setPaletteColor(0, 4, 0xffff);

    const std::vector<PaletteColorChange> ramp = edit::makePaletteRampChanges(document, 0, 2, 4);
    expect(ramp.size() == 1, "ramp skips colors that already match");
    expect(ramp[0].colorIndex == 3 && ramp[0].oldColor == 0x1234 && ramp[0].newColor == 0x8410,
           "ramp blends the color between its ends");

    document.setPaletteColor(0, 3, 0x8410);
    expect(edit::makePaletteRampChanges(document, 0, 2, 4).empty(), "finished ramp is a no-op");

    document.setPaletteColor(0, 3, 0x1234);
    document.setPaletteColor(0, 4, 0x1234);
    document.setPaletteColor(0, 5, 0xffff);
    const std::vector<PaletteColorChange> reversed =
        edit::makePaletteRampChanges(document, 0, 5, 2);
    expect(reversed.size() == 2, "reversed ramp changes the inner colors");
    expect(reversed[0].colorIndex == 3 && reversed[0].oldColor == 0x1234 &&
               reversed[0].newColor == 0x52aa,
           "reversed ramp is darkest next to its end index");
    expect(reversed[1].colorIndex == 4 && reversed[1].newColor == 0xad55,
           "reversed ramp is brightest next to its start index");
    expect(document.paletteColor(0, 3) == 0x1234, "building changes leaves the document alone");

    // Colors the old 8-bit round trip could not represent: the ends must stay untouched.
    document.setPaletteColor(0, 10, 0x0841);
    document.setPaletteColor(0, 11, 0x0841);
    document.setPaletteColor(0, 12, 0x0841);
    expect(edit::makePaletteRampChanges(document, 0, 10, 12).empty(),
           "ramp between equal colors changes nothing");
    document.setPaletteColor(0, 12, 0x18c3);
    const std::vector<PaletteColorChange> dark = edit::makePaletteRampChanges(document, 0, 10, 12);
    expect(dark.size() == 1 && dark[0].colorIndex == 11 && dark[0].newColor == 0x1082,
           "ramp never changes its two end colors");

    expectThrows<std::out_of_range>(
        [&] {
            (void)edit::makePaletteRampChanges(document,
                                               0,
                                               2,
                                               static_cast<int>(kPaletteColorCount));
        },
        "ramp end outside the palette");
}

void testPixelChangesDedupeAndSkipNoOps()
{
    ProjectDocument document = makeDocument();
    document.setColorizedPixel(0, Resolution::Sd, 1, 0, 0x00aa);
    document.setColorizedPixel(0, Resolution::Sd, 2, 0, 0x1234);
    document.markClean();

    const EditLayer layer{EditKind::FramePixels, 0, Resolution::Sd};
    const std::vector<PixelChange> changes = edit::makePixelChanges(
        document, layer, {{3, 0}, {1, 0}, {2, 0}, {1, 0}, {3, 0}}, uint16_t{0x1234});
    expect(sameChanges(changes, {{3, 0, 0x0000, 0x1234}, {1, 0, 0x00aa, 0x1234}}),
           "changes keep first-occurrence order, capture old colors and skip no-ops");
    expect(!document.isDirty(), "building changes does not edit the document");

    expect(edit::makePixelChanges(document, layer, {}, uint16_t{1}).empty(),
           "no points, no changes");
    expect(edit::makePixelChanges(document, layer, {{2, 0}, {2, 0}}, uint16_t{0x1234}).empty(),
           "all no-ops, no changes");
    expectThrows<std::out_of_range>(
        [&] { (void)edit::makePixelChanges(document, layer, {{0, 0}, {128, 0}}, uint16_t{1}); },
        "frame pixel outside the layer");
    expectThrows<std::logic_error>(
        [&] {
            (void)edit::makePixelChanges(document,
                                         EditLayer{EditKind::FramePixels, 1, Resolution::Hd},
                                         {{0, 0}},
                                         uint16_t{1});
        },
        "frame without HD");
    expectThrows<std::invalid_argument>(
        [&] {
            (void)edit::makePixelChanges(document,
                                         EditLayer{EditKind::DynamicMask, 0, Resolution::Sd},
                                         {{0, 0}},
                                         uint16_t{1});
        },
        "mask kind has no RGB565 pixels");
}

void testPixelChangesPerLayerAndColorSource()
{
    ProjectDocument document = makeDocument();
    document.setColorizedPixel(0, Resolution::Hd, 200, 60, 0x0f0f);
    document.setSpriteColorizedPixel(0, Resolution::Hd, 4, 4, 0x0abc);

    const std::vector<PixelChange> hd = edit::makePixelChanges(
        document, EditLayer{EditKind::FramePixels, 0, Resolution::Hd}, {{200, 60}}, uint16_t{1});
    expect(sameChanges(hd, {{200, 60, 0x0f0f, 1}}), "frame layer reads the requested resolution");

    const std::vector<PixelChange> background = edit::makePixelChanges(
        document,
        EditLayer{EditKind::BackgroundImage, 0, Resolution::Hd},
        {{255, 63}},
        uint16_t{2});
    expect(sameChanges(background, {{255, 63, 0x6060, 2}}), "background layer reads backgrounds");

    const std::vector<PixelChange> sprite = edit::makePixelChanges(
        document, EditLayer{EditKind::SpriteImage, 0, Resolution::Hd}, {{4, 4}}, uint16_t{3});
    expect(sameChanges(sprite, {{4, 4, 0x0abc, 3}}), "sprite layer reads sprite pixels");

    int calls = 0;
    const std::vector<PixelChange> perPoint = edit::makePixelChanges(
        document,
        EditLayer{EditKind::BackgroundImage, 0, Resolution::Sd},
        {{0, 0}, {1, 0}, {0, 0}, {2, 0}},
        [&calls](Point point) {
            ++calls;
            return point.x == 1 ? uint16_t{0x5050} : static_cast<uint16_t>(0x100 + point.x);
        });
    expect(calls == 3, "color source is asked once per distinct point");
    expect(sameChanges(perPoint, {{0, 0, 0x5050, 0x100}, {2, 0, 0x5050, 0x102}}),
           "per-point colors skip points that already match");
}

void testPixelChangesMatchLegacyLoop()
{
    ProjectDocument document = makeDocument();
    std::mt19937 random(99);
    for (int round = 0; round < 200; ++round) {
        std::vector<Point> points;
        const std::size_t count = random() % 40;
        for (std::size_t index = 0; index < count; ++index) {
            points.push_back(Point{static_cast<int>(random() % 6), static_cast<int>(random() % 4)});
        }
        const uint16_t color = static_cast<uint16_t>(random() % 3);
        const std::vector<PixelChange> changes = edit::makePixelChanges(
            document, EditLayer{EditKind::FramePixels, 0, Resolution::Sd}, points, color);
        expect(sameChanges(changes,
                           legacyFramePixelChanges(document, 0, Resolution::Sd, points, color)),
               "pixel changes match the former MainWindow loop, round " + std::to_string(round));
        for (const PixelChange& change : changes) {
            document.setColorizedPixel(0, Resolution::Sd, change.x, change.y, change.newColor);
        }
    }
}

void testMaskChanges()
{
    ProjectDocument document = makeDocument();
    document.setComparisonMaskPixel(0, 1, 0, 1);
    document.setDynamicMaskPixel(0, Resolution::Hd, 5, 5, 3);
    document.setBackgroundMaskPixel(0, Resolution::Sd, 7, 7, 1);
    document.markClean();

    const std::vector<MaskPixelChange> comparison = edit::makeMaskChanges(
        document,
        EditLayer{EditKind::ComparisonMask, 0, Resolution::Hd},
        {{0, 0}, {1, 0}, {0, 0}, {2, 0}},
        1);
    expect(comparison.size() == 2, "comparison mask dedupes and skips set pixels");
    expect(comparison[0].x == 0 && comparison[0].oldValue == 0 && comparison[0].newValue == 1,
           "comparison mask first change");
    expect(comparison[1].x == 2 && comparison[1].y == 0, "comparison mask second change");

    const std::vector<MaskPixelChange> dynamic = edit::makeMaskChanges(
        document, EditLayer{EditKind::DynamicMask, 0, Resolution::Hd}, {{5, 5}, {6, 5}}, 255);
    expect(dynamic.size() == 1 && dynamic[0].x == 5 && dynamic[0].oldValue == 3 &&
               dynamic[0].newValue == 255,
           "dynamic mask erase captures the old set and skips unset pixels");

    const std::vector<MaskPixelChange> background = edit::makeMaskChanges(
        document, EditLayer{EditKind::BackgroundMask, 0, Resolution::Sd}, {{7, 7}, {8, 7}}, 1);
    expect(background.size() == 1 && background[0].x == 8 && background[0].oldValue == 0,
           "background mask skips pixels already set");
    expect(!document.isDirty(), "building mask changes does not edit the document");

    expectThrows<std::logic_error>(
        [&] {
            (void)edit::makeMaskChanges(document,
                                        EditLayer{EditKind::DynamicMask, 1, Resolution::Sd},
                                        {{0, 0}},
                                        1);
        },
        "frame without dynamic data");
    expectThrows<std::out_of_range>(
        [&] {
            (void)edit::makeMaskChanges(document,
                                        EditLayer{EditKind::ComparisonMask, 0, Resolution::Sd},
                                        {{0, 32}},
                                        1);
        },
        "mask pixel outside the layer");
    expectThrows<std::invalid_argument>(
        [&] {
            (void)edit::makeMaskChanges(document,
                                        EditLayer{EditKind::FramePixels, 0, Resolution::Sd},
                                        {{0, 0}},
                                        1);
        },
        "pixel kind has no mask");
}

void testSpritePaintChanges()
{
    ProjectDocument document = makeDocument();
    document.setSpriteColorizedPixel(0, Resolution::Sd, 2, 1, 0x1111);
    document.setSpriteColorizedPixel(0, Resolution::Sd, 3, 1, 0x2222);
    document.setSpriteColorizedPixel(0, Resolution::Sd, 0, 0, 0x3333);
    document.markClean();

    const std::vector<Point> points{
        {2, 1}, {3, 1}, {2, 1}, {4, 1}, {0, 0}, {-1, 0}, {256, 0}, {0, 64}};
    const std::vector<PixelChange> painted =
        edit::makeSpritePaintChanges(document, 0, Resolution::Sd, points, 0x2222, false);
    expect(sameChanges(painted, {{2, 1, 0x1111, 0x2222}}),
           "painting skips duplicates, no-ops, transparent pixels and pixels outside the shape");

    const std::vector<PixelChange> erased =
        edit::makeSpritePaintChanges(document, 0, Resolution::Sd, points, 0, true);
    expect(sameChanges(erased, {{2, 1, 0x1111, 0}, {3, 1, 0x2222, 0}}),
           "erasing clears colored pixels inside the shape only");

    document.setSpriteColorizedPixel(0, Resolution::Hd, 4, 1, 0x4444);
    document.markClean();
    const std::vector<PixelChange> hd =
        edit::makeSpritePaintChanges(document, 0, Resolution::Hd, points, 0x2222, false);
    expect(sameChanges(hd, {{4, 1, 0x4444, 0x2222}}), "HD painting reads the HD sprite");
    expect(!document.isDirty(), "building sprite changes does not edit the document");

    expectThrows<std::out_of_range>(
        [&] { (void)edit::makeSpritePaintChanges(document, 3, Resolution::Sd, points, 1, false); },
        "sprite paint with bad sprite index");
}

void testStrokePixelsPerTool()
{
    using edit::DrawingTool;
    using edit::PaletteGradientShape;
    const Point start{2, 3};
    const Point end{9, 8};
    const auto stroke = [&](DrawingTool tool, PaletteGradientShape shape) {
        return edit::makeStrokePixels(tool, shape, start, end);
    };
    const PaletteGradientShape rectangle = PaletteGradientShape::Rectangle;

    expect(samePoints(stroke(DrawingTool::Line, rectangle), edit::makeLinePixels(start, end)),
           "line stroke");
    expect(samePoints(stroke(DrawingTool::Rectangle, rectangle),
                      edit::makeRectanglePixels(start, end)),
           "rectangle stroke");
    expect(samePoints(stroke(DrawingTool::FilledRectangle, rectangle),
                      edit::makeFilledRectanglePixels(start, end)),
           "filled rectangle stroke");
    expect(samePoints(stroke(DrawingTool::Circle, rectangle), edit::makeCirclePixels(start, end)),
           "circle stroke");
    expect(samePoints(stroke(DrawingTool::FilledCircle, rectangle),
                      edit::makeFilledCirclePixels(start, end)),
           "filled circle stroke");
    expect(samePoints(stroke(DrawingTool::Ellipse, rectangle),
                      edit::makeEllipsePixels(start, end)),
           "ellipse stroke");
    expect(samePoints(stroke(DrawingTool::FilledEllipse, rectangle),
                      edit::makeFilledEllipsePixels(start, end)),
           "filled ellipse stroke");

    // Only the gradient and selection tools look at the gradient shape.
    expect(samePoints(stroke(DrawingTool::Line, PaletteGradientShape::Circle),
                      edit::makeLinePixels(start, end)),
           "line stroke ignores the gradient shape");
    for (DrawingTool tool : {DrawingTool::PaletteGradient, DrawingTool::Selection}) {
        expect(samePoints(stroke(tool, rectangle), edit::makeFilledRectanglePixels(start, end)),
               "rectangle gradient shape");
        expect(samePoints(stroke(tool, PaletteGradientShape::Circle),
                          edit::makeFilledCirclePixels(start, end)),
               "circle gradient shape");
        expect(samePoints(stroke(tool, PaletteGradientShape::Ellipse),
                          edit::makeFilledEllipsePixels(start, end)),
               "ellipse gradient shape");
    }
    for (DrawingTool tool :
         {DrawingTool::Point, DrawingTool::ColorPicker, DrawingTool::FloodFill}) {
        expect(stroke(tool, rectangle).empty(), "click tools have no stroke");
    }
}

void testPaletteGradientColorSource()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "gradient_source";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(kSdFrameSize, 0),
        makeRgb565Frame(kSdFrameSize, 0),
    });
    for (uint16_t paletteIndex = 0; paletteIndex < 2; ++paletteIndex) {
        PaletteRecord palette;
        for (std::size_t color = 0; color < kPaletteColorCount; ++color) {
            palette.colors.push_back(static_cast<uint16_t>(paletteIndex * 0x100 + color));
        }
        snapshot.palettes.push_back(palette);
    }
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);

    const PaletteGradient gradient{
        Point{0, 0},
        Point{3, 0},
        Point{0, 0},
        Point{3, 0},
        PaletteGradientDirection::LeftToRight,
        PaletteGradientPattern::Linear,
        4,
        7,
    };
    const edit::PixelColorSource source = edit::paletteGradientColorSource(document, 1, gradient);
    for (int x = 0; x <= 3; ++x) {
        const Point point{x, 0};
        expect(source(point) ==
                   document.paletteColor(1, edit::paletteGradientColorIndex(gradient, point)),
               "gradient source reads the palette color of the point's band");
    }
    expect(source(Point{0, 0}) == 0x104 && source(Point{3, 0}) == 0x107,
           "gradient source runs from the start to the end color");

    document.setPaletteColor(1, 4, 0xabcd);
    expect(source(Point{0, 0}) == 0xabcd, "gradient source reads the document when asked");
}

void testFramePixelSelection()
{
    edit::FramePixelSelection selection(Size{4, 3});
    expect(!selection.frameIndex(), "new selection has no frame");
    expect(selection.pixels(0).empty() && selection.pixels(std::nullopt).empty(),
           "new selection has no pixels");

    expect(selection.paint(2, {Point{3, 2}, Point{1, 0}, Point{1, 0}, Point{4, 0}, Point{0, -1}},
                           false),
           "marking pixels changes the selection");
    expect(selection.frameIndex() == std::optional<std::size_t>(2), "selection is on its frame");
    expect(samePoints(selection.pixels(2), {Point{1, 0}, Point{3, 2}}),
           "marked pixels come back row by row, without the ones outside");
    expect(selection.pixels(1).empty() && selection.pixels(std::nullopt).empty(),
           "another frame shows no selection");

    expect(!selection.paint(2, {Point{1, 0}}, false), "marking a marked pixel changes nothing");
    expect(!selection.paint(2, {Point{0, 0}}, true), "erasing an unmarked pixel changes nothing");
    expect(!selection.paint(2, {Point{9, 9}}, false), "points outside change nothing");
    expect(selection.paint(2, {Point{1, 0}}, true), "erasing a marked pixel");
    expect(samePoints(selection.pixels(2), {Point{3, 2}}), "erased pixel is gone");

    expect(!selection.paint(5, {Point{0, 0}}, true),
           "erasing on another frame changes no pixel");
    expect(selection.frameIndex() == std::optional<std::size_t>(5) &&
               selection.pixels(5).empty() && selection.pixels(2).empty(),
           "painting on another frame drops the old selection even without a change");

    expect(selection.paint(5, {Point{0, 0}}, false), "marking on the new frame");
    selection.clear();
    expect(!selection.frameIndex() && selection.pixels(5).empty(), "clear drops everything");
}

}  // namespace

void runDrawingToolsTests()
{
    testLineGeometry();
    testRectangleGeometry();
    testEllipseGeometry();
    testCircleGeometry();
    testFloodFillRegionAndBorders();
    testFloodFillConnectivity();
    testFloodFillNoOps();
    testFloodFillPredicateContract();
    testFloodFillMatchesLegacyAlgorithm();
    testSpriteFloodFill();
    testLinearGradientRatio();
    testDiagonalGradientRatio();
    testFreeGradientRatio();
    testRadialGradientRatio();
    testPaletteGradientColorIndex();
    testInterpolateRgb565();
    testInterpolateRgb565KeepsEndpoints();
    testPaletteRampChanges();
    testPixelChangesDedupeAndSkipNoOps();
    testPixelChangesPerLayerAndColorSource();
    testPixelChangesMatchLegacyLoop();
    testMaskChanges();
    testSpritePaintChanges();
    testStrokePixelsPerTool();
    testPaletteGradientColorSource();
    testFramePixelSelection();
}
