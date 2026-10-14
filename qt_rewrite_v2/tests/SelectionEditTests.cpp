#include "edit/AutoCopy.h"
#include "edit/CopyPaste.h"
#include "edit/FrameContentCommand.h"
#include "edit/PlaneSelection.h"
#include "edit/RecolorHelpers.h"
#include "edit/ScrollFill.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/ColorSetEdits.h"
#include "edit/DrawingTools.h"
#include "edit/UndoHistories.h"
#include "legacy/LegacyProjectSerializer.h"
#include "render/ProjectGeometry.h"

namespace document = colorizing::document;
namespace edit = colorizing::edit;
using colorizing::render::PlaneLayout;
using colorizing::render::Point;
using colorizing::render::ProjectGeometry;
using colorizing::render::Rect;
using colorizing::render::Resolution;
using colorizing::render::Size;
using document::ProjectDocument;
using document::ProjectSnapshot;
using edit::PlaneSelection;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: selection edits: " << message << '\n';
        std::exit(1);
    }
}

template <typename Exception>
bool throws(const std::function<void()>& call)
{
    try {
        call();
    } catch (const Exception&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

std::size_t at(Size size, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
           static_cast<std::size_t>(x);
}

const char* layoutName(PlaneLayout layout)
{
    switch (layout) {
        case PlaneLayout::Ordinary:
            return "128x32";
        case PlaneLayout::NativeExtra:
            return "256x64 original";
        case PlaneLayout::NativeOriginal:
            return "192x64";
    }
    return "?";
}

// originals: one ROM frame per project frame. Colorized frames are a pattern that differs per
// frame; ordinary projects get HD frames for the frames listed in hdFrames.
ProjectSnapshot makeSnapshot(PlaneLayout layout,
                             const std::vector<document::IndexedFrame>& originals,
                             const std::vector<std::size_t>& hdFrames = {})
{
    const ProjectGeometry geometry(layout);
    ProjectSnapshot snapshot;
    snapshot.romName = "selection";
    snapshot.colorCount = 4;
    snapshot.geometry = geometry;
    for (std::size_t frameIndex = 0; frameIndex < originals.size(); ++frameIndex) {
        document::ProjectFrameSnapshot frame =
            document::makeUncolorizedFrameSnapshot(geometry, 4, originals[frameIndex], 30);
        for (std::size_t i = 0; i < frame.colorizedSd.pixels.size(); ++i) {
            frame.colorizedSd.pixels[i] = static_cast<uint16_t>(0x100 * (frameIndex + 1) + i % 97);
        }
        if (frame.colorizedHd) {
            for (std::size_t i = 0; i < frame.colorizedHd->pixels.size(); ++i) {
                frame.colorizedHd->pixels[i] =
                    static_cast<uint16_t>(0x2000 + 0x100 * (frameIndex + 1) + i % 89);
            }
        }
        if (layout == PlaneLayout::Ordinary) {
            for (std::size_t hdFrame : hdFrames) {
                if (hdFrame == frameIndex) {
                    frame.colorizedHd = document::makeRgb565Frame(
                        geometry.planeSize(Resolution::Hd),
                        static_cast<uint16_t>(0x4000 + frameIndex));
                }
            }
        }
        snapshot.frames.push_back(frame);
    }
    snapshot.palettes.assign(document::kPaletteCount,
                             document::PaletteRecord{std::vector<uint16_t>(64, 0), ""});
    return snapshot;
}

document::IndexedFrame patterned(Size size, int seed)
{
    document::IndexedFrame frame = document::makeIndexedFrame(size, 0);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            frame.pixels[at(size, x, y)] = static_cast<uint8_t>((x * 3 + y * 5 + seed) % 4);
        }
    }
    return frame;
}

ProjectDocument makeDocument(PlaneLayout layout,
                             const std::vector<document::IndexedFrame>& originals,
                             const std::vector<std::size_t>& hdFrames = {})
{
    ProjectDocument document =
        ProjectDocument::fromSnapshot(makeSnapshot(layout, originals, hdFrames));
    document.markClean();
    return document;
}

std::string cromBytes(const ProjectDocument& document)
{
    std::ostringstream stream;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrom(stream, document.toSnapshot(), error),
           "cROM writes: " + error);
    return stream.str();
}

void applyChanges(ProjectDocument& document,
                  edit::UndoHistories& histories,
                  std::size_t frameIndex,
                  edit::FrameContentChanges changes)
{
    auto command = std::make_unique<edit::FrameContentCommand>(frameIndex,
                                                               document.dynamicPlane(),
                                                               std::move(changes));
    command->apply(document);
    const edit::HistoryKey key = edit::historyKey(command->target());
    histories.push(key, std::move(command));
}

void testPlaneSelection()
{
    PlaneSelection selection(Resolution::Hd, Size{8, 4});
    expect(selection.isEmpty() && !selection.bounds(), "a new selection is empty");
    expect(selection.add({Point{1, 1}, Point{2, 1}, Point{9, 9}, Point{-1, 0}}), "add");
    expect(selection.count() == 2, "points outside the plane are ignored");
    expect(!selection.add({Point{1, 1}}), "adding a selected pixel changes nothing");
    const std::optional<Rect> bounds = selection.bounds();
    expect(bounds && bounds->x == 1 && bounds->y == 1 && bounds->width == 2 && bounds->height == 1,
           "bounds");
    selection.mirror(true, false);
    expect(selection.contains(Point{6, 1}) && selection.contains(Point{5, 1}) &&
               !selection.contains(Point{1, 1}) && selection.count() == 2,
           "mirror horizontally");
    selection.mirror(false, true);
    expect(selection.contains(Point{6, 2}) && selection.contains(Point{5, 2}), "mirror vertically");
    selection.invert();
    expect(selection.count() == 30 && !selection.contains(Point{6, 2}), "invert");
    expect(selection.remove({Point{0, 0}}) && selection.count() == 29, "remove");
    selection.selectAll();
    expect(selection.count() == 32, "select all");
    selection.clear();
    expect(selection.isEmpty(), "select none");
    expect(throws<std::invalid_argument>([&] { selection.assignMask(std::vector<uint8_t>{1, 0}); }),
           "a mask of another size is refused");
    std::vector<uint8_t> mask(32, 0);
    mask[at(Size{8, 4}, 3, 2)] = 7;
    selection.assignMask(mask);
    expect(selection.count() == 1 && selection.contains(Point{3, 2}), "assign a mask");
    expect(selection.plane() == Resolution::Hd, "the plane is kept");

    // Stripes: rows 1-2, skip 3, rows 6-7 of an 8-row plane.
    const std::vector<uint8_t> rows =
        edit::stripeMask(Size{4, 8}, edit::StripePattern{1, 2, 3, false});
    for (int y = 0; y < 8; ++y) {
        const bool selected = y == 1 || y == 2 || y == 6 || y == 7;
        for (int x = 0; x < 4; ++x) {
            expect((rows[at(Size{4, 8}, x, y)] != 0) == selected, "row stripes");
        }
    }
    const std::vector<uint8_t> columns =
        edit::stripeMask(Size{5, 2}, edit::StripePattern{0, 1, 1, true});
    for (int x = 0; x < 5; ++x) {
        expect((columns[at(Size{5, 2}, x, 1)] != 0) == (x % 2 == 0), "column stripes");
    }
    expect(!edit::isValidStripePattern(edit::StripePattern{0, 0, 1, false}) &&
               !edit::isValidStripePattern(edit::StripePattern{0, 1, 0, false}) &&
               !edit::isValidStripePattern(edit::StripePattern{257, 1, 1, false}) &&
               edit::isValidStripePattern(edit::StripePattern{256, 256, 256, true}),
           "the Win32 limits of the auto selection");
    expect(throws<std::invalid_argument>(
               [] { (void)edit::stripeMask(Size{4, 4}, edit::StripePattern{0, 0, 1, false}); }),
           "an invalid pattern throws");

    // ROM masks: the doubled plane of an ordinary project covers a ROM pixel per 2x2 block.
    const ProjectGeometry ordinary(PlaneLayout::Ordinary);
    PlaneSelection hd(Resolution::Hd, ordinary.planeSize(Resolution::Hd));
    hd.add({Point{5, 3}});
    const std::vector<uint8_t> rom = edit::selectionRomMask(ordinary, hd);
    expect(rom.size() == 128u * 32u && rom[at(Size{128, 32}, 2, 1)] == 1 &&
               std::count(rom.begin(), rom.end(), 1) == 1,
           "an HD pixel covers the ROM pixel at half its coordinates");
    const ProjectGeometry big(PlaneLayout::NativeExtra);
    PlaneSelection native(Resolution::Hd, big.planeSize(Resolution::Hd));
    native.add({Point{255, 63}});
    const std::vector<uint8_t> bigRom = edit::selectionRomMask(big, native);
    expect(bigRom.size() == 256u * 64u && bigRom[at(Size{256, 64}, 255, 63)] == 1,
           "the native plane is the ROM frame");

    document::Rgb565Frame frame = document::makeRgb565Frame(Size{3, 2}, 5);
    frame.pixels[1] = 0;
    frame.pixels[4] = 9;
    const std::vector<Point> fives = edit::pixelsOfColor(frame, 5);
    expect(fives.size() == 4 && fives[0].x == 0 && fives[1].x == 2, "pixels of a color");
    const std::vector<Point> lit =
        edit::nonBlackPixels(frame, {Point{0, 0}, Point{1, 0}, Point{1, 1}, Point{7, 7}});
    expect(lit.size() == 2 && lit[1].y == 1, "only non-black pixels");
}

// A dynamic zone of set 2 at (4..6, 3) on the dynamic plane of frame 0, with its colors.
void addDynamicZone(ProjectDocument& document, std::size_t frameIndex)
{
    const Resolution plane = document.dynamicPlane();
    for (int x = 4; x <= 6; ++x) {
        document.setDynamicMaskPixel(frameIndex, plane, x, 3, 2);
    }
    for (std::size_t color = 0; color < 4; ++color) {
        document.setDynamicColor(frameIndex, plane, 2, color, static_cast<uint16_t>(0xA0 + color));
    }
    document.setDynamicShadowDirection(frameIndex, plane, 2, 5);
    document.setDynamicShadowColor(frameIndex, plane, 2, 0x1234);
}

void testCopyBuffers()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        const ProjectGeometry geometry(layout);
        const Size rom = geometry.romFrameSize();
        ProjectDocument document =
            makeDocument(layout, {patterned(rom, 0), patterned(rom, 1)}, {0, 1});
        addDynamicZone(document, 0);
        const Resolution plane = document.dynamicPlane();
        const Size planeSize = geometry.planeSize(plane);
        PlaneSelection selection(plane, planeSize);
        selection.add({Point{4, 3}, Point{5, 3}, Point{8, 5}});
        const edit::CopyBuffer buffer = edit::copySelection(document, 0, selection);
        const std::string name = layoutName(layout);
        expect(buffer.box.x == 4 && buffer.box.y == 3 && buffer.box.width == 5 &&
                   buffer.box.height == 3,
               name + ": the buffer is the bounding box");
        expect(buffer.selectedCount() == 3 && buffer.mask[0] == 1 && buffer.mask[2] == 0,
               name + ": the selected box pixels");
        expect(buffer.colors[0] == document.colorizedPixel(0, plane, 4, 3) &&
                   buffer.colors[2] == document.colorizedPixel(0, plane, 6, 3),
               name + ": the colors, also of unselected box pixels");
        expect(buffer.originals[at(Size{5, 3}, 4, 2)] ==
                   document.originalFrame(0).pixels[at(rom, 8, 5)],
               name + ": the original shades");
        expect(buffer.hasDynamicData() && buffer.dynamicMask[0] == 2 &&
                   buffer.dynamicMask[at(Size{5, 3}, 4, 2)] == 255,
               name + ": the dynamic mask on the dynamic plane");
        expect(buffer.dynamicSets.size() == 1 && buffer.dynamicSets[0].setIndex == 2 &&
                   buffer.dynamicSets[0].colors[3] == 0xA3 &&
                   buffer.dynamicSets[0].shadowDirection == 5 &&
                   buffer.dynamicSets[0].shadowColor == 0x1234,
               name + ": the dynamic sets the copied pixels use");
        expect(throws<std::invalid_argument>([&] {
                   (void)edit::copySelection(document, 0, PlaneSelection(plane, planeSize));
               }),
               name + ": an empty selection cannot be copied");
    }

    // The HD plane of an ordinary project: no dynamic data, originals at half the coordinates.
    const ProjectGeometry ordinary(PlaneLayout::Ordinary);
    ProjectDocument document = makeDocument(
        PlaneLayout::Ordinary, {patterned(Size{128, 32}, 0), patterned(Size{128, 32}, 2)}, {0});
    addDynamicZone(document, 0);
    PlaneSelection hd(Resolution::Hd, ordinary.planeSize(Resolution::Hd));
    hd.add({Point{9, 7}});
    const edit::CopyBuffer buffer = edit::copySelection(document, 0, hd);
    expect(!buffer.hasDynamicData() && buffer.dynamicSets.empty(),
           "HD content of an ordinary project carries no dynamic data");
    expect(buffer.originals[0] == document.originalFrame(0).pixels[at(Size{128, 32}, 4, 3)],
           "an HD pixel's original is the ROM pixel at half its coordinates");
    expect(throws<std::logic_error>([&] { (void)edit::copySelection(document, 1, hd); }),
           "a frame without HD has nothing to copy on the HD plane");
}

void testPaste()
{
    const Size rom{128, 32};
    document::IndexedFrame flat = document::makeIndexedFrame(rom, 1);
    document::IndexedFrame other = document::makeIndexedFrame(rom, 1);
    other.pixels[at(rom, 21, 11)] = 3;
    ProjectDocument document =
        makeDocument(PlaneLayout::Ordinary, {flat, other, flat}, {0, 2});
    addDynamicZone(document, 0);
    const std::string before = cromBytes(document);
    const Resolution sd = Resolution::Sd;
    PlaneSelection selection(sd, rom);
    selection.add({Point{4, 3}, Point{5, 3}, Point{6, 3}, Point{4, 4}});
    const edit::CopyBuffer buffer = edit::copySelection(document, 0, selection);

    // Plain paste with its box at 20,10 on frames 1 and 2: colors, mask and set 2's colors.
    edit::PastePlacement placement{20, 10, false, false};
    edit::UndoHistories histories;
    for (std::size_t frameIndex : {std::size_t{1}, std::size_t{2}}) {
        edit::FrameContentChanges changes = edit::makePasteChanges(
            document, buffer, frameIndex, sd, placement, edit::PasteMode::All);
        expect(changes.pixels.size() == 4, "four pixels are pasted");
        applyChanges(document, histories, frameIndex, std::move(changes));
    }
    for (std::size_t frameIndex : {std::size_t{1}, std::size_t{2}}) {
        expect(document.colorizedPixel(frameIndex, sd, 20, 10) ==
                       document.colorizedPixel(0, sd, 4, 3) &&
                   document.colorizedPixel(frameIndex, sd, 22, 10) ==
                       document.colorizedPixel(0, sd, 6, 3) &&
                   document.colorizedPixel(frameIndex, sd, 20, 11) ==
                       document.colorizedPixel(0, sd, 4, 4),
               "the colors land at the placement");
        expect(document.dynamicMaskPixel(frameIndex, sd, 21, 10) == 2 &&
                   document.dynamicMaskPixel(frameIndex, sd, 20, 11) == 255,
               "the dynamic mask comes along");
        expect(document.dynamicColor(frameIndex, sd, 2, 1) == 0xA1 &&
                   document.dynamicShadowDirection(frameIndex, sd, 2) == 5 &&
                   document.dynamicShadowColor(frameIndex, sd, 2) == 0x1234,
               "the used dynamic set's colors and shadow come along (CopyDynaCol)");
        expect(document.dynamicColor(frameIndex, sd, 3, 1) == 0, "unused sets stay");
    }
    // Undo frame by frame restores the project byte for byte.
    while (histories.undo(edit::HistoryKey{edit::EditKind::FramePixels, 2, sd}, document)) {
    }
    while (histories.undo(edit::HistoryKey{edit::EditKind::FramePixels, 1, sd}, document)) {
    }
    expect(cromBytes(document) == before, "undoing every frame's paste restores the project");

    // Mirrored horizontally and vertically: box pixel (0,0) takes the copied (2,1).
    edit::PastePlacement mirrored{30, 12, true, true};
    edit::FrameContentChanges changes =
        edit::makePasteChanges(document, buffer, 1, sd, mirrored, edit::PasteMode::All);
    applyChanges(document, histories, 1, std::move(changes));
    expect(document.colorizedPixel(1, sd, 32, 12) == document.colorizedPixel(0, sd, 4, 4) &&
               document.colorizedPixel(1, sd, 30, 13) == document.colorizedPixel(0, sd, 6, 3) &&
               document.colorizedPixel(1, sd, 32, 13) == document.colorizedPixel(0, sd, 4, 3),
           "mirroring turns the box");
    expect(edit::pasteSourceIndex(buffer, mirrored, 0, 0) == at(Size{3, 2}, 2, 1),
           "the source index of a mirrored box");
    histories.undo(edit::HistoryKey{edit::EditKind::FramePixels, 1, sd}, document);

    // Clipping at the edges: a box at -2,0 keeps the pixels that land inside.
    changes = edit::makePasteChanges(document, buffer, 1, sd, edit::PastePlacement{-2, 0},
                                     edit::PasteMode::All);
    expect(changes.pixels.size() == 1 && changes.pixels[0].x == 0 && changes.pixels[0].y == 0,
           "a box over the left edge is clipped");
    changes = edit::makePasteChanges(document, buffer, 1, sd, edit::PastePlacement{126, 31},
                                     edit::PasteMode::All);
    expect(changes.pixels.size() == 2, "a box over the bottom right corner is clipped");

    // Only where the originals match: frame 1 has another shade at 21,11.
    document::IndexedFrame shades = document::makeIndexedFrame(rom, 1);
    ProjectDocument matching = makeDocument(PlaneLayout::Ordinary, {shades, other}, {});
    PlaneSelection block(sd, rom);
    block.add({Point{20, 10}, Point{21, 10}, Point{20, 11}, Point{21, 11}});
    const edit::CopyBuffer blockBuffer = edit::copySelection(matching, 0, block);
    changes = edit::makePasteChanges(matching, blockBuffer, 1, sd, edit::PastePlacement{20, 10},
                                     edit::PasteMode::WhereOriginalsMatch);
    expect(changes.pixels.size() == 3, "the pixel whose original differs is left out");
    for (const edit::PixelChange& change : changes.pixels) {
        expect(!(change.x == 21 && change.y == 11), "not the differing pixel");
    }

    // Dynamic mask only: no color changes, the mask is written, sets left alone.
    changes = edit::makePasteChanges(document, buffer, 1, sd, placement,
                                     edit::PasteMode::DynamicMaskOnly);
    expect(changes.pixels.empty() && changes.dynamicSets.empty() &&
               changes.dynamicMask.size() == 3,
           "the dynamic-mask-only paste writes the mask alone");

    // Refusals: the HD plane, and dynamic-only content without a dynamic mask.
    expect(edit::pasteRefusal(document, buffer, Resolution::Hd, edit::PasteMode::All).has_value(),
           "SD content is refused on the HD plane");
    expect(throws<std::invalid_argument>([&] {
               (void)edit::makePasteChanges(document, buffer, 0, Resolution::Hd, placement,
                                            edit::PasteMode::All);
           }),
           "pasting on another plane throws");
    PlaneSelection hdSelection(Resolution::Hd, Size{256, 64});
    hdSelection.add({Point{1, 1}});
    const edit::CopyBuffer hdBuffer = edit::copySelection(document, 0, hdSelection);
    expect(edit::pasteRefusal(document, hdBuffer, sd, edit::PasteMode::All).has_value(),
           "HD content is refused on the SD plane");
    expect(edit::pasteRefusal(document, hdBuffer, Resolution::Hd, edit::PasteMode::DynamicMaskOnly)
               .has_value(),
           "no dynamic paste on the HD plane of an ordinary project");
    expect(edit::makePasteChanges(document, hdBuffer, 1, Resolution::Hd, placement,
                                  edit::PasteMode::All)
               .isEmpty(),
           "a frame without HD is left alone");
    changes = edit::makePasteChanges(document, hdBuffer, 2, Resolution::Hd,
                                     edit::PastePlacement{40, 40}, edit::PasteMode::All);
    expect(changes.pixels.size() == 1 && !changes.changesDynamicData(),
           "HD paste on a frame with HD: colors only");

    // Another project layout is refused.
    ProjectDocument big = makeDocument(PlaneLayout::NativeExtra, {patterned(Size{256, 64}, 0)});
    expect(edit::pasteRefusal(big, buffer, Resolution::Hd, edit::PasteMode::All).has_value(),
           "content of another layout is refused");
}

void testPasteNativeLayouts()
{
    for (PlaneLayout layout : {PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        const ProjectGeometry geometry(layout);
        const Size rom = geometry.romFrameSize();
        ProjectDocument document = makeDocument(layout, {patterned(rom, 0), patterned(rom, 3)});
        addDynamicZone(document, 0);
        const Resolution plane = document.dynamicPlane();
        PlaneSelection selection(plane, rom);
        selection.add({Point{4, 3}, Point{rom.width - 1, rom.height - 1}});
        const edit::CopyBuffer buffer = edit::copySelection(document, 0, selection);
        const std::string before = cromBytes(document);
        edit::UndoHistories histories;
        edit::FrameContentChanges changes = edit::makePasteChanges(
            document, buffer, 1, plane, edit::PastePlacement{0, 0}, edit::PasteMode::All);
        expect(!changes.pixels.empty() && !changes.dynamicMask.empty(),
               std::string(layoutName(layout)) + ": paste on the native plane");
        applyChanges(document, histories, 1, std::move(changes));
        expect(document.colorizedPixel(1, plane, 0, 0) == document.colorizedPixel(0, plane, 4, 3) &&
                   document.dynamicMaskPixel(1, plane, 0, 0) == 2,
               std::string(layoutName(layout)) + ": the pasted pixel");
        histories.undo(edit::HistoryKey{edit::EditKind::FramePixels, 1, plane}, document);
        expect(cromBytes(document) == before, std::string(layoutName(layout)) + ": undo");
    }
}

int highestBit(uint32_t value)
{
    int bit = -1;
    while (value != 0) {
        value >>= 1;
        ++bit;
    }
    return bit;
}

// A difference d of bytes 0..3 with crc(a ^ d) == crc(a) for every a of the same length: CRC32
// is affine in its input over GF(2), so d is a nonzero solution of crc(d) == crc(0...0), found
// by Gaussian elimination over the two low bits of each byte.
std::vector<uint8_t> crcCollisionDifference(std::size_t length)
{
    const auto crc = [](const std::vector<uint8_t>& bytes) {
        document::IndexedFrame frame{Size{static_cast<int>(bytes.size()), 1}, bytes};
        return edit::maskedOriginalHash(frame, std::vector<uint8_t>(bytes.size(), 1));
    };
    const uint32_t zero = crc(std::vector<uint8_t>(length, 0));
    struct Row {
        uint32_t value = 0;
        uint64_t combination = 0;
    };
    // Each row's highest bit is its pivot, distinct among the rows.
    std::vector<Row> basis;
    for (std::size_t bit = 0; bit < 2 * length; ++bit) {
        std::vector<uint8_t> bytes(length, 0);
        bytes[bit / 2] = static_cast<uint8_t>(1u << (bit % 2));
        Row row{crc(bytes) ^ zero, uint64_t{1} << bit};
        bool reduced = true;
        while (reduced && row.value != 0) {
            reduced = false;
            for (const Row& existing : basis) {
                if (row.value & (1u << highestBit(existing.value))) {
                    row.value ^= existing.value;
                    row.combination ^= existing.combination;
                    reduced = true;
                }
            }
        }
        if (row.value != 0) {
            basis.push_back(row);
            continue;
        }
        std::vector<uint8_t> difference(length, 0);
        for (std::size_t b = 0; b < 2 * length; ++b) {
            if (row.combination & (uint64_t{1} << b)) {
                difference[b / 2] ^= static_cast<uint8_t>(1u << (b % 2));
            }
        }
        return difference;
    }
    return {};
}

void testAutoCopy()
{
    const Size rom{128, 32};
    // Frames 1 and 3 show what frame 0 shows in row 0, x 0..19; frame 2 differs there by a
    // difference with the same CRC32, frame 4 differs plainly.
    const std::vector<uint8_t> difference = crcCollisionDifference(20);
    expect(difference.size() == 20 &&
               std::any_of(difference.begin(), difference.end(), [](uint8_t v) { return v != 0; }),
           "a CRC32 collision of 20 pixels with shades 0 to 3 exists");
    document::IndexedFrame source = patterned(rom, 0);
    for (int x = 0; x < 20; ++x) {
        source.pixels[at(rom, x, 0)] = 0;
    }
    document::IndexedFrame same = patterned(rom, 1);
    document::IndexedFrame collision = patterned(rom, 2);
    document::IndexedFrame differing = patterned(rom, 3);
    for (int x = 0; x < 20; ++x) {
        same.pixels[at(rom, x, 0)] = 0;
        collision.pixels[at(rom, x, 0)] = difference[static_cast<std::size_t>(x)];
        differing.pixels[at(rom, x, 0)] = 0;
    }
    differing.pixels[at(rom, 7, 0)] = 2;
    ProjectDocument document =
        makeDocument(PlaneLayout::Ordinary, {source, same, collision, same, differing}, {0, 1});
    addDynamicZone(document, 0);
    PlaneSelection selection(Resolution::Sd, rom);
    for (int x = 0; x < 20; ++x) {
        selection.add({Point{x, 0}});
    }
    const std::vector<uint8_t> romMask = edit::selectionRomMask(document.geometry(), selection);
    expect(edit::maskedOriginalHash(document.originalFrame(0), romMask) ==
               edit::maskedOriginalHash(document.originalFrame(2), romMask),
           "frame 2 has the source's masked hash");
    const edit::AutoCopyScan scan = edit::scanAutoCopy(document, 0, selection);
    expect(scan.matches == std::vector<std::size_t>{1, 3}, "frames 1 and 3 match");
    expect(scan.hashCandidates == 3 && scan.hashCollisions == 1,
           "the collision is a hash candidate that the pixel compare drops");
    const edit::AutoCopyScan restricted = edit::scanAutoCopy(document, 0, selection, {2, 3, 4});
    expect(restricted.matches == std::vector<std::size_t>{3}, "restricted to some frames");

    // Apply: the colorized pixels of the selection and the dynamic mask under it.
    selection.add({Point{4, 3}, Point{5, 3}});
    const edit::AutoCopyScan withZone = edit::scanAutoCopy(document, 0, selection);
    expect(withZone.matches.empty(), "a bigger selection compares more pixels");
    selection.remove({Point{4, 3}, Point{5, 3}});
    edit::UndoHistories histories;
    const std::string before = cromBytes(document);
    for (std::size_t frameIndex : scan.matches) {
        edit::FrameContentChanges changes =
            edit::makeAutoCopyChanges(document, 0, frameIndex, selection);
        expect(changes.pixels.size() == 20 && changes.dynamicSets.empty(),
               "auto copy writes the selected pixels, not the dynamic colors");
        applyChanges(document, histories, frameIndex, std::move(changes));
        for (int x = 0; x < 20; ++x) {
            expect(document.colorizedPixel(frameIndex, Resolution::Sd, x, 0) ==
                       document.colorizedPixel(0, Resolution::Sd, x, 0),
                   "copied pixel");
        }
        expect(document.colorizedPixel(frameIndex, Resolution::Sd, 30, 0) !=
                   document.colorizedPixel(0, Resolution::Sd, 30, 0),
               "pixels outside the selection stay");
    }
    for (std::size_t frameIndex : scan.matches) {
        histories.undo(edit::HistoryKey{edit::EditKind::FramePixels, frameIndex, Resolution::Sd},
                       document);
    }
    expect(cromBytes(document) == before, "undo restores the project");

    // The dynamic mask goes with the pixels on the dynamic plane.
    PlaneSelection zone(Resolution::Sd, rom);
    zone.add({Point{4, 3}, Point{5, 3}});
    edit::FrameContentChanges zoneChanges = edit::makeAutoCopyChanges(document, 0, 1, zone);
    expect(zoneChanges.dynamicMask.size() == 2 && zoneChanges.dynamicMask[0].newValue == 2,
           "the dynamic mask is copied");

    // HD selection: frame 3 has no HD and is counted, not matched.
    PlaneSelection hd(Resolution::Hd, Size{256, 64});
    for (int x = 0; x < 40; ++x) {
        hd.add({Point{x, 0}, Point{x, 1}});
    }
    const edit::AutoCopyScan hdScan = edit::scanAutoCopy(document, 0, hd);
    expect(hdScan.matches == std::vector<std::size_t>{1} && hdScan.withoutPlane == 1,
           "HD: frames without HD are left out");
    edit::FrameContentChanges hdChanges = edit::makeAutoCopyChanges(document, 0, 1, hd);
    expect(hdChanges.pixels.size() == 80 && !hdChanges.changesDynamicData(),
           "HD: colors only, the dynamic zones are the SD plane's");
}

uint8_t noise(int x, int y)
{
    uint32_t value = static_cast<uint32_t>(x + 1000) * 2654435761u ^
                     static_cast<uint32_t>(y + 1000) * 2246822519u;
    value ^= value >> 15;
    value *= 2654435761u;
    return static_cast<uint8_t>((value >> 13) % 4);
}

// Scrolling pictures: frame k shows the picture moved by k steps.
void testScrollFill()
{
    // Vertical scroll in a 128x32 project: a tall picture moving up two rows per frame.
    const Size rom{128, 32};
    const auto tall = [](int x, int y) { return noise(x, y); };
    std::vector<document::IndexedFrame> originals;
    for (int frame = 0; frame < 5; ++frame) {
        document::IndexedFrame original = document::makeIndexedFrame(rom, 0);
        for (int y = 0; y < rom.height; ++y) {
            for (int x = 0; x < rom.width; ++x) {
                original.pixels[at(rom, x, y)] = tall(x, y + 2 * frame);
            }
        }
        originals.push_back(original);
    }
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, originals, {0, 1, 2, 3, 4});
    addDynamicZone(document, 0);
    // First and last frame colorized by their picture row: 1000 + row on the first, 2000 + row
    // on the last, so that it shows which reference a row came from.
    for (std::size_t frame : {std::size_t{0}, std::size_t{4}}) {
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 128; ++x) {
                document.setColorizedPixel(
                    frame, Resolution::Sd, x, y,
                    static_cast<uint16_t>((frame == 0 ? 1000 : 2000) + y + 2 * int(frame)));
            }
        }
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 256; ++x) {
                document.setColorizedPixel(frame, Resolution::Hd, x, y,
                                           static_cast<uint16_t>(5000 + y + 4 * int(frame)));
            }
        }
    }
    const std::optional<edit::ScrollMatch> match =
        edit::findScroll(document.originalFrame(0), document.originalFrame(1));
    expect(match && match->direction == edit::ScrollDirection::CameraDown && match->shift == 2,
           "frame 1 is frame 0 moved up by 2 rows");
    const std::optional<edit::ScrollMatch> back =
        edit::findScroll(document.originalFrame(4), document.originalFrame(3));
    expect(back && back->direction == edit::ScrollDirection::CameraUp && back->shift == 2,
           "frame 3 is frame 4 moved down by 2 rows");

    expect(edit::selectedRunAround({0, 1, 2, 3, 4, 7}, 2) == std::make_pair(std::size_t{0},
                                                                            std::size_t{4}),
           "the run of selected frames around the current one");
    expect(edit::selectedRunAround({3}, 5) == std::make_pair(std::size_t{5}, std::size_t{5}),
           "an unselected current frame is a run of itself");

    std::vector<edit::ScrollFillFrame> fill = edit::makeScrollFill(document, Resolution::Sd, 0, 4);
    expect(fill.size() == 3, "three frames between");
    edit::UndoHistories histories;
    const std::string before = cromBytes(document);
    for (edit::ScrollFillFrame& frame : fill) {
        expect(frame.fromFirst && frame.fromLast, "both references match");
        applyChanges(document, histories, frame.frameIndex, std::move(frame.changes));
    }
    for (std::size_t frame = 1; frame <= 3; ++frame) {
        for (int y = 0; y < 32; ++y) {
            // Frame 4 covers rows 2 * (4 - frame) on and wins there; frame 0 the rows above.
            const int base = y >= 2 * (4 - int(frame)) ? 2000 : 1000;
            expect(document.colorizedPixel(frame, Resolution::Sd, 50, y) ==
                       static_cast<uint16_t>(base + y + 2 * int(frame)),
                   "each row has the color of its picture row, frame 4's where both apply");
        }
    }
    // Frame 0's zone in row 3 is row 1 of frame 1, which frame 4 does not cover; frame 4's
    // rows have no zone and clear the frame's own there.
    expect(document.dynamicMaskPixel(1, Resolution::Sd, 4, 1) == 2 &&
               document.dynamicMaskPixel(2, Resolution::Sd, 4, 1) == 255,
           "the dynamic mask moves with the rows");
    for (std::size_t frame = 3; frame >= 1; --frame) {
        histories.undo(edit::HistoryKey{edit::EditKind::FramePixels, frame, Resolution::Sd},
                       document);
    }
    expect(cromBytes(document) == before, "undo restores the project");

    // Only the first frame applies when the last is no scroll of the others: the dynamic zone
    // of frame 0 (row 3) moves to row 1 of frame 1.
    std::vector<document::IndexedFrame> firstOnly = originals;
    firstOnly[4] = patterned(rom, 1);
    ProjectDocument firstDocument = makeDocument(PlaneLayout::Ordinary, firstOnly, {0, 1, 2, 3, 4});
    addDynamicZone(firstDocument, 0);
    fill = edit::makeScrollFill(firstDocument, Resolution::Sd, 0, 4);
    expect(fill[0].fromFirst && !fill[0].fromLast, "only the first reference matches");
    applyChanges(firstDocument, histories, 1, std::move(fill[0].changes));
    expect(firstDocument.dynamicMaskPixel(1, Resolution::Sd, 4, 1) == 2 &&
               firstDocument.colorizedPixel(1, Resolution::Sd, 4, 29) ==
                   firstDocument.colorizedPixel(0, Resolution::Sd, 4, 31) &&
               firstDocument.colorizedPixel(1, Resolution::Sd, 4, 31) !=
                   firstDocument.colorizedPixel(0, Resolution::Sd, 4, 31),
           "rows move by the shift and the uncovered rows stay");

    // The HD plane of an ordinary project moves by twice the shift.
    fill = edit::makeScrollFill(document, Resolution::Hd, 0, 4);
    for (edit::ScrollFillFrame& frame : fill) {
        expect(!frame.changes.changesDynamicData(), "HD: no dynamic data");
        applyChanges(document, histories, frame.frameIndex, std::move(frame.changes));
    }
    for (std::size_t frame = 1; frame <= 3; ++frame) {
        for (int y = 0; y < 64; ++y) {
            expect(document.colorizedPixel(frame, Resolution::Hd, 100, y) ==
                       static_cast<uint16_t>(5000 + y + 4 * int(frame)),
                   "HD rows move by twice the shift");
        }
    }

    // Horizontal scroll in a 192x64 project: the picture moves right by 3 columns per frame.
    const Size native{192, 64};
    const auto wide = [](int x, int y) { return noise(x, y); };
    std::vector<document::IndexedFrame> wideOriginals;
    for (int frame = 0; frame < 3; ++frame) {
        document::IndexedFrame original = document::makeIndexedFrame(native, 0);
        for (int y = 0; y < native.height; ++y) {
            for (int x = 0; x < native.width; ++x) {
                original.pixels[at(native, x, y)] = wide(x - 3 * frame + 300, y);
            }
        }
        wideOriginals.push_back(original);
    }
    ProjectDocument wideDocument = makeDocument(PlaneLayout::NativeOriginal, wideOriginals);
    const std::optional<edit::ScrollMatch> left =
        edit::findScroll(wideDocument.originalFrame(0), wideDocument.originalFrame(1));
    expect(left && left->direction == edit::ScrollDirection::CameraLeft && left->shift == 3,
           "content moving right is a camera moving left");
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 192; ++x) {
            wideDocument.setColorizedPixel(0, Resolution::Sd, x, y, static_cast<uint16_t>(x + 300));
            wideDocument.setColorizedPixel(2, Resolution::Sd, x, y,
                                           static_cast<uint16_t>(x - 6 + 300));
        }
    }
    fill = edit::makeScrollFill(wideDocument, Resolution::Sd, 0, 2);
    expect(fill.size() == 1 && fill[0].fromLast &&
               fill[0].fromLast->direction == edit::ScrollDirection::CameraRight &&
               fill[0].fromLast->shift == 3,
           "the last frame is the middle one moved right");
    applyChanges(wideDocument, histories, 1, std::move(fill[0].changes));
    for (int x = 0; x < 192; ++x) {
        expect(wideDocument.colorizedPixel(1, Resolution::Sd, x, 10) ==
                   static_cast<uint16_t>(x - 3 + 300),
               "every column has the color of its picture column");
    }
}

void testRecolorHelpers()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        const ProjectGeometry geometry(layout);
        const Size rom = geometry.romFrameSize();
        document::IndexedFrame original = patterned(rom, 0);
        ProjectDocument document = makeDocument(layout, {original, patterned(rom, 1)}, {0, 1});
        addDynamicZone(document, 0);
        const std::string name = layoutName(layout);
        const Resolution plane = document.dynamicPlane();
        const Size planeSize = geometry.planeSize(plane);

        // Wand: every pixel of a color, with the draw color and with set replace.
        const uint16_t color = document.colorizedPixel(0, plane, 10, 2);
        std::size_t expected = 0;
        for (uint16_t value : document.colorizedFrame(0, plane).pixels) {
            expected += value == color ? 1 : 0;
        }
        edit::FrameContentChanges changes =
            edit::makeRecolorChanges(document, 0, plane, color, [](Point) { return uint16_t{7}; });
        expect(changes.pixels.size() == expected && expected > 1, name + ": wand recolor");
        const edit::PixelColorSource replace = edit::colorSetReplaceColorSource(document, 0, plane);
        changes = edit::makeRecolorChanges(document, 0, plane, color, replace);
        for (const edit::PixelChange& change : changes.pixels) {
            const uint8_t shade = document.originalFrame(0).pixels[at(rom, change.x, change.y)];
            expect(change.newColor == document.editColor(shade), name + ": set replace wand");
        }
        expect(edit::isDynamicPixel(document, 0, plane, Point{5, 3}) &&
                   !edit::isDynamicPixel(document, 0, plane, Point{5, 4}),
               name + ": dynamic pixels");

        // Black original to black, whole frame and in a selection.
        changes = edit::makeBlackOriginalToBlackChanges(document, 0, plane, nullptr);
        std::size_t blacks = 0;
        for (uint8_t shade : document.originalFrame(0).pixels) {
            blacks += shade == 0 ? 1 : 0;
        }
        expect(changes.pixels.size() == blacks, name + ": black original to black");
        PlaneSelection area(plane, planeSize);
        area.add({Point{0, 0}, Point{1, 0}, Point{2, 0}, Point{3, 0}});
        changes = edit::makeBlackOriginalToBlackChanges(document, 0, plane, &area);
        expect(changes.pixels.size() == 1 && changes.pixels[0].x == 0 &&
                   changes.pixels[0].newColor == 0,
               name + ": black original to black in the selection");

        // Remove the dynamic zone in a selection, then everywhere.
        PlaneSelection zone(plane, planeSize);
        zone.add({Point{4, 3}});
        changes = edit::makeRemoveDynamicChanges(document, 0, &zone);
        const std::vector<uint16_t> plain = document::plainOriginalColors(4);
        expect(changes.dynamicMask.size() == 1 && changes.pixels.size() <= 1,
               name + ": remove the zone in the selection");
        edit::UndoHistories histories;
        applyChanges(document, histories, 0, std::move(changes));
        expect(document.dynamicMaskPixel(0, plane, 4, 3) == 255 &&
                   document.colorizedPixel(0, plane, 4, 3) ==
                       plain[document.originalFrame(0).pixels[at(rom, 4, 3)]],
               name + ": the pixel gets its plain color");
        changes = edit::makeRemoveDynamicChanges(document, 0, nullptr);
        expect(changes.dynamicMask.size() == 2, name + ": remove the rest");
        changes = edit::makeAssignDynamicSetChanges(document, 1, &zone, 7);
        expect(changes.dynamicMask.size() == 1 && changes.dynamicMask[0].newValue == 7,
               name + ": assign a set to the selection");
        changes = edit::makeAssignDynamicSetChanges(document, 1, nullptr, 7);
        expect(changes.dynamicMask.size() == static_cast<std::size_t>(planeSize.width) *
                                                 static_cast<std::size_t>(planeSize.height),
               name + ": assign a set to the frame");
        expect(throws<std::out_of_range>(
                   [&] { (void)edit::makeAssignDynamicSetChanges(document, 1, nullptr, 40); }),
               name + ": the set must exist");
    }

    // The HD plane of an ordinary project maps to the ROM pixel at half the coordinates.
    const Size rom{128, 32};
    document::IndexedFrame original = document::makeIndexedFrame(rom, 2);
    original.pixels[at(rom, 3, 1)] = 0;
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, {original}, {0});
    const edit::FrameContentChanges changes =
        edit::makeBlackOriginalToBlackChanges(document, 0, Resolution::Hd, nullptr);
    expect(changes.pixels.size() == 4, "HD: a black ROM pixel blacks its 2x2 block");
    for (const edit::PixelChange& change : changes.pixels) {
        expect(change.x / 2 == 3 && change.y / 2 == 1, "HD: the block of the black pixel");
    }
    PlaneSelection sdArea(Resolution::Sd, rom);
    expect(throws<std::invalid_argument>([&] {
               (void)edit::makeBlackOriginalToBlackChanges(document, 0, Resolution::Hd, &sdArea);
           }),
           "a selection of another plane is refused");
    addDynamicZone(document, 0);
    expect(edit::isDynamicPixel(document, 0, Resolution::Hd, Point{9, 7}),
           "an HD pixel over an SD zone shows dynamic content");
}

void testMoreHelpers()
{
    const Size rom{128, 32};
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary,
                                            {patterned(rom, 0), patterned(rom, 1)}, {0, 1});
    addDynamicZone(document, 0);
    const Resolution plane = Resolution::Sd;

    // Dynamic to static: the zone's pixels keep their set's color for their shade.
    edit::FrameContentChanges changes = edit::makeDynamicToStaticChanges(document, 0, nullptr);
    expect(changes.dynamicMask.size() == 3, "dyn to static leaves the zone");
    for (const edit::PixelChange& change : changes.pixels) {
        const uint8_t shade = document.originalFrame(0).pixels[at(rom, change.x, change.y)];
        expect(change.newColor == 0xA0 + shade, "dyn to static keeps the set's color");
    }
    PlaneSelection one(plane, rom);
    one.add({Point{5, 3}});
    changes = edit::makeDynamicToStaticChanges(document, 0, &one);
    expect(changes.dynamicMask.size() == 1, "dyn to static in the selection");

    // Brightness, as CDMD's Brightness(): channels scaled and rounded down, at least a step.
    expect(edit::brightenedColor(0x0841, 2.0) == 0x1082, "brightness doubles the channels");
    expect(edit::brightenedColor(0x0841, 1.05) == 0x1062, "a small factor still moves a step");
    expect(edit::brightenedColor(0xffff, 1.5) == 0xffff, "up to white");
    expect(edit::brightenedColor(0x0000, 0.5) == 0x0000, "down to black");
    expect(edit::brightenedColor(0x1234, 1.0) == 0x1234, "100 percent changes nothing");
    PlaneSelection area(plane, rom);
    area.add({Point{20, 10}, Point{21, 10}});
    changes = edit::makeBrightnessChanges(document, 1, plane, area, 0.5);
    for (const edit::PixelChange& change : changes.pixels) {
        expect(area.contains(Point{change.x, change.y}) &&
                   change.newColor == edit::brightenedColor(change.oldColor, 0.5),
               "brightness on the selected pixels");
    }

    // A shadow next to the selection, not on dynamic pixels.
    PlaneSelection dot(plane, rom);
    dot.add({Point{10, 10}});
    changes = edit::makeSelectionShadowChanges(document, 1, plane, dot,
                                               edit::SelectionDirection::AllAround, 0x0001);
    std::size_t shadowed = 0;
    for (const edit::PixelChange& change : changes.pixels) {
        shadowed += change.newColor == 0x0001 && std::abs(change.x - 10) <= 1 &&
                            std::abs(change.y - 10) <= 1 && !(change.x == 10 && change.y == 10)
                        ? 1
                        : 0;
    }
    expect(shadowed == changes.pixels.size() && shadowed >= 7, "a shadow all around");
    changes = edit::makeSelectionShadowChanges(document, 1, plane, dot,
                                               edit::SelectionDirection::Left, 0x0001);
    expect(changes.pixels.size() <= 1 &&
               (changes.pixels.empty() || (changes.pixels[0].x == 9 && changes.pixels[0].y == 10)),
           "a shadow to the left");
    PlaneSelection besideZone(plane, rom);
    besideZone.add({Point{7, 3}});
    changes = edit::makeSelectionShadowChanges(document, 0, plane, besideZone,
                                               edit::SelectionDirection::Left, 0x0001);
    expect(changes.pixels.empty(), "no shadow on a dynamic pixel");

    // Growing the selection, clipped at the edges.
    PlaneSelection corner(plane, rom);
    corner.add({Point{0, 0}});
    expect(edit::grownSelection(corner, edit::SelectionDirection::AllAround).count() == 4,
           "grown all around at a corner");
    expect(edit::grownSelection(corner, edit::SelectionDirection::Right).count() == 2,
           "grown to the right");
    expect(edit::grownSelection(corner, edit::SelectionDirection::Left).count() == 1,
           "nothing left of the edge");
}

}  // namespace

void runSelectionEditTests()
{
    testPlaneSelection();
    testCopyBuffers();
    testPaste();
    testPasteNativeLayouts();
    testAutoCopy();
    testScrollFill();
    testRecolorHelpers();
    testMoreHelpers();
}
