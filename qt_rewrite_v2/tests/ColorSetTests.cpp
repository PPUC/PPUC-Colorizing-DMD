#include "edit/ColorModel.h"
#include "edit/ColorSetEdits.h"

#include <cmath>
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
#include "edit/DrawingTools.h"
#include "edit/UndoHistories.h"
#include "legacy/LegacyProjectSerializer.h"
#include "render/ProjectGeometry.h"

namespace document = colorizing::document;
namespace edit = colorizing::edit;
using colorizing::render::PlaneLayout;
using colorizing::render::Point;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;
using document::ColorSetTables;
using document::ProjectDocument;
using document::ProjectSnapshot;
using edit::DrawColorMode;
using edit::Rgb888;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: color sets: " << message << '\n';
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

uint8_t shadeAt(int x, int y, uint32_t colorCount)
{
    return static_cast<uint8_t>((x * 7 + y * 3) % static_cast<int>(colorCount));
}

document::IndexedFrame patternedOriginal(Size size, uint32_t colorCount, int frameIndex)
{
    document::IndexedFrame frame = document::makeIndexedFrame(size, 0);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            frame.pixels[static_cast<std::size_t>(y * size.width + x)] =
                static_cast<uint8_t>((shadeAt(x, y, colorCount) + frameIndex) % colorCount);
        }
    }
    return frame;
}

// Two frames with different originals; the dynamic data of frame 0 only.
ProjectSnapshot makeSnapshot(PlaneLayout layout, uint32_t colorCount)
{
    const ProjectGeometry geometry(layout);
    ProjectSnapshot snapshot;
    snapshot.romName = "colorsets";
    snapshot.colorCount = colorCount;
    snapshot.geometry = geometry;
    const Size original = geometry.romFrameSize();
    const Size sd = geometry.planeSize(Resolution::Sd);
    const Size hd = geometry.planeSize(Resolution::Hd);
    for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
        document::ProjectFrameSnapshot frame;
        frame.originalSd = patternedOriginal(original, colorCount, frameIndex);
        frame.colorizedSd = document::makeRgb565Frame(sd, 0);
        if (layout != PlaneLayout::NativeOriginal) {
            frame.colorizedHd = document::makeRgb565Frame(hd, 0);
        }
        frame.comparisonMaskId = 255;
        snapshot.frames.push_back(frame);
    }
    document::FrameDynamicData dynamicData;
    dynamicData.maskSd = document::makeIndexedFrame(sd, 255);
    dynamicData.colorsSd.assign(document::kMaxDynamicSetsPerFrame * colorCount, 0);
    dynamicData.maskHd = document::makeIndexedFrame(hd, 255);
    dynamicData.colorsHd.assign(document::kMaxDynamicSetsPerFrame * colorCount, 0);
    snapshot.frames[0].dynamicData = dynamicData;
    snapshot.palettes.assign(document::kPaletteCount,
                             document::PaletteRecord{std::vector<uint16_t>(64, 0), ""});
    for (std::size_t slot = 0; slot < document::kEditColorCount; ++slot) {
        snapshot.editorMetadata.editColors[slot] = static_cast<uint16_t>(0x1000 + slot);
    }
    return snapshot;
}

ProjectDocument makeDocument(PlaneLayout layout, uint32_t colorCount)
{
    ProjectDocument document = ProjectDocument::fromSnapshot(makeSnapshot(layout, colorCount));
    document.markClean();
    return document;
}

std::vector<uint8_t> crpBytes(const ProjectSnapshot& snapshot)
{
    std::ostringstream stream;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrp(stream, snapshot, error), "cRP writes: " + error);
    const std::string data = stream.str();
    return std::vector<uint8_t>(data.begin(), data.end());
}

void testRgb565RoundTrips()
{
    for (uint32_t value = 0; value <= 0xffff; ++value) {
        const auto color = static_cast<uint16_t>(value);
        const Rgb888 rgb = edit::rgb565ToRgb888(color);
        expect(edit::rgb888ToRgb565(rgb) == color, "RGB565 -> RGB888 -> RGB565");
        expect(rgb.red == edit::expand5To8(static_cast<uint8_t>(color >> 11)) &&
                   rgb.green == edit::expand6To8(static_cast<uint8_t>((color >> 5) & 0x3f)) &&
                   rgb.blue == edit::expand5To8(static_cast<uint8_t>(color & 0x1f)),
               "the R5/G6/B5 expansion is the RGB565 one");
        const edit::Hsv shown = edit::roundedHsv(edit::rgb888ToHsv(rgb));
        expect(edit::rgb888ToRgb565(edit::hsvToRgb888(shown)) == color,
               "RGB565 survives H/S/V shown with one decimal and entered again: " +
                   std::to_string(value));
        const std::optional<Rgb888> fromHex = edit::rgb888FromHex(edit::rgb888ToHex(rgb));
        expect(fromHex && edit::rgb888ToRgb565(*fromHex) == color, "RGB565 through hex");
        // Each 5/6 bit field entered on its own changes its component only.
        const Rgb888 redOnly{edit::expand5To8(static_cast<uint8_t>(color >> 11)), 0, 0};
        expect(edit::rgb888ToRgb565(redOnly) == (color & 0xf800), "R5 field");
    }
    std::size_t wholeNumberFailures = 0;
    for (uint32_t value = 0; value <= 0xffff; ++value) {
        const edit::Hsv hsv = edit::rgb888ToHsv(edit::rgb565ToRgb888(static_cast<uint16_t>(value)));
        const edit::Hsv whole{std::round(hsv.hue), std::round(hsv.saturation),
                              std::round(hsv.value)};
        if (edit::rgb888ToRgb565(edit::hsvToRgb888(whole)) != value) {
            ++wholeNumberFailures;
        }
    }
    expect(wholeNumberFailures > 10000,
           "whole-number H/S/V lose colors, which is why one decimal is shown: " +
               std::to_string(wholeNumberFailures));
}

void testRgb888HsvRoundTrip()
{
    for (int red = 0; red < 256; ++red) {
        for (int green = 0; green < 256; ++green) {
            for (int blue = 0; blue < 256; ++blue) {
                const Rgb888 color{static_cast<uint8_t>(red),
                                   static_cast<uint8_t>(green),
                                   static_cast<uint8_t>(blue)};
                if (edit::hsvToRgb888(edit::rgb888ToHsv(color)) != color) {
                    expect(false, "RGB888 -> HSV -> RGB888");
                }
            }
        }
    }
}

void testHsvAndHexDetails()
{
    const edit::Hsv red = edit::rgb888ToHsv(Rgb888{255, 0, 0});
    expect(red.hue == 0.0 && red.saturation == 100.0 && red.value == 100.0, "red");
    const edit::Hsv blue = edit::rgb888ToHsv(Rgb888{0, 0, 255});
    expect(blue.hue == 240.0, "blue");
    const edit::Hsv magenta = edit::rgb888ToHsv(Rgb888{255, 0, 128});
    expect(magenta.hue > 329.0 && magenta.hue < 331.0, "hue of a red with blue stays positive");
    expect(edit::hsvToRgb888(edit::Hsv{360.0, 100.0, 100.0}) == Rgb888{255, 0, 0},
           "hue 360 is red");
    expect(edit::hsvToRgb888(edit::Hsv{-120.0, 100.0, 100.0}) == Rgb888{0, 0, 255},
           "negative hue wraps");
    expect(edit::hsvToRgb888(edit::Hsv{50.0, 150.0, -5.0}) == Rgb888{0, 0, 0},
           "saturation and value are clamped");
    expect(edit::roundedHsv(edit::Hsv{359.96, 0.04, 99.95}).hue == 0.0,
           "a hue that rounds to 360 is 0");
    expect(edit::rgb888ToHex(Rgb888{0xab, 0x0c, 0xff}) == "AB0CFF", "hex is upper case");
    expect(edit::rgb888FromHex("#ab0Cff") == Rgb888{0xab, 0x0c, 0xff}, "hex with #, any case");
    expect(!edit::rgb888FromHex("12345") && !edit::rgb888FromHex("1234567") &&
               !edit::rgb888FromHex("zz0000") && !edit::rgb888FromHex(""),
           "bad hex");
    expect(edit::expand5To8(31) == 255 && edit::expand5To8(40) == 255 &&
               edit::expand6To8(63) == 255 && edit::expand6To8(1) == 4,
           "expansion and clamping");
}

void testStoredDrawColorMode()
{
    expect(edit::drawColorModeFromStored(0) == DrawColorMode::SingleColor &&
               edit::drawColorModeFromStored(1) == DrawColorMode::ColorSetReplace &&
               edit::drawColorModeFromStored(2) == DrawColorMode::SingleColor &&
               edit::drawColorModeFromStored(7) == DrawColorMode::SingleColor,
           "stored mode 2 (gradient) reads as single color, as the Win32 loader does");
    expect(edit::storedDrawColorMode(DrawColorMode::ColorSetReplace) == 1 &&
               edit::storedDrawColorMode(DrawColorMode::SingleColor) == 0,
           "stored values");

    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, 4);
    const uint64_t revision = document.revision();
    document.setDrawColorMode(1);
    document.setFirstDisplayedColorSet(56);
    expect(document.editorMetadata().drawColorMode == 1 &&
               document.editorMetadata().firstDisplayedColorSet == 56 && !document.isDirty() &&
               document.revision() == revision,
           "drawing settings are not changes of the project");
    expect(throws<std::out_of_range>([&]() { document.setFirstDisplayedColorSet(64); }),
           "first displayed set out of range");
}

void testColorSetOperations(uint32_t colorCount)
{
    const std::string label = std::to_string(colorCount) + " colors: ";
    ProjectSnapshot snapshot = makeSnapshot(PlaneLayout::Ordinary, colorCount);
    document::EditorMetadata& metadata = snapshot.editorMetadata;
    for (std::size_t index = 0; index < document::kColorSetColorCount; ++index) {
        metadata.colorSets[5 * 16 + index] = 0x0555;
        metadata.colorSets[7 * 16 + index] = static_cast<uint16_t>(0x7000 + index);
    }
    metadata.activeColorSets[7] = 1;
    metadata.activeColorSets[9] = 2;
    const char leftover[] = "x\0old name";
    std::copy(leftover, leftover + sizeof(leftover), metadata.colorSetNames.begin() + 7 * 64);
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    const ColorSetTables original = document.colorSetTables();
    edit::UndoHistories histories;
    const edit::HistoryKey key = edit::editorColorsHistoryKey();
    const auto run = [&](ColorSetTables after) {
        std::unique_ptr<edit::EditCommand> command =
            edit::makeEditorColorsCommand(document, std::move(after));
        if (!command) {
            return false;
        }
        expect(edit::historyKey(command->target()) == key && command->recordReferences().empty(),
               label + "one project history, no record");
        command->apply(document);
        histories.push(key, std::move(command));
        return true;
    };

    expect(run(edit::withEditColor(document.colorSetTables(), 2, 0xf800)) &&
               document.editColor(2) == 0xf800 && document.isDirty(),
           label + "edit color");
    expect(!run(edit::withEditColor(document.colorSetTables(), 2, 0xf800)),
           label + "an unchanged edit color makes no command");

    expect(run(edit::withStoredColorSet(document.colorSetTables(), 5, colorCount)),
           label + "store");
    ColorSetTables tables = document.colorSetTables();
    for (std::size_t index = 0; index < 16; ++index) {
        const uint16_t expected =
            index < colorCount ? document.editColor(index) : uint16_t{0x0555};
        expect(tables.colorSets[5 * 16 + index] == expected,
               label + "store copies the project's colors only");
    }
    expect(tables.activeColorSets[5] == 1, label + "a stored set is active");

    expect(run(edit::withLoadedColorSet(document.colorSetTables(), 7, colorCount)),
           label + "load");
    for (std::size_t index = 0; index < 16; ++index) {
        const uint16_t expected = index < colorCount ? static_cast<uint16_t>(0x7000 + index)
                                                     : static_cast<uint16_t>(0x1000 + index);
        expect(document.editColor(index) == expected,
               label + "load copies the project's colors only");
    }
    expect(!run(edit::withLoadedColorSet(document.colorSetTables(), 3, colorCount)),
           label + "an inactive set does not load");

    expect(edit::colorSetName(document.colorSetTables(), 7) == "x", label + "name read");
    expect(!run(edit::withColorSetName(document.colorSetTables(), 7, "x")),
           label + "the same name keeps the bytes behind it");
    expect(run(edit::withColorSetName(document.colorSetTables(), 7, std::string(70, 'n'))),
           label + "rename");
    tables = document.colorSetTables();
    expect(edit::colorSetName(tables, 7) == std::string(63, 'n') &&
               tables.colorSetNames[7 * 64 + 63] == 0,
           label + "names are cut to 63 bytes");
    expect(run(edit::withColorSetName(document.colorSetTables(), 7, "Red"))
               && document.colorSetTables().colorSetNames[7 * 64 + 5] == 0,
           label + "a new name is zero filled");

    expect(!run(edit::withColorSetActive(document.colorSetTables(), 9, true)),
           label + "a set stored as 2 is active already");
    expect(run(edit::withColorSetActive(document.colorSetTables(), 7, false)),
           label + "deactivate");
    tables = document.colorSetTables();
    expect(tables.activeColorSets[7] == 0 && edit::colorSetName(tables, 7) == "Red" &&
               edit::colorSetColors(tables, 7)[0] == 0x7000,
           label + "deactivating keeps colors and name");

    expect(throws<std::out_of_range>(
               [&]() { static_cast<void>(edit::withStoredColorSet(tables, 64, colorCount)); }),
           label + "set index");
    ColorSetTables bad = tables;
    bad.colorSetNames.pop_back();
    const uint64_t revision = document.revision();
    expect(throws<std::invalid_argument>([&]() { document.setColorSetTables(bad); }) &&
               document.colorSetTables() == tables && document.revision() == revision,
           label + "tables of the wrong size are refused");

    while (histories.undoLatest({key}, document) != nullptr) {
    }
    expect(document.colorSetTables() == original, label + "undo restores every byte");
    std::size_t redone = 0;
    while (histories.redoLatest({key}, document) != nullptr) {
        ++redone;
    }
    expect(redone == 6 && document.colorSetTables() == tables, label + "redo");
}

void testMatchingEditColorSlot()
{
    const std::vector<uint16_t> colors{1, 2, 1, 3, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
    expect(edit::matchingEditColorSlot(colors, 4, 1) == std::optional<std::size_t>(2),
           "the last match of the used slots");
    expect(edit::matchingEditColorSlot(colors, 16, 1) == std::optional<std::size_t>(4),
           "16 colors");
    expect(!edit::matchingEditColorSlot(colors, 4, 4), "a color of an unused slot only");
    expect(!edit::matchingEditColorSlot(colors, 16, 9), "no match");
}

uint16_t expectedReplaceColor(const ProjectDocument& document,
                              std::size_t frameIndex,
                              Point rom)
{
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    return document.editColor(
        original.pixels[static_cast<std::size_t>(rom.y * original.size.width + rom.x)]);
}

void testColorSetReplaceSource(PlaneLayout layout, uint32_t colorCount)
{
    const std::string label = "replace source " + std::to_string(static_cast<int>(layout)) +
                               "/" + std::to_string(colorCount) + ": ";
    const ProjectDocument document = makeDocument(layout, colorCount);
    const ProjectGeometry geometry = document.geometry();
    for (Resolution plane : {Resolution::Sd, Resolution::Hd}) {
        if (!geometry.isEditable(plane)) {
            expect(throws<std::logic_error>([&]() {
                       static_cast<void>(edit::colorSetReplaceColorSource(document, 0, plane));
                   }),
                   label + "a plane without ROM pixels has no source");
            continue;
        }
        const Size size = geometry.planeSize(plane);
        const int scale = geometry.planeScale(plane);
        for (std::size_t frameIndex = 0; frameIndex < 2; ++frameIndex) {
            const edit::PixelColorSource source =
                edit::colorSetReplaceColorSource(document, frameIndex, plane);
            for (int y = 0; y < size.height; ++y) {
                for (int x = 0; x < size.width; ++x) {
                    if (source(Point{x, y}) !=
                        expectedReplaceColor(document, frameIndex, Point{x / scale, y / scale})) {
                        expect(false, label + "the edit color of the shade under the pixel");
                    }
                }
            }
            expect(throws<std::out_of_range>(
                       [&]() { static_cast<void>(source(Point{size.width, 0})); }) &&
                       throws<std::out_of_range>(
                           [&]() { static_cast<void>(source(Point{0, -1})); }),
                   label + "points outside the plane");
        }
        const edit::PixelColorSource first =
            edit::colorSetReplaceColorSource(document, 0, plane);
        const edit::PixelColorSource second =
            edit::colorSetReplaceColorSource(document, 1, plane);
        expect(first(Point{0, 0}) != second(Point{0, 0}),
               label + "each frame uses its own original shades");
    }
}

void testShadesAboveFourIn16ColorProjects()
{
    ProjectSnapshot snapshot = makeSnapshot(PlaneLayout::Ordinary, 16);
    snapshot.frames[0].originalSd.pixels[0] = 15;
    snapshot.frames[0].originalSd.pixels[1] = 9;
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const edit::PixelColorSource source =
        edit::colorSetReplaceColorSource(document, 0, Resolution::Sd);
    expect(source(Point{0, 0}) == 0x100f && source(Point{1, 0}) == 0x1009,
           "16 shades take the 16 edit colors");
}

void testEditColorsAndDynamicSets()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, 4);
    document.setDynamicColor(0, Resolution::Sd, 3, 1, 0x1001);
    std::vector<std::unique_ptr<edit::EditCommand>> commands =
        edit::makeEditColorsToDynamicSetCommands(document, {0, 1}, Resolution::Sd, 3);
    expect(commands.size() == 1, "frames without dynamic data are skipped");
    const edit::EditTarget target = commands[0]->target();
    expect(target.kind == edit::EditKind::DynamicColor && target.index == 0 &&
               target.resolution == Resolution::Sd,
           "the frame's dynamic color history");
    commands[0]->apply(document);
    for (std::size_t index = 0; index < 4; ++index) {
        expect(document.dynamicColor(0, Resolution::Sd, 3, index) == document.editColor(index),
               "Edit -> Dyn");
    }
    expect(document.dynamicColor(0, Resolution::Sd, 2, 0) == 0, "other sets untouched");
    commands[0]->revert(document);
    expect(document.dynamicColor(0, Resolution::Sd, 3, 1) == 0x1001 &&
               document.dynamicColor(0, Resolution::Sd, 3, 0) == 0,
           "undo of Edit -> Dyn");
    commands[0]->apply(document);
    expect(edit::makeEditColorsToDynamicSetCommands(document, {0}, Resolution::Sd, 3).empty(),
           "nothing to copy");
    std::vector<std::unique_ptr<edit::EditCommand>> hd =
        edit::makeEditColorsToDynamicSetCommands(document, {0}, Resolution::Hd, 3);
    expect(hd.size() == 1 &&
               throws<std::logic_error>([&]() { hd[0]->apply(document); }) &&
               document.dynamicColor(0, Resolution::Hd, 3, 0) == 0,
           "the HD dynamic colors of an ordinary project stay unchanged");

    document.setDynamicColor(0, Resolution::Sd, 4, 2, 0xabcd);
    const ColorSetTables tables =
        edit::withEditColorsFromDynamicSet(document, 0, Resolution::Sd, 4);
    expect(tables.editColors[2] == 0xabcd && tables.editColors[0] == 0 &&
               tables.editColors[4] == 0x1004,
           "Dyn -> Edit copies the project's colors");
    expect(edit::withEditColorsFromDynamicSet(document, 1, Resolution::Sd, 4) ==
               document.colorSetTables(),
           "a frame without dynamic data changes nothing");
}

void testUntouchedTablesSerializeUnchanged()
{
    ProjectSnapshot snapshot = makeSnapshot(PlaneLayout::Ordinary, 4);
    document::EditorMetadata& metadata = snapshot.editorMetadata;
    metadata.activeColorSets[0] = 1;
    metadata.activeColorSets[1] = 2;
    metadata.colorSets[17] = 0xbeef;
    const char name[] = "Gold\0\x01\x02";
    std::copy(name, name + sizeof(name), metadata.colorSetNames.begin() + 64);
    metadata.currentColorSet = 3;
    metadata.firstDisplayedColorSet = 4;
    metadata.drawColorMode = 2;
    const std::vector<uint8_t> expected = crpBytes(snapshot);

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    expect(crpBytes(document.toSnapshot()) == expected, "loaded tables are written unchanged");
    edit::UndoHistories histories;
    const edit::HistoryKey key = edit::editorColorsHistoryKey();
    for (ColorSetTables after :
         {edit::withStoredColorSet(document.colorSetTables(), 1, 4),
          edit::withColorSetName(document.colorSetTables(), 1, "Silver"),
          edit::withColorSetActive(document.colorSetTables(), 0, false)}) {
        std::unique_ptr<edit::EditCommand> command =
            edit::makeEditorColorsCommand(document, std::move(after));
        command->apply(document);
        histories.push(key, std::move(command));
    }
    expect(crpBytes(document.toSnapshot()) != expected, "the edits are written");
    while (histories.undoLatest({key}, document) != nullptr) {
    }
    expect(crpBytes(document.toSnapshot()) == expected, "after undo every byte is the file's");
}

}  // namespace

void runColorSetTests()
{
    testRgb565RoundTrips();
    testRgb888HsvRoundTrip();
    testHsvAndHexDetails();
    testStoredDrawColorMode();
    testColorSetOperations(4);
    testColorSetOperations(16);
    testMatchingEditColorSlot();
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        testColorSetReplaceSource(layout, 4);
        testColorSetReplaceSource(layout, 16);
    }
    testShadesAboveFourIn16ColorProjects();
    testEditColorsAndDynamicSets();
    testUntouchedTablesSerializeUnchanged();
}
