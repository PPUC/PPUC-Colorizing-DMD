// Frame dumps: the reader, the import filter and the removal of duplicates, a new project
// from a dump and frames added to a project.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/EditCommand.h"
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"
#include "import/DumpFile.h"
#include "import/FrameDumpImport.h"
#include "import/FrameDumpReader.h"
#include "legacy/LegacyProjectSerializer.h"
#include "runtime/FrameIdentification.h"
#include "runtime/RuntimePreview.h"
#include "legacy/LegacyCrpWriter.h"

#include <miniz/miniz.h>

using colorizing::document::IndexedFrame;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectSnapshot;
using colorizing::importing::DumpFrame;
using colorizing::importing::DumpImportOptions;
using colorizing::importing::DumpImportResult;
using colorizing::importing::FrameDumpReader;
using colorizing::importing::ProjectFrameMatcher;
using colorizing::importing::importFrameDump;
using colorizing::importing::makeProjectFrames;
using colorizing::importing::makeProjectSnapshot;
using colorizing::render::PlaneLayout;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;
using colorizing::runtime::FrameIdentification;
using colorizing::runtime::PlaneRequest;
using colorizing::runtime::RuntimeFrame;
using colorizing::runtime::RuntimePreview;

namespace document = colorizing::document;
namespace edit = colorizing::edit;
namespace importing = colorizing::importing;
namespace legacy = colorizing::legacy;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frame dump: " << message << '\n';
        std::exit(1);
    }
}

template <typename Value>
void expectEqual(const Value& actual, const Value& expected, const std::string& message)
{
    if (!(actual == expected)) {
        std::cerr << "FAIL: frame dump: " << message << ": " << actual << ", expected "
                  << expected << '\n';
        std::exit(1);
    }
}

struct TextFrame {
    uint32_t timecode = 0;
    Size size{128, 32};
    std::vector<uint8_t> pixels;
};

// A frame with shades below `shades`, different for every seed.
TextFrame textFrame(uint32_t timecode, int seed, Size size = Size{128, 32}, int shades = 4)
{
    TextFrame frame;
    frame.timecode = timecode;
    frame.size = size;
    frame.pixels.resize(static_cast<std::size_t>(size.width) *
                        static_cast<std::size_t>(size.height));
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            frame.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
                         static_cast<std::size_t>(x)] =
                static_cast<uint8_t>((x * 7 + y * 3 + (x / (seed + 2)) + seed * 5) % shades);
        }
    }
    return frame;
}

std::string frameText(const TextFrame& frame, const std::string& eol, bool upperCase = true)
{
    static const char upper[] = "0123456789ABCDEF";
    static const char lower[] = "0123456789abcdef";
    const char* digits = upperCase ? upper : lower;
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text += upper[(frame.timecode >> shift) & 0xf];
    }
    text += eol;
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            text += digits[frame.pixels[static_cast<std::size_t>(y) *
                                            static_cast<std::size_t>(frame.size.width) +
                                        static_cast<std::size_t>(x)]];
        }
        text += eol;
    }
    return text;
}

std::string dumpText(const std::vector<TextFrame>& frames,
                     const std::string& eol = "\r\n",
                     bool blankLines = true)
{
    std::string text;
    for (const TextFrame& frame : frames) {
        text += frameText(frame, eol);
        if (blankLines) {
            text += eol;
        }
    }
    return text;
}

struct ReadResult {
    std::vector<DumpFrame> frames;
    FrameDumpReader::Status status = FrameDumpReader::Status::End;
    importing::DumpError error;
    std::optional<std::size_t> truncatedFrameLine;
    Size dumpFrameSize{0, 0};
    Size storedFrameSize{0, 0};
    uint64_t bytesRead = 0;
};

ReadResult readAll(const std::string& text)
{
    std::istringstream stream(text, std::ios::binary);
    FrameDumpReader reader(stream);
    ReadResult result;
    DumpFrame frame;
    for (;;) {
        result.status = reader.next(frame);
        if (result.status != FrameDumpReader::Status::Frame) {
            break;
        }
        result.frames.push_back(frame);
    }
    // Asking again gives the same answer.
    expect(reader.next(frame) == result.status, "the reader stays at its end");
    result.error = reader.error();
    result.truncatedFrameLine = reader.truncatedFrameLine();
    if (reader.hasFrameSize()) {
        result.dumpFrameSize = reader.dumpFrameSize();
        result.storedFrameSize = reader.storedFrameSize();
    }
    result.bytesRead = reader.bytesRead();
    return result;
}

void expectError(const std::string& text,
                 std::size_t line,
                 const std::string& messagePart,
                 const std::string& what)
{
    const ReadResult result = readAll(text);
    expect(result.status == FrameDumpReader::Status::Error, what + ": is an error");
    expectEqual(result.error.line, line, what + ": line of the error (" + result.error.text() +
                                             ")");
    expect(result.error.message.find(messagePart) != std::string::npos,
           what + ": message '" + result.error.message + "' names '" + messagePart + "'");
    expect(result.error.text().rfind("line " + std::to_string(line) + ": ", 0) == 0,
           what + ": the text starts with the line");
}

void testReaderSizes()
{
    for (const std::string eol : {"\n", "\r\n"}) {
        const std::vector<TextFrame> frames{textFrame(0x10, 1), textFrame(0x2A, 2),
                                            textFrame(0xFFFFFFFF, 3)};
        const std::string text = dumpText(frames, eol);
        const ReadResult result = readAll(text);
        expect(result.status == FrameDumpReader::Status::End, "128x32 reads to the end");
        expectEqual(result.frames.size(), std::size_t{3}, "128x32 frame count");
        expect(result.dumpFrameSize == Size{128, 32} && result.storedFrameSize == Size{128, 32},
               "128x32 sizes");
        expect(!result.truncatedFrameLine, "nothing truncated");
        expectEqual(result.bytesRead, static_cast<uint64_t>(text.size()), "all bytes read");
        for (std::size_t index = 0; index < frames.size(); ++index) {
            expectEqual(result.frames[index].timecode, frames[index].timecode, "timecode");
            expect(result.frames[index].pixels.pixels == frames[index].pixels, "pixels");
            expect(result.frames[index].pixels.size == Size{128, 32}, "frame size");
            expectEqual(static_cast<int>(result.frames[index].usedShades), 0xf, "used shades");
            expectEqual(result.frames[index].line, 1 + index * 34, "line of the timecode");
        }
    }

    for (const Size size : {Size{192, 64}, Size{256, 64}}) {
        const std::vector<TextFrame> frames{textFrame(1, 1, size, 16), textFrame(2, 2, size, 16)};
        const ReadResult result = readAll(dumpText(frames, "\n"));
        expectEqual(result.frames.size(), std::size_t{2}, "native size frame count");
        expect(result.dumpFrameSize == size && result.storedFrameSize == size, "native sizes");
        expect(result.frames[1].pixels.pixels == frames[1].pixels, "native size pixels");
        expectEqual(static_cast<int>(result.frames[0].usedShades), 0xffff, "16 shades used");
    }

    // 128x16 is stored in the middle of 128x32.
    TextFrame small = textFrame(5, 1, Size{128, 16}, 4);
    for (uint8_t& pixel : small.pixels) {
        pixel = static_cast<uint8_t>(1 + pixel % 3);
    }
    const ReadResult padded = readAll(dumpText({small, small}));
    expectEqual(padded.frames.size(), std::size_t{2}, "128x16 frame count");
    expect(padded.dumpFrameSize == Size{128, 16} && padded.storedFrameSize == Size{128, 32},
           "128x16 is stored as 128x32");
    const auto& pixels = padded.frames[0].pixels.pixels;
    expectEqual(pixels.size(), std::size_t{128 * 32}, "padded pixel count");
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::size_t row = index / 128;
        const uint8_t expected =
            row < 8 || row >= 24 ? uint8_t{0} : small.pixels[index - 8 * 128];
        expect(pixels[index] == expected, "padded pixel " + std::to_string(index));
    }
    // The padding is black: shade 0 counts although the dump has none.
    expectEqual(static_cast<int>(padded.frames[0].usedShades), 0xf, "padding adds shade 0");
}

void testReaderForms()
{
    const TextFrame frame = textFrame(0xAB, 1, Size{128, 32}, 16);
    const std::string rows = frameText(frame, "\n").substr(11);
    const std::string lowerRows = frameText(frame, "\n", false).substr(11);
    const auto readOne = [&](const std::string& text) {
        const ReadResult result = readAll(text);
        expect(result.status == FrameDumpReader::Status::End && result.frames.size() == 1,
               "one frame: " + result.error.text());
        expect(result.frames[0].pixels.pixels == frame.pixels, "pixels in another form");
        return result.frames[0].timecode;
    };
    expectEqual(readOne("0xab\n" + rows), 0xABu, "short lower case timecode");
    expectEqual(readOne("0XAB\n" + rows), 0xABu, "0X");
    expectEqual(readOne("xAB\n" + rows), 0xABu, "x without the 0");
    expectEqual(readOne("0x000000AB\n" + lowerRows), 0xABu, "lower case rows");
    expectEqual(readOne("\xEF\xBB\xBF" "0x000000AB\n" + rows), 0xABu, "byte order mark");
    expectEqual(readOne("\n\n  0x000000AB \t\n" + rows + "\n\n\n"), 0xABu,
                "blank lines and blanks around a line");
    // The last line needs no line end.
    expectEqual(readOne("0x000000AB\n" + rows.substr(0, rows.size() - 1)), 0xABu,
                "no line end at the end of the file");

    // Frames need no blank line between them.
    const std::vector<TextFrame> frames{textFrame(1, 1), textFrame(2, 2), textFrame(3, 3)};
    const ReadResult dense = readAll(dumpText(frames, "\r\n", false));
    expectEqual(dense.frames.size(), std::size_t{3}, "frames without blank lines");
    expect(dense.frames[1].pixels.pixels == frames[1].pixels &&
               dense.frames[2].timecode == 3,
           "frames without blank lines are read as they are");
    expectEqual(dense.frames[1].line, std::size_t{34}, "line of the second timecode");

    const ReadResult empty = readAll("");
    expect(empty.status == FrameDumpReader::Status::End && empty.frames.empty(),
           "an empty file has no frame");
    const ReadResult blank = readAll("\r\n\r\n");
    expect(blank.status == FrameDumpReader::Status::End && blank.frames.empty(),
           "blank lines alone are no frame");
}

void testReaderTruncation()
{
    const std::vector<TextFrame> frames{textFrame(1, 1), textFrame(2, 2), textFrame(3, 3)};
    const std::string whole = dumpText(frames, "\r\n");
    const std::size_t thirdStart = 2 * (12 + 32 * 130 + 2);
    const auto expectTruncated = [&](const std::string& text, const std::string& what) {
        const ReadResult result = readAll(text);
        expect(result.status == FrameDumpReader::Status::End,
               what + ": no error (" + result.error.text() + ")");
        expectEqual(result.frames.size(), std::size_t{2}, what + ": the complete frames");
        expect(result.truncatedFrameLine && *result.truncatedFrameLine == 69,
               what + ": the dropped frame is named by its line");
        expect(result.frames[1].pixels.pixels == frames[1].pixels, what + ": frames before it");
    };
    // In the middle of a row, as the Ghostbusters dump ends.
    expectTruncated(whole.substr(0, thirdStart + 12 + 18 * 130 + 64), "cut inside a row");
    expectTruncated(whole.substr(0, thirdStart + 12 + 18 * 130), "cut after a row");
    expectTruncated(whole.substr(0, thirdStart + 12 + 18 * 130) + "\r\n\r\n",
                    "cut after a row, blank lines behind");
    expectTruncated(whole.substr(0, thirdStart + 12), "cut after the timecode");
    expectTruncated(whole.substr(0, thirdStart + 10), "cut inside the timecode line");
    expectTruncated(whole.substr(0, thirdStart + 12 + 18 * 130 + 64) + "\r\n\r\n  \r\n",
                    "cut inside a row, blank lines behind");

    // A dump that ends inside its first frame holds none.
    const ReadResult first = readAll(whole.substr(0, 12 + 10 * 130 + 5));
    expect(first.status == FrameDumpReader::Status::End && first.frames.empty() &&
               first.truncatedFrameLine && *first.truncatedFrameLine == 1,
           "cut inside the first frame");
}

void testReaderErrors()
{
    const std::vector<TextFrame> frames{textFrame(1, 1), textFrame(2, 2), textFrame(3, 3)};
    const std::string eol = "\n";
    const std::string first = frameText(frames[0], eol) + eol;
    const std::string second = frameText(frames[1], eol) + eol;
    const std::string third = frameText(frames[2], eol) + eol;
    const std::size_t rowBytes = 129;
    const std::size_t timecodeBytes = 11;

    // A character that is no digit, in the second frame, row 5, column 7.
    std::string bad = first + second + third;
    bad[first.size() + timecodeBytes + 4 * rowBytes + 6] = 'G';
    expectError(bad, 35 + 5, "column 7: 'G' is not a hexadecimal digit", "a letter above F");
    bad[first.size() + timecodeBytes + 4 * rowBytes + 6] = '\0';
    expectError(bad, 35 + 5, "column 7: byte 0x00", "a control character");
    bad = first;
    bad[timecodeBytes + 2 * rowBytes] = ' ';
    // A blank at the start of a row is trimmed, which leaves the row too short.
    expectError(bad + second, 4, "the row has 127 digits", "a blank in the first frame");

    // A row of another length in the middle of the dump.
    std::string shortRow = first + second + third;
    shortRow.erase(first.size() + timecodeBytes + 9 * rowBytes, 3);
    expectError(shortRow, 35 + 10, "the row has 125 digits", "a short row");
    std::string longRow = first + second + third;
    longRow.insert(first.size() + timecodeBytes + 9 * rowBytes, "11");
    expectError(longRow, 35 + 10, "the row has 130 digits", "a long row");

    // A damaged last frame with no frame after it (a recording that broke off writing rows of
    // another width, as a SEGA_XL dump does): dropped as a truncated one.
    for (const std::string& insert : {std::string("11"), std::string("zz")}) {
        std::string damaged = first + second;
        damaged.insert(first.size() + timecodeBytes + 9 * rowBytes, insert);
        const ReadResult result = readAll(damaged);
        expect(result.status == FrameDumpReader::Status::End && result.frames.size() == 1 &&
                   result.truncatedFrameLine && *result.truncatedFrameLine == 35,
               "a damaged last frame is dropped, the frames before it are read");
    }

    // A frame with fewer or more rows than the first one: frames of mixed sizes.
    const std::string sixteen = frameText(textFrame(9, 4, Size{128, 16}), eol) + eol;
    expectError(first + sixteen + third, 35 + 17, "has 16 rows, the first frame of the dump has 32",
                "a 128x16 frame in a 128x32 dump");
    expectError(sixteen + first, 18 + 18, "a row follows the 16 rows",
                "a 128x32 frame in a 128x16 dump");
    const std::string wide = frameText(textFrame(9, 4, Size{256, 64}, 16), eol) + eol;
    expectError(first + wide + third, 36, "the row has 256 digits",
                "a 256x64 frame in a 128x32 dump");
    expectError(first + "0x00000009" + eol + "0x0000000A" + eol + second, 36,
                "has 0 rows", "two timecodes in a row");

    // Lines that are neither a timecode nor a row.
    expectError(first + "frame 2" + eol + second, 35, "expected the timecode line",
                "text between frames");
    expectError("0x123456789" + eol + second, 1, "expected the timecode line",
                "a timecode with nine digits");
    expectError("0x12G4" + eol + second, 1, "expected the timecode line",
                "a timecode with another character");
    expectError("12345678" + eol + second, 1, "expected the timecode line",
                "a timecode without x");
    expectError(std::string("PK\x03\x04") + std::string(5000, 'A'), 1, "longer than",
                "a file without line ends");

    // Sizes no project has.
    const std::string narrow = frameText(textFrame(1, 1, Size{100, 32}), eol) + eol;
    expectError(narrow + narrow, 2, "the first row has 100 characters", "frames 100 pixels wide");
    const std::string twenty = frameText(textFrame(1, 1, Size{128, 20}), eol) + eol;
    expectError(twenty + twenty, 1, "the first frame is 128x20", "frames with 20 rows");
    const std::string tall = frameText(textFrame(1, 1, Size{128, 70}), eol) + eol;
    expectError(tall, 2 + 64, "more than 64 rows", "a frame with 70 rows");
    expectError("0x00000001" + eol + eol + first, 2, "has no rows",
                "a timecode with a blank line behind it");
}

DumpImportResult importText(const std::string& text,
                            const DumpImportOptions& options = {},
                            const ProjectDocument* project = nullptr)
{
    std::istringstream stream(text, std::ios::binary);
    return importFrameDump(stream, options, project);
}

void expectCountsAddUp(const DumpImportResult& result, const std::string& what)
{
    const importing::DumpImportCounts& counts = result.counts;
    expectEqual(counts.transitional + counts.tooShort + counts.tooManyColors + counts.duplicates +
                    counts.identicalInProject + counts.identifiedByMaskOrShape +
                    counts.framesKept,
                counts.framesRead, what + ": every frame is counted once");
    expectEqual(result.frames.size(), counts.framesKept, what + ": kept frames");
}

// A frame of two pictures: left half one shade, right half another.
TextFrame twoShades(uint32_t timecode, uint8_t left, uint8_t right)
{
    TextFrame frame;
    frame.timecode = timecode;
    frame.pixels.resize(128 * 32);
    for (std::size_t index = 0; index < frame.pixels.size(); ++index) {
        frame.pixels[index] = index % 128 < 64 ? left : right;
    }
    return frame;
}

void testTransitionalFrames()
{
    using importing::isTransitionalFrame;
    const auto pixels = [](const TextFrame& frame) {
        return document::IndexedFrame{Size{128, 32},
                                      document::CompactArray<uint8_t>(frame.pixels)};
    };
    // Old picture: left lit (3), right dark (0); new: the other way round. The WPC ghost has
    // the same in-between shade wherever the same change happens: 3 -> 0 shows 1, 0 -> 3 2.
    const TextFrame oldPicture = twoShades(0, 3, 0);
    const TextFrame ghost = twoShades(16, 1, 2);
    const TextFrame newPicture = twoShades(32, 0, 3);
    expect(isTransitionalFrame(pixels(oldPicture), pixels(ghost), pixels(newPicture)),
           "a mix of both pictures is a transitional frame");
    // Not when one changing pixel has another shade than the rest of its kind.
    TextFrame uneven = ghost;
    uneven.pixels[5] = 2;
    expect(!isTransitionalFrame(pixels(oldPicture), pixels(uneven), pixels(newPicture)),
           "every pixel with the same change has the same shade");
    // Nor when a pixel that stays the same in both neighbours changes in between.
    TextFrame steady = twoShades(0, 3, 1);
    TextFrame flicker = twoShades(16, 1, 1);
    TextFrame steadyNew = twoShades(32, 0, 1);
    expect(isTransitionalFrame(pixels(steady), pixels(flicker), pixels(steadyNew)),
           "only the changing half: a transitional frame");
    flicker.pixels[100] = 2;
    expect(!isTransitionalFrame(pixels(steady), pixels(flicker), pixels(steadyNew)),
           "a pixel that both neighbours have alike keeps it");
    // Nor a frame that only shows the old or the new shade: no shade in between.
    expect(!isTransitionalFrame(pixels(oldPicture), pixels(twoShades(16, 3, 3)),
                                pixels(newPicture)),
           "a frame without an in-between shade is not a mix");

    // In a dump: the ghosts of 16 ms go, a frame that shows longer stays (the limit).
    std::vector<TextFrame> dump{oldPicture, ghost, newPicture,
                                twoShades(48, 2, 1),  // ghost back, but shown 40 ms
                                twoShades(88, 3, 0)};
    DumpImportOptions options;
    options.dropTransitionalFrames = true;
    DumpImportResult result = importText(dumpText(dump), options);
    expectCountsAddUp(result, "transitional frames");
    expectEqual(result.counts.transitional, std::size_t{1}, "the 16 ms ghost is left out");
    expect(result.frames[1].original.pixels == pixels(newPicture).pixels,
           "and the frame after it follows the one before");
    options.transitionalLimitMs = 50;
    result = importText(dumpText(dump), options);
    expectEqual(result.counts.transitional, std::size_t{2},
                "a longer limit takes the 40 ms ghost too");
    options.dropTransitionalFrames = false;
    result = importText(dumpText(dump), options);
    expectEqual(result.counts.transitional, std::size_t{0}, "off, nothing is left out");
    expect(importing::clampedOptions(DumpImportOptions{.transitionalLimitMs = 1000})
                   .transitionalLimitMs == importing::kMaxTransitionalLimitMs,
           "the limit is clamped");
}

void testDurations()
{
    // 20 ms, 4000 ms, more than 4000 ms, a step back, the last frame.
    const std::vector<TextFrame> frames{textFrame(1000, 1), textFrame(1020, 2),
                                        textFrame(5020, 3), textFrame(9021, 4),
                                        textFrame(9000, 5)};
    const DumpImportResult result = importText(dumpText(frames));
    expect(result.ok, "a dump imports: " + result.error.text());
    expectCountsAddUp(result, "durations");
    expectEqual(result.frames.size(), std::size_t{5}, "all frames kept");
    const std::vector<uint32_t> expected{20, 4000, 30, 30, 30};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expectEqual(result.frames[index].durationMs, expected[index],
                    "duration of frame " + std::to_string(index));
        expect(result.frames[index].original.pixels == frames[index].pixels, "kept in order");
    }
    expectEqual(result.colorCount, 4u, "shades up to 3 are 4 colors");
    expect(result.dumpFrameSize == Size{128, 32}, "size of the dump");

    // A dump that ends inside a frame: the last complete frame has the default duration,
    // as in the Win32 editor, which does not look at the timecode of the cut frame.
    const std::string cut = dumpText({textFrame(100, 1), textFrame(150, 2)}) +
                            frameText(textFrame(190, 3), "\r\n").substr(0, 500);
    const DumpImportResult truncated = importText(cut);
    expect(truncated.ok && truncated.frames.size() == 2, "the complete frames are imported");
    expect(truncated.truncatedFrameLine && *truncated.truncatedFrameLine == 69,
           "the cut frame is reported");
    expectEqual(truncated.frames[0].durationMs, 50u, "measured duration");
    expectEqual(truncated.frames[1].durationMs, 30u, "last complete frame");
}

void testFilters()
{
    // Durations: 4, 5, 14, 15, 5000 (default 30), last (default 30).
    const std::vector<TextFrame> frames{textFrame(0, 1), textFrame(4, 2), textFrame(9, 3),
                                        textFrame(23, 4), textFrame(38, 5), textFrame(5038, 6)};
    DumpImportOptions options;
    options.dropShortFrames = true;
    options.shortFrameLimitMs = 15;
    DumpImportResult result = importText(dumpText(frames), options);
    expectCountsAddUp(result, "short frames");
    expectEqual(result.counts.tooShort, std::size_t{3}, "frames shorter than 15 ms");
    expectEqual(result.frames.size(), std::size_t{3}, "frames of 15 ms and more");
    expect(result.frames[0].original.pixels == frames[3].pixels &&
               result.frames[0].durationMs == 15,
           "a frame of exactly the limit stays");

    // The limit is brought into 5..3000.
    options.shortFrameLimitMs = 1;
    result = importText(dumpText(frames), options);
    expectEqual(result.counts.tooShort, std::size_t{1}, "limit 1 becomes 5");
    options.shortFrameLimitMs = 100000;
    result = importText(dumpText(frames), options);
    // The two frames with the default duration of 30 ms stay although 30 < 3000.
    expectEqual(result.counts.tooShort, std::size_t{4}, "limit above 3000 becomes 3000");
    expectEqual(result.frames.size(), std::size_t{2}, "default durations are not filtered");
    expect(importing::clampedOptions(options).shortFrameLimitMs == 3000 &&
               importing::clampedOptions(DumpImportOptions{false, 0, false, 99, false})
                       .colorLimit == 16 &&
               importing::clampedOptions(DumpImportOptions{false, 0, false, 0, false})
                       .colorLimit == 1,
           "limits are clamped");

    // Colors: frames with 2, 3, 4 and 9 shades.
    std::vector<TextFrame> colorful{textFrame(0, 1, Size{128, 32}, 2),
                                    textFrame(100, 2, Size{128, 32}, 3),
                                    textFrame(200, 3, Size{128, 32}, 4),
                                    textFrame(300, 4, Size{128, 32}, 9)};
    DumpImportOptions colorOptions;
    colorOptions.dropColorfulFrames = true;
    colorOptions.colorLimit = 3;
    result = importText(dumpText(colorful), colorOptions);
    expectCountsAddUp(result, "colors");
    expectEqual(result.counts.tooManyColors, std::size_t{2}, "frames with more than 3 shades");
    expectEqual(result.frames.size(), std::size_t{2}, "frames with up to 3 shades");
    // The number of colors of the project comes from all frames, dropped or not.
    expectEqual(result.colorCount, 16u, "a dropped frame with 9 shades makes it 16 colors");
    colorOptions.dropColorfulFrames = false;
    result = importText(dumpText(colorful), colorOptions);
    expectEqual(result.frames.size(), std::size_t{4}, "the limit alone filters nothing");

    // A short frame is counted as short, whatever its colors are.
    std::vector<TextFrame> both{textFrame(0, 1, Size{128, 32}, 9), textFrame(3, 2)};
    DumpImportOptions bothOptions;
    bothOptions.dropShortFrames = true;
    bothOptions.shortFrameLimitMs = 5;
    bothOptions.dropColorfulFrames = true;
    bothOptions.colorLimit = 4;
    result = importText(dumpText(both), bothOptions);
    expect(result.counts.tooShort == 1 && result.counts.tooManyColors == 0 &&
               result.frames.size() == 1,
           "the duration is checked before the colors");
}

void testDuplicatesInDump()
{
    // A B A C B A: the first of each stays, with its own duration.
    const TextFrame a = textFrame(0, 1);
    const TextFrame b = textFrame(0, 2);
    const TextFrame c = textFrame(0, 3);
    std::vector<TextFrame> frames{a, b, a, c, b, a};
    const std::vector<uint32_t> timecodes{100, 110, 130, 160, 200, 250};
    for (std::size_t index = 0; index < frames.size(); ++index) {
        frames[index].timecode = timecodes[index];
    }
    DumpImportResult result = importText(dumpText(frames));
    expectCountsAddUp(result, "duplicates");
    expectEqual(result.counts.duplicates, std::size_t{3}, "repeated frames");
    expectEqual(result.frames.size(), std::size_t{3}, "different frames");
    expect(result.frames[0].original.pixels == a.pixels &&
               result.frames[1].original.pixels == b.pixels &&
               result.frames[2].original.pixels == c.pixels,
           "the first occurrences, in their order");
    // The time of the dropped repetitions is not added to the frame that stays.
    expect(result.frames[0].durationMs == 10 && result.frames[1].durationMs == 20 &&
               result.frames[2].durationMs == 40,
           "durations of the first occurrences");
    expectEqual(result.counts.hashCollisions, std::size_t{0}, "no collision");

    // A frame that was filtered out is not what a later frame is a duplicate of.
    DumpImportOptions options;
    options.dropShortFrames = true;
    options.shortFrameLimitMs = 15;
    result = importText(dumpText(frames), options);
    expectCountsAddUp(result, "duplicates with a filter");
    // a (10 ms) is too short; its repetition after b (30 ms) is the first a that passes.
    expect(result.counts.tooShort == 1 && result.counts.duplicates == 2 &&
               result.frames.size() == 3,
           "counts with a short first occurrence");
    expect(result.frames[0].original.pixels == b.pixels &&
               result.frames[1].original.pixels == a.pixels && result.frames[1].durationMs == 30,
           "the first occurrence that passes the filter stays");
}

// A project of the frames of a dump, as the editor makes it.
ProjectDocument projectOf(const std::vector<TextFrame>& frames, const std::string& name = "rom")
{
    DumpImportResult result = importText(dumpText(frames));
    expect(result.ok, "the fixture dump imports: " + result.error.text());
    return ProjectDocument::fromSnapshot(makeProjectSnapshot(name, std::move(result)));
}

IndexedFrame indexed(const TextFrame& frame)
{
    IndexedFrame result;
    result.size = frame.size;
    result.pixels = frame.pixels;
    return result;
}

void testAgainstProject()
{
    // The project: frame 0 plain, frame 1 with mask 0 over the left 16 columns, frame 2 in
    // shape mode, frame 3 with mask 1 over the top 8 rows and in shape mode.
    const std::vector<TextFrame> existing{textFrame(0, 1), textFrame(100, 2), textFrame(200, 3),
                                          textFrame(300, 4)};
    ProjectDocument project = projectOf(existing);
    expect(project.appendComparisonMask() == 0 && project.appendComparisonMask() == 1,
           "two masks");
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 16; ++x) {
            project.setComparisonMaskPixel(0, x, y, 1);
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 128; ++x) {
            project.setComparisonMaskPixel(1, x, y, 1);
        }
    }
    project.setComparisonMaskId(1, 0);
    project.setShapeComparisonMode(2, 1);
    project.setComparisonMaskId(3, 1);
    project.setShapeComparisonMode(3, 1);

    // Dump frames: each project frame as it is, then variants.
    TextFrame underMask = existing[1];   // differs from frame 1 under mask 0 only
    underMask.pixels[5] = static_cast<uint8_t>((underMask.pixels[5] + 1) % 4);
    TextFrame outsideMask = existing[1];  // differs from frame 1 outside mask 0
    outsideMask.pixels[100] = static_cast<uint8_t>((outsideMask.pixels[100] + 1) % 4);
    TextFrame sameShape = existing[2];    // other shades, same shape as frame 2
    for (uint8_t& pixel : sameShape.pixels) {
        pixel = pixel == 0 ? uint8_t{0} : static_cast<uint8_t>(1 + pixel % 3);
    }
    TextFrame otherShape = existing[2];
    otherShape.pixels[200] = otherShape.pixels[200] == 0 ? uint8_t{2} : uint8_t{0};
    TextFrame maskAndShape = existing[3];  // other shades, differs under mask 1
    for (uint8_t& pixel : maskAndShape.pixels) {
        pixel = pixel == 0 ? uint8_t{0} : static_cast<uint8_t>(1 + pixel % 3);
    }
    maskAndShape.pixels[3] = maskAndShape.pixels[3] == 0 ? uint8_t{1} : uint8_t{0};
    TextFrame plainVariant = existing[0];  // frame 0 has no mask: any difference counts
    plainVariant.pixels[5] = static_cast<uint8_t>((plainVariant.pixels[5] + 1) % 4);
    const TextFrame fresh = textFrame(0, 9);

    std::vector<TextFrame> dump{existing[0], existing[1], existing[2],   existing[3],
                                underMask,   outsideMask, sameShape,     otherShape,
                                maskAndShape, plainVariant, fresh,       underMask};
    for (std::size_t index = 0; index < dump.size(); ++index) {
        dump[index].timecode = static_cast<uint32_t>(1000 + index * 40);
    }
    const std::string text = dumpText(dump);

    // Without the masks: only identical originals are dropped.
    DumpImportOptions options;
    DumpImportResult result = importText(text, options, &project);
    expect(result.ok, "the dump is compared with the project: " + result.error.text());
    expectCountsAddUp(result, "compared without masks");
    expectEqual(result.counts.identicalInProject, std::size_t{4}, "identical originals");
    expectEqual(result.counts.identifiedByMaskOrShape, std::size_t{0}, "masks are not used");
    expectEqual(result.counts.duplicates, std::size_t{1}, "the repeated dump frame");
    expectEqual(result.frames.size(), std::size_t{7}, "everything else is new");

    // With the masks and shape modes: what the runtime identifies is dropped.
    options.useMasksAndShapeModes = true;
    result = importText(text, options, &project);
    expectCountsAddUp(result, "compared with masks");
    // Frame 0 alone is compared plainly; frames 1 to 3 match through their own rule.
    expectEqual(result.counts.identicalInProject, std::size_t{1}, "identical to a plain frame");
    expectEqual(result.counts.identifiedByMaskOrShape, std::size_t{6},
                "identified through a mask or shape mode");
    expectEqual(result.counts.duplicates, std::size_t{1}, "the repeated dump frame");
    expectEqual(result.frames.size(), std::size_t{4}, "frames the project does not identify");
    expect(result.frames[0].original.pixels == outsideMask.pixels &&
               result.frames[1].original.pixels == otherShape.pixels &&
               result.frames[2].original.pixels == plainVariant.pixels &&
               result.frames[3].original.pixels == fresh.pixels,
           "the new frames, in the order of the dump");
    expectEqual(result.frames[0].durationMs, 40u, "a new frame keeps its duration");

    // "Identified by the project" is the runtime's rule, frame by frame.
    const FrameIdentification rule(project);
    const ProjectFrameMatcher withMasks(project, true);
    const ProjectFrameMatcher withoutMasks(project, false);
    for (const TextFrame& frame : dump) {
        const IndexedFrame original = indexed(frame);
        const uint32_t hash = legacy::legacyFrameHash(original, nullptr, false);
        const bool identified = !rule.matches(original).empty();
        expect((withMasks.match(original, hash) != ProjectFrameMatcher::Match::None) == identified,
               "the matcher with masks agrees with the runtime's search rule");
        bool identical = false;
        for (std::size_t index = 0; index < project.frameCount(); ++index) {
            identical = identical || project.originalFrame(index).pixels == frame.pixels;
        }
        expect((withoutMasks.match(original, hash) == ProjectFrameMatcher::Match::Identical) ==
                   identical,
               "the matcher without masks finds identical originals only");
    }

    // A project frame with a mask ID the project has no mask for is compared plainly.
    ProjectDocument odd = projectOf(existing);
    odd.restoreComparisonMaskId(1, 7);
    const ProjectFrameMatcher oddMatcher(odd, true);
    const IndexedFrame oddFrame = indexed(underMask);
    expect(oddMatcher.match(oddFrame, legacy::legacyFrameHash(oddFrame, nullptr, false)) ==
               ProjectFrameMatcher::Match::None &&
               FrameIdentification(odd).matches(oddFrame).empty(),
           "a mask ID without a mask is no mask");

    // A dump of one frame is compared too (the Win32 editor skips that case).
    result = importText(dumpText({existing[2]}), options, &project);
    expect(result.ok && result.frames.empty() && result.counts.alreadyInProject() == 1,
           "a dump of one frame the project has");

    // Another size and too many shades are refused; fewer shades are taken.
    DumpImportResult refused =
        importText(dumpText({textFrame(0, 1, Size{192, 64})}), options, &project);
    expect(!refused.ok && !refused.cancelled &&
               refused.error.message.find("192x64") != std::string::npos &&
               refused.error.message.find("128x32") != std::string::npos,
           "another size is refused: " + refused.error.message);
    refused = importText(dumpText({textFrame(0, 1, Size{128, 16})}), options, &project);
    expect(refused.ok, "a 128x16 dump fits a 128x32 project");
    refused = importText(dumpText({fresh, textFrame(50, 8, Size{128, 32}, 16)}), options, &project);
    expect(!refused.ok && refused.frames.empty() &&
               refused.error.message.find("16 shades") != std::string::npos,
           "16 shades into a project with 4 colors: " + refused.error.message);
    ProjectDocument sixteen = projectOf({textFrame(0, 1, Size{128, 32}, 16)});
    expectEqual(sixteen.colorCount(), 16u, "a project with 16 colors");
    result = importText(dumpText({fresh}), options, &sixteen);
    expect(result.ok && result.colorCount == 4 && result.frames.size() == 1,
           "4 shades into a project with 16 colors");
}

void testCancelAndFiles()
{
    std::vector<TextFrame> frames;
    for (int index = 0; index < 200; ++index) {
        frames.push_back(textFrame(static_cast<uint32_t>(index * 20), index));
    }
    const std::string text = dumpText(frames);
    std::istringstream stream(text, std::ios::binary);
    int calls = 0;
    uint64_t lastBytes = 0;
    DumpImportResult result = importFrameDump(stream, {}, nullptr, [&](uint64_t bytesRead) {
        expect(bytesRead >= lastBytes && bytesRead <= text.size(), "progress goes forward");
        lastBytes = bytesRead;
        return ++calls < 3;
    });
    expect(!result.ok && result.cancelled && result.frames.empty(), "cancelled by the progress");
    expectEqual(calls, 3, "stops at once");

    std::istringstream again(text, std::ios::binary);
    result = importFrameDump(again, {}, nullptr, [&](uint64_t bytesRead) {
        lastBytes = bytesRead;
        return true;
    });
    expect(result.ok && result.counts.framesRead == 200, "runs to the end");
    expectCountsAddUp(result, "a longer dump");
    expectEqual(lastBytes, static_cast<uint64_t>(text.size()), "the last progress is the end");

    result = importing::importFrameDumpFile("/nonexistent/dump.txt", {}, nullptr);
    expect(!result.ok && result.error.message.find("cannot be opened") != std::string::npos,
           "a file that is not there");
    result = importText("");
    expect(!result.ok && result.error.message.find("no frame") != std::string::npos,
           "an empty dump");
    result = importText(frameText(textFrame(0, 1), "\n").substr(0, 900));
    expect(!result.ok && result.error.line == 1 &&
               result.error.message.find("no complete frame") != std::string::npos,
           "a dump that ends inside its first frame");
    result = importText("0x00000001\nnot a row\n");
    expect(!result.ok && result.error.line == 2, "a reader error is passed on with its line");
}

void testNames()
{
    expectEqual(importing::romNameFromDumpPath("/dumps/afm_113b.txt"), std::string("afm_113b"),
                "name of a path");
    expectEqual(importing::romNameFromDumpPath("C:\\dumps\\smb3.dump.txt"), std::string("smb3"),
                "up to the first dot, after a backslash");
    expectEqual(importing::romNameFromDumpPath("serum-3mmy13yy.txt"), std::string("serum-3mmy13yy"),
                "without a directory");
    expectEqual(importing::romNameFromDumpPath("/dumps/" + std::string(80, 'a') + ".txt").size(),
                std::size_t{63}, "cut to 63 characters");
    expectEqual(importing::romNameFromDumpPath("/dumps/.txt"), std::string(), "no name");

    expect(importing::romNameProblem("afm_113b").empty(), "a ROM name");
    expect(importing::romNameProblem(std::string(63, 'a')).empty(), "63 characters");
    expect(!importing::romNameProblem(std::string(64, 'a')).empty(), "64 characters");
    expect(!importing::romNameProblem("").empty(), "no name");
    for (const std::string name : {"a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b",
                                   "a|b", "a\tb", "caf\xC3\xA9", " a", "a ", "a."}) {
        expect(!importing::romNameProblem(name).empty(), "refused name '" + name + "'");
    }

    expect(importing::geometryForDump(Size{128, 32})->layout() == PlaneLayout::Ordinary &&
               importing::geometryForDump(Size{192, 64})->layout() == PlaneLayout::NativeOriginal &&
               importing::geometryForDump(Size{256, 64})->layout() == PlaneLayout::NativeExtra &&
               !importing::geometryForDump(Size{128, 16}),
           "layouts of the dump sizes");
}

// The bytes of each block of a serialized file.
std::map<std::string, std::string> blocks(const std::string& bytes,
                                          const legacy::LegacySectionOffsets& sections)
{
    std::map<std::string, std::string> result;
    for (std::size_t index = 0; index + 1 < sections.size(); ++index) {
        const std::size_t begin = sections[index].second;
        result[sections[index].first] = bytes.substr(begin, sections[index + 1].second - begin);
    }
    return result;
}

std::string serializedCrom(const ProjectSnapshot& snapshot,
                           legacy::LegacySectionOffsets* sections = nullptr)
{
    std::ostringstream stream(std::ios::binary);
    std::string error;
    expect(legacy::writeLegacyCrom(stream, snapshot, error, sections),
           "the cROM is written: " + error);
    return stream.str();
}

std::string serializedCrp(const ProjectSnapshot& snapshot,
                          legacy::LegacySectionOffsets* sections = nullptr)
{
    std::ostringstream stream(std::ios::binary);
    std::string error;
    expect(legacy::writeLegacyCrp(stream, snapshot, error, sections),
           "the cRP is written: " + error);
    return stream.str();
}

uint32_t u32At(const std::string& bytes, std::size_t offset)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset])) |
           static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2])) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3])) << 24;
}

uint16_t u16At(const std::string& bytes, std::size_t offset)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(bytes[offset]) |
                                 static_cast<uint8_t>(bytes[offset + 1]) << 8);
}

void expectFilled(const std::map<std::string, std::string>& fileBlocks,
                  const std::string& name,
                  std::size_t size,
                  uint8_t value,
                  const std::string& what)
{
    const auto found = fileBlocks.find(name);
    expect(found != fileBlocks.end(), what + ": block '" + name + "' exists");
    expectEqual(found->second.size(), size, what + ": size of '" + name + "'");
    for (char byte : found->second) {
        if (static_cast<uint8_t>(byte) != value) {
            expect(false, what + ": '" + name + "' is " + std::to_string(value) + " throughout");
        }
    }
}

// A new project of every layout: what a save writes, block by block, and libserum.
void testNewProject(Size dumpSize, uint32_t shades)
{
    const std::string what =
        std::to_string(dumpSize.width) + "x" + std::to_string(dumpSize.height) + ", " +
        std::to_string(shades) + " shades";
    const std::size_t frameCount = 5;
    std::vector<TextFrame> frames;
    for (std::size_t index = 0; index < frameCount; ++index) {
        frames.push_back(textFrame(static_cast<uint32_t>(1000 + index * 50),
                                   static_cast<int>(index) + 1,
                                   dumpSize,
                                   static_cast<int>(shades)));
    }
    // A repetition, and a short last-but-one frame: 10 ms, which a save flags inactive.
    frames.push_back(frames[1]);
    frames.back().timecode = 1250;
    frames.push_back(textFrame(1300, 20, dumpSize, static_cast<int>(shades)));
    frames.push_back(textFrame(1310, 21, dumpSize, static_cast<int>(shades)));

    DumpImportResult dump = importText(dumpText(frames));
    expect(dump.ok, what + ": imports: " + dump.error.text());
    expectEqual(dump.counts.framesKept, std::size_t{7}, what + ": kept frames");
    const std::vector<importing::ImportedFrame> kept = dump.frames;
    const uint32_t colorCount = shades > 4 ? 16u : 4u;
    expectEqual(dump.colorCount, colorCount, what + ": colors");

    const ProjectSnapshot snapshot = makeProjectSnapshot("newrom", std::move(dump));
    expect(dump.frames.empty(), what + ": the frames are taken out of the result");
    std::string validation;
    expect(document::isValidProjectSnapshot(snapshot, &validation),
           what + ": valid: " + validation);
    const ProjectGeometry geometry = snapshot.geometry;
    const Size romSize = geometry.romFrameSize();
    const Size sdSize = geometry.planeSize(Resolution::Sd);
    const Size hdSize = geometry.planeSize(Resolution::Hd);
    const auto pixelsOf = [](Size size) {
        return static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
    };
    const PlaneLayout expectedLayout = dumpSize.width == 192   ? PlaneLayout::NativeOriginal
                                       : dumpSize.width == 256 ? PlaneLayout::NativeExtra
                                                               : PlaneLayout::Ordinary;
    expect(geometry.layout() == expectedLayout, what + ": layout");

    // The document saves what the snapshot holds, and a reload of it is the same document.
    ProjectDocument project = ProjectDocument::fromSnapshot(snapshot);
    expect(!project.isDirty(), what + ": a new document is clean");
    legacy::LegacySectionOffsets cromSections;
    legacy::LegacySectionOffsets crpSections;
    const ProjectSnapshot saved = project.toSnapshot();
    const std::string crom = serializedCrom(saved, &cromSections);
    const std::string crp = serializedCrp(saved, &crpSections);
    expect(crom == serializedCrom(snapshot) && crp == serializedCrp(snapshot),
           what + ": the document writes what the snapshot writes");
    const std::map<std::string, std::string> cromBlocks = blocks(crom, cromSections);
    const std::map<std::string, std::string> crpBlocks = blocks(crp, crpSections);
    const std::size_t n = kept.size();
    const std::vector<uint16_t> colors = legacy::legacyOriginalColors(colorCount);

    // cROM header.
    const std::string& header = cromBlocks.at("header");
    expectEqual(header.size(), std::size_t{114}, what + ": header size");
    expectEqual(std::string(header.c_str()), std::string("newrom"), what + ": ROM name");
    expectEqual(u32At(header, 64), 84u, what + ": header length of revision 21");
    expect(u32At(header, 68) == static_cast<uint32_t>(sdSize.width) &&
               u32At(header, 72) == static_cast<uint32_t>(sdSize.height) &&
               u32At(header, 76) == static_cast<uint32_t>(hdSize.width) &&
               u32At(header, 80) == static_cast<uint32_t>(hdSize.height),
           what + ": plane sizes");
    expectEqual(u32At(header, 84), static_cast<uint32_t>(n), what + ": frame count");
    expectEqual(u32At(header, 88), colorCount, what + ": color count");
    expect(u32At(header, 92) == 0 && u32At(header, 96) == 0 && u16At(header, 100) == 0,
           what + ": no masks, sprites, backgrounds");
    expectEqual(u32At(header, 102), expectedLayout == PlaneLayout::NativeExtra ? 1u : 0u,
                what + ": 256x64 flag");

    // Per-frame defaults (CopyTXTFrames2Frame).
    const std::size_t sd = pixelsOf(sdSize);
    const std::size_t hd = pixelsOf(hdSize);
    expectFilled(cromBlocks, "shape comparison modes", n, 0, what);
    expectFilled(cromBlocks, "comparison mask IDs", n, 255, what);
    expectFilled(cromBlocks, "comparison masks", 0, 0, what);
    expectFilled(cromBlocks, "extra frame flags", n,
                 expectedLayout == PlaneLayout::NativeExtra ? 1 : 0, what);
    expectFilled(cromBlocks, "SD dynamic masks", n * sd, 255, what);
    expectFilled(cromBlocks, "HD dynamic masks", n * hd, 255, what);
    expectFilled(cromBlocks, "SD dynamic colors", n * 32 * colorCount * 2, 0, what);
    expectFilled(cromBlocks, "HD dynamic colors", n * 32 * colorCount * 2, 0, what);
    // 16-bit sprite IDs, 0xffff for an empty slot (format version 1).
    expectFilled(cromBlocks, "frame sprites", n * document::kSpritesPerFrame * 2, 255, what);
    expectFilled(cromBlocks, "SD color rotations", n * 4 * 64 * 2, 0, what);
    expectFilled(cromBlocks, "HD color rotations", n * 4 * 64 * 2, 0, what);
    expectFilled(cromBlocks, "trigger IDs", n * 4, 255, what);
    expectFilled(cromBlocks, "background IDs", n * 2, 255, what);
    expectFilled(cromBlocks, "SD background masks", n * sd, 1, what);
    expectFilled(cromBlocks, "HD background masks", n * hd, 1, what);
    expectFilled(cromBlocks, "SD dynamic shadow directions", n * 32, 0, what);
    expectFilled(cromBlocks, "SD dynamic shadow colors", n * 32 * 2, 0, what);
    expectFilled(cromBlocks, "HD dynamic shadow directions", n * 32, 0, what);
    expectFilled(cromBlocks, "HD dynamic shadow colors", n * 32 * 2, 0, what);
    for (const char* empty : {"extra sprite flags", "sprite originals", "SD sprite colors",
                              "HD sprite masks", "HD sprite colors", "sprite detection dwords",
                              "sprite detection dword positions", "sprite detection areas",
                              "extra background flags", "SD backgrounds", "HD backgrounds",
                              "SD sprite dynamic colors", "HD sprite dynamic colors",
                              "SD sprite dynamic masks", "HD sprite dynamic masks",
                              "sprite shape modes"}) {
        expectFilled(cromBlocks, empty, 0, 0, what);
    }
    const std::string& boxes = cromBlocks.at("frame sprite bounding boxes");
    expectEqual(boxes.size(), n * document::kSpritesPerFrame * 8, what + ": sprite boxes size");
    for (std::size_t box = 0; box < n * document::kSpritesPerFrame; ++box) {
        expect(u16At(boxes, box * 8) == 0 && u16At(boxes, box * 8 + 2) == 0 &&
                   u16At(boxes, box * 8 + 4) == romSize.width - 1 &&
                   u16At(boxes, box * 8 + 6) == romSize.height - 1,
               what + ": a sprite slot's bounds are the whole frame");
    }

    // Hashes, pictures and the active flag.
    const std::string& hashes = cromBlocks.at("hash codes");
    const std::string& active = cromBlocks.at("active frame flags");
    const std::string& sdFrames = cromBlocks.at("SD colorized frames");
    const std::string& hdFrames = cromBlocks.at("HD colorized frames");
    expect(hashes.size() == n * 4 && active.size() == n && sdFrames.size() == n * sd * 2 &&
               hdFrames.size() == n * hd * 2,
           what + ": per-frame block sizes");
    for (std::size_t frame = 0; frame < n; ++frame) {
        expectEqual(u32At(hashes, frame * 4),
                    legacy::legacyFrameHash(kept[frame].original, nullptr, false),
                    what + ": hash of frame " + std::to_string(frame));
        // Frame 5 lasted 10 ms and is not colorized.
        expectEqual(static_cast<int>(active[frame]), kept[frame].durationMs >= 15 ? 1 : 0,
                    what + ": active flag of frame " + std::to_string(frame));
        const auto& original = kept[frame].original.pixels;
        const bool extraNative = expectedLayout == PlaneLayout::NativeExtra;
        const std::string& native = extraNative ? hdFrames : sdFrames;
        const std::size_t nativePixels = extraNative ? hd : sd;
        for (std::size_t index = 0; index < nativePixels; ++index) {
            expect(u16At(native, (frame * nativePixels + index) * 2) == colors[original[index]],
                   what + ": the picture is the original in the plain colors");
        }
        if (extraNative) {
            for (std::size_t index = 0; index < sd; ++index) {
                const std::size_t x = index % 128;
                const std::size_t y = index / 128;
                expect(u16At(sdFrames, (frame * sd + index) * 2) ==
                           colors[original[2 * y * 256 + 2 * x]],
                       what + ": the unused 128x32 picture is every second pixel");
            }
        } else {
            for (std::size_t index = 0; index < hd; ++index) {
                expect(u16At(hdFrames, (frame * hd + index) * 2) == 0,
                       what + ": no extra-plane picture");
            }
        }
    }
    expectEqual(kept[5].durationMs, 10u, what + ": the short frame");

    // cRP: originals, durations, the plain colors in palette 0 and the edit colors, and
    // nothing else.
    expectEqual(std::string(crpBlocks.at("ROM name").c_str()), std::string("newrom"),
                what + ": cRP ROM name");
    const std::string& originals = crpBlocks.at("original frames");
    const std::string& durations = crpBlocks.at("frame durations");
    expectEqual(originals.size(), n * pixelsOf(romSize), what + ": originals size");
    for (std::size_t frame = 0; frame < n; ++frame) {
        expect(originals.compare(frame * pixelsOf(romSize), pixelsOf(romSize),
                                 std::string(kept[frame].original.pixels.begin(),
                                             kept[frame].original.pixels.end())) == 0,
               what + ": original of frame " + std::to_string(frame));
        expectEqual(u32At(durations, frame * 4), kept[frame].durationMs, what + ": duration");
    }
    const std::string& palettes = crpBlocks.at("palettes");
    const std::string& editColors = crpBlocks.at("edit colors");
    expectEqual(palettes.size(), std::size_t{256 * 64 * 2}, what + ": palettes size");
    for (std::size_t index = 0; index < 256 * 64; ++index) {
        expectEqual(u16At(palettes, index * 2), index < colorCount ? colors[index] : uint16_t{0},
                    what + ": palette entry " + std::to_string(index));
    }
    for (std::size_t index = 0; index < 16; ++index) {
        expectEqual(u16At(editColors, index * 2), index < colorCount ? colors[index] : uint16_t{0},
                    what + ": edit color " + std::to_string(index));
    }
    for (const char* zero : {"color sets", "draw settings", "mask names", "sections",
                             "sprite names", "sprite source frames", "sprite source rectangles",
                             "image positions", "palette names", "imported palettes",
                             "elapsed time", "PuP pack"}) {
        expectFilled(crpBlocks, zero, crpBlocks.at(zero).size(), 0, what);
    }
    // The ramp of the Win32 editor: 5 bits in red and the same 5 bits in the green field.
    expect(colors.front() == 0 && colors.back() == ((31 << 11) | (31 << 5)),
           what + ": plain colors run from black to the brightest");

    // libserum loads the cROM and identifies every original as its frame.
    RuntimePreview& preview = RuntimePreview::instance();
    std::string error;
    const std::vector<uint8_t> cromBytes(crom.begin(), crom.end());
    expect(preview.openCrom(cromBytes, geometry, n,
                            colorizing::runtime::effectivePlaneRequest(geometry,
                                                                       PlaneRequest::Rows32),
                            &error),
           what + ": libserum loads the new project: " + error);
    for (std::size_t frame = 0; frame < n; ++frame) {
        const RuntimeFrame rendered = preview.colorize(kept[frame].original);
        expect(rendered.ok() && rendered.identified,
               what + ": libserum identifies frame " + std::to_string(frame) + " " +
                   rendered.error);
        if (!rendered.unchanged) {
            expect(rendered.frameIndex && *rendered.frameIndex == frame,
                   what + ": libserum identifies frame " + std::to_string(frame) + " as itself");
        }
        // The inactive frame is identified and not rendered.
        expectEqual(rendered.skipped, kept[frame].durationMs < 15, what + ": skipped frames");
    }
    IndexedFrame unknown = kept[0].original;
    unknown.pixels[7] = static_cast<uint8_t>((unknown.pixels[7] + 1) % shades);
    expect(!preview.colorize(unknown).identified, what + ": another frame is not identified");
    preview.close();

    // In the document a frame is complete: nothing is filled in lazily later.
    expect(project.comparisonMaskId(0) == document::kNoComparisonMaskId &&
               project.backgroundId(0) == document::kNoBackgroundId &&
               project.triggerId(0) == document::kNoTriggerId &&
               project.dynamicData(0).has_value() && project.backgroundData(0).has_value() &&
               project.frameSpriteReferences(0).size() == document::kSpritesPerFrame &&
               project.paletteCount() == document::kPaletteCount,
           what + ": document defaults");
    expectEqual(project.hasHdFrame(0), expectedLayout == PlaneLayout::NativeExtra,
                what + ": extra-plane picture");
    if (expectedLayout == PlaneLayout::NativeExtra) {
        expect(!project.frameHdGaps(0).any(), what + ": the runtime renders the frame");
    }
}

void testNewProjectRefusals()
{
    const auto throws = [](const std::string& name, DumpImportResult dump) {
        try {
            (void)makeProjectSnapshot(name, std::move(dump));
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    const std::string text = dumpText({textFrame(0, 1)});
    expect(throws("a/b", importText(text)), "a name with a path character");
    expect(throws("", importText(text)), "no name");
    expect(!throws("rom", importText(text)), "a good name");
    DumpImportResult empty = importText(text);
    empty.frames.clear();
    expect(throws("rom", std::move(empty)), "no frames");
    expect(throws("rom", importText("")), "a failed import");
}

std::string serialized(const ProjectDocument& project)
{
    const ProjectSnapshot snapshot = project.toSnapshot();
    return serializedCrom(snapshot) + serializedCrp(snapshot);
}

void testAddFrames(Size dumpSize)
{
    const std::string what = "add to " + std::to_string(dumpSize.width) + "x" +
                             std::to_string(dumpSize.height);
    // Timecodes that go back at the join, so that the frames have the same durations in
    // one dump as in two.
    const std::vector<TextFrame> first{textFrame(500, 1, dumpSize), textFrame(540, 2, dumpSize),
                                       textFrame(600, 3, dumpSize)};
    std::vector<TextFrame> second{textFrame(100, 4, dumpSize), textFrame(170, 2, dumpSize),
                                  textFrame(250, 5, dumpSize), textFrame(40, 6, dumpSize)};
    std::vector<TextFrame> joined = first;
    joined.insert(joined.end(), second.begin(), second.end());

    ProjectDocument project = projectOf(first);
    const std::string before = serialized(project);
    const uint64_t revisionBefore = project.identificationRevision();

    DumpImportOptions options;
    options.useMasksAndShapeModes = true;
    DumpImportResult dump = importText(dumpText(second), options, &project);
    expect(dump.ok, what + ": imports: " + dump.error.text());
    expect(dump.counts.framesRead == 4 && dump.counts.identicalInProject == 1 &&
               dump.counts.framesKept == 3,
           what + ": one frame is in the project already");
    std::vector<document::FrameRecord> records = makeProjectFrames(project, std::move(dump));
    expectEqual(records.size(), std::size_t{3}, what + ": frames to add");
    expect(dump.frames.empty(), what + ": the frames are taken out of the result");

    edit::UndoHistories histories;
    std::unique_ptr<edit::StructureCommand> command = edit::addFrames(project, std::move(records));
    expect(command && command->addsRecords() && command->recordKind() == edit::RecordKind::Frame &&
               command->recordsPresent(),
           what + ": the command adds frames");
    expect(command->recordIndices() == std::vector<std::size_t>({3, 4, 5}),
           what + ": the frames are appended");
    expect(command->renumberedRecords().size() == 1 &&
               command->renumberedRecords()[0].kind == edit::RecordKind::Frame &&
               command->renumberedRecords()[0].firstIndex == 3,
           what + ": frames from the first added one on are named as renumbered");
    histories.push(edit::structureHistoryKey(), std::move(command));
    expectEqual(project.frameCount(), std::size_t{6}, what + ": frame count");
    expect(project.isDirty() && project.identificationRevision() != revisionBefore,
           what + ": the document changed");

    // The same project as one made of both dumps at once.
    const ProjectDocument whole = projectOf(joined);
    const std::string added = serialized(project);
    expect(added == serialized(whole), what + ": equals a project of all frames");
    expectEqual(project.frameDurationMs(3), 70u, what + ": duration of an added frame");
    expectEqual(project.frameDurationMs(5), 30u, what + ": duration of the last added frame");
    expect(project.comparisonMaskId(4) == document::kNoComparisonMaskId &&
               project.shapeComparisonMode(4) == 0 &&
               project.backgroundId(4) == document::kNoBackgroundId,
           what + ": defaults of an added frame");

    // An edit of an added frame and one of an old frame, each in its own history.
    const Resolution plane = project.geometry().nativePlane();
    const edit::HistoryKey addedKey{edit::EditKind::FramePixels, 4, plane};
    const edit::HistoryKey oldKey{edit::EditKind::FramePixels, 1, plane};
    for (const edit::HistoryKey& key : {addedKey, oldKey}) {
        auto pixelEdit = std::make_unique<edit::PixelEditCommand>(
            edit::EditKind::FramePixels, key.index, plane,
            std::vector<edit::PixelChange>{
                {3, 3, project.colorizedPixel(key.index, plane, 3, 3), 0x1234}});
        pixelEdit->apply(project);
        histories.push(key, std::move(pixelEdit));
    }
    expect(histories.canUndo(addedKey) && histories.canUndo(oldKey), what + ": two edits");

    // Undo removes exactly the added frames; the history of the added frame goes with them.
    const edit::EditCommand* undone = histories.undo(edit::structureHistoryKey(), project);
    expect(undone != nullptr, what + ": the addition is undone");
    expectEqual(project.frameCount(), std::size_t{3}, what + ": frame count after undo");
    expect(!histories.canUndo(addedKey), what + ": the history of a removed frame is dropped");
    expect(histories.canUndo(oldKey), what + ": the history of an older frame stays");
    expect(histories.undo(oldKey, project) != nullptr, what + ": the older edit is undone");
    expect(serialized(project) == before, what + ": undo restores the project");

    // Redo puts the frames back as they were when they were taken out, edit included.
    expect(histories.redo(edit::structureHistoryKey(), project) != nullptr, what + ": redo");
    expectEqual(project.frameCount(), std::size_t{6}, what + ": frame count after redo");
    expectEqual(project.colorizedPixel(4, plane, 3, 3), uint16_t{0x1234},
                what + ": a redo keeps what was edited before the undo");
    expect(histories.canRedo(oldKey), what + ": the history of an older frame stays again");
    expect(histories.undo(edit::structureHistoryKey(), project) != nullptr, what + ": undo again");
    expect(serialized(project) == before, what + ": the second undo restores the project too");

    // Nothing to add is no command; a frame of another size is refused.
    expect(edit::addFrames(project, {}) == nullptr, what + ": no frames, no command");
    const Size otherSize = dumpSize.width == 128 ? Size{192, 64} : Size{128, 32};
    bool refused = false;
    try {
        (void)project.makeUncolorizedFrame(document::makeIndexedFrame(otherSize), 30);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, what + ": an original of another size is refused");
    refused = false;
    try {
        (void)project.makeUncolorizedFrame(
            document::makeIndexedFrame(project.originalFrameSize(), 9), 30);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, what + ": a shade the project has no color for is refused");
    expect(serialized(project) == before, what + ": refusals change nothing");
}

// Sections and sprite source frames do not move when frames are appended and come back
// byte for byte when the addition is undone.
void testAddFramesKeepsTables()
{
    ProjectDocument project = projectOf({textFrame(0, 1), textFrame(50, 2), textFrame(90, 3)});
    document::RecordTables tables = project.recordTables();
    tables.sectionCount = 2;
    tables.sectionFirstFrames[0] = 0;
    tables.sectionFirstFrames[1] = 2;
    document::setFixedName(tables.sectionNames, 1, document::kSectionNameBytes, "attract");
    // An entry behind the last section, as the Win32 editor leaves them.
    tables.sectionFirstFrames[5] = 77;
    project.restoreRecordTables(tables);
    const std::size_t sprite = project.appendSprite(project.makeBlankSprite("ball"), 2);
    expectEqual(sprite, std::size_t{0}, "a sprite");
    const std::string before = serialized(project);

    DumpImportResult dump = importText(dumpText({textFrame(0, 7), textFrame(40, 8)}), {}, &project);
    edit::UndoHistories histories;
    histories.push(edit::structureHistoryKey(),
                   edit::addFrames(project, makeProjectFrames(project, std::move(dump))));
    expectEqual(project.frameCount(), std::size_t{5}, "two frames added");
    const document::RecordTables after = project.recordTables();
    expect(after.sectionCount == 2 && after.sectionFirstFrames == tables.sectionFirstFrames &&
               after.sectionNames == tables.sectionNames &&
               after.spriteSourceFrames[0] == 2,
           "sections and sprite sources stay where they are");
    // Deleting an added frame and undoing both, in order.
    histories.push(edit::structureHistoryKey(), edit::deleteFrames(project, {3}));
    expectEqual(project.frameCount(), std::size_t{4}, "one of the added frames deleted");
    expect(histories.undo(edit::structureHistoryKey(), project) != nullptr, "undo the deletion");
    expect(histories.undo(edit::structureHistoryKey(), project) != nullptr, "undo the addition");
    expect(serialized(project) == before, "both undone: the project is as it was");
}

}  // namespace

// The bytes of a gzip file (RFC 1952) of the text.
std::string gzipOf(const std::string& text)
{
    mz_stream stream{};
    expect(mz_deflateInit2(&stream, MZ_BEST_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9,
                           MZ_DEFAULT_STRATEGY) == MZ_OK,
           "deflate starts");
    std::string deflated(mz_deflateBound(&stream, static_cast<mz_ulong>(text.size())), '\0');
    stream.next_in = reinterpret_cast<const unsigned char*>(text.data());
    stream.avail_in = static_cast<unsigned int>(text.size());
    stream.next_out = reinterpret_cast<unsigned char*>(deflated.data());
    stream.avail_out = static_cast<unsigned int>(deflated.size());
    expect(mz_deflate(&stream, MZ_FINISH) == MZ_STREAM_END, "deflate ends");
    deflated.resize(stream.total_out);
    mz_deflateEnd(&stream);
    // Header with a file name (FNAME), as gzip writes it.
    std::string bytes("\x1f\x8b\x08\x08\0\0\0\0\0\x03", 10);
    bytes += "dump.txt";
    bytes.push_back('\0');
    bytes += deflated;
    const auto appendU32 = [&bytes](uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<char>((value >> shift) & 0xff));
        }
    };
    appendU32(static_cast<uint32_t>(
        mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(text.data()), text.size())));
    appendU32(static_cast<uint32_t>(text.size()));
    return bytes;
}

// A dump compressed as .gz or in a .zip reads as the same dump.
void testCompressedDumps()
{
    std::vector<TextFrame> frames;
    for (int index = 0; index < 120; ++index) {
        frames.push_back(textFrame(static_cast<uint32_t>(index * 20), index));
    }
    const std::string text = dumpText(frames);
    const DumpImportResult plain = importText(text);
    expect(plain.ok, "the plain dump imports");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("colorizing_qt_v2_dumps_" +
         std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto write = [](const std::filesystem::path& path, const std::string& bytes) {
        std::ofstream(path, std::ios::binary).write(bytes.data(),
                                                    static_cast<std::streamsize>(bytes.size()));
    };
    // A gzip file with another name, and a zip whose dump is not its first entry.
    write(directory / "dump.dat", gzipOf(text));
    const std::string zipPath = (directory / "dump.zip").string();
    mz_zip_archive zip{};
    expect(mz_zip_writer_init_file(&zip, zipPath.c_str(), 0) &&
               mz_zip_writer_add_mem(&zip, "readme.md", "notes", 5, MZ_DEFAULT_COMPRESSION) &&
               mz_zip_writer_add_mem(&zip, "afm_113b.txt", text.data(), text.size(),
                                     MZ_DEFAULT_COMPRESSION) &&
               mz_zip_writer_finalize_archive(&zip) && mz_zip_writer_end(&zip),
           "the zip is written");

    for (const char* name : {"dump.dat", "dump.zip"}) {
        const std::string path = (directory / name).string();
        const importing::DumpFile file = importing::openDumpFile(path);
        expect(file.ok() && file.sizeBytes == text.size(),
               std::string(name) + " opens with the size of the dump");
        uint64_t total = 0;
        uint64_t lastBytes = 0;
        const DumpImportResult result = importing::importFrameDumpFile(
            path, {}, nullptr,
            [&lastBytes](uint64_t bytes) {
                lastBytes = bytes;
                return true;
            },
            &total);
        expect(result.ok && result.frames.size() == plain.frames.size() &&
                   result.counts.framesRead == plain.counts.framesRead && total == text.size() &&
                   lastBytes == text.size(),
               std::string(name) + " imports as the plain dump, with its progress");
    }
    expect(importing::openDumpFile((directory / "dump.zip").string()).entryName ==
               "afm_113b.txt",
           "of a zip the .txt entry is read");

    const std::string zipPathNoText = (directory / "other.zip").string();
    mz_zip_archive empty{};
    expect(mz_zip_writer_init_file(&empty, zipPathNoText.c_str(), 0) &&
               mz_zip_writer_finalize_archive(&empty) && mz_zip_writer_end(&empty),
           "an empty zip is written");
    const importing::DumpFile none = importing::openDumpFile(zipPathNoText);
    expect(!none.ok() && none.error.find("no dump") != std::string::npos,
           "a zip without a file holds no dump");
    write(directory / "cut.gz", std::string("\x1f\x8b\x08", 3));
    expect(!importing::openDumpFile((directory / "cut.gz").string()).ok(),
           "a gzip file cut in its header is refused");
    std::filesystem::remove_all(directory);
}

void runFrameDumpImportTests()
{
    testReaderSizes();
    testReaderForms();
    testReaderTruncation();
    testReaderErrors();
    testDurations();
    testFilters();
    testTransitionalFrames();
    testDuplicatesInDump();
    testAgainstProject();
    testCancelAndFiles();
    testCompressedDumps();
    testNames();
    testNewProject(Size{128, 32}, 4);
    testNewProject(Size{128, 32}, 16);
    testNewProject(Size{128, 16}, 4);
    testNewProject(Size{192, 64}, 16);
    testNewProject(Size{256, 64}, 16);
    testNewProjectRefusals();
    testAddFrames(Size{128, 32});
    testAddFrames(Size{192, 64});
    testAddFrames(Size{256, 64});
    testAddFramesKeepsTables();
}
