// Playback through the runtime preview identifies frames like the whole project does.
//
// libserum identifies a ROM frame by order: it starts at the frame identified last, and several
// frames can match one ROM frame (RuntimePreview::openSequence(), FrameIdentification). The
// preview never loads the whole project; it loads windows of the played frames with the other
// frames that take part in identifying them, and carries libserum's identification state from
// one window to the next. Here a project built to have such frames is played through the
// preview, with windows of several sizes, and through libserum with the whole project written
// as one cROM, and every step has to come out the same.

#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectSerializer.h"
#include "render/ProjectGeometry.h"
#include "runtime/FrameIdentification.h"
#include "runtime/RuntimePreview.h"
#include "RuntimeFixtures.h"

using colorizing::document::IndexedFrame;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectSnapshot;
using colorizing::runtime::FrameIdentification;
using colorizing::runtime::RuntimeFrame;
using colorizing::runtime::RuntimePlane;
using colorizing::runtime::RuntimePreview;
using namespace colorizing::runtime_fixtures;

namespace {

std::string g_context;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: playback identification: " << g_context << ": " << message << '\n';
        std::exit(1);
    }
}

const char* layoutName(PlaneLayout layout)
{
    switch (layout) {
        case PlaneLayout::Ordinary:
            return "128x32";
        case PlaneLayout::NativeExtra:
            return "256x64 original";
        case PlaneLayout::NativeOriginal:
            break;
    }
    return "192x64 original";
}

constexpr std::size_t kFrames = 16;
constexpr std::size_t kInactiveFrame = 12;

uint16_t flatColor(std::size_t frame)
{
    return static_cast<uint16_t>(0x0841 * (frame + 1));
}

// Frames that share hashes. Mask 0 covers the right half of the ROM frame, mask 1 the left
// half; "L" and "R" name what a frame's original has in its halves.
//
//   frame  original           mask  shape  matches the original of
//   0      L=a   R=a          -     0      0
//   1      L=x   R=r1         0     0      1, 2, 4, 10 (left half x), 6
//   2      L=x   R=r2         0     0      the same
//   3      L=c   R=c          -     0      3
//   4      L=x   R=r3         0     0      the same as 1
//   5      L=y   R=z          1     0      5, 11 (right half z), 15
//   6      L=x   R=r1         -     0      6, and the frames 1 matches
//   7      L=x   R=z          -     0      7, and 1, 2, 4, 10 (left x), 5, 11 (right z)
//   8      lit pattern p      -     1      8, 9 (the same pixels lit)
//   9      p, other shades    -     1      8, 9
//   10     L=x   R=r4         0     0      the same as 1
//   11     L=d   R=z          1     0      5, 11
//   12     L=e   R=e          -     0      12; shorter than 15 ms and not colorized: inactive
//   13     L=x   R=r2         -     0      13, and 1, 2, 4, 10
//   14     L=f   R=f          -     0      14
//   15     L=y   R=z          -     0      15, and 5, 11
//
// So the original of frame 1 is identified as frame 1, 2, 4, 6 or 10 depending on where the
// search starts, and which of two masks' frames wins depends on which mask the search meets
// first, which frames that match nothing decide.
ProjectSnapshot makeProjectWithSharedHashes(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, kFrames);
    const Size rom = snapshot.geometry.romFrameSize();
    const int half = rom.width / 2;
    snapshot.comparisonMasks.assign(2, makeIndexedFrame(rom, 0));
    for (int y = 0; y < rom.height; ++y) {
        for (int x = 0; x < rom.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * rom.width + x;
            snapshot.comparisonMasks[0].pixels[index] = x >= half ? 1 : 0;
            snapshot.comparisonMasks[1].pixels[index] = x < half ? 1 : 0;
        }
    }
    // A half is a pattern of shades with its number written into its first row.
    const auto fillHalf = [&](std::size_t frame, bool right, int pattern) {
        IndexedFrame& original = snapshot.frames[frame].originalSd;
        const int left = right ? half : 0;
        for (int y = 0; y < rom.height; ++y) {
            for (int x = 0; x < half; ++x) {
                original.pixels[static_cast<std::size_t>(y) * rom.width + x + left] =
                    static_cast<uint8_t>((x * 3 + y) % static_cast<int>(kColorCount));
            }
        }
        for (int bit = 0; bit < 8; ++bit) {
            original.pixels[static_cast<std::size_t>(left + bit)] =
                (pattern >> bit) & 1 ? uint8_t{3} : uint8_t{0};
        }
    };
    const auto setFrame = [&](std::size_t frame, int left, int right, uint8_t mask) {
        fillHalf(frame, false, left);
        fillHalf(frame, true, right);
        snapshot.frames[frame].comparisonMaskId = mask;
    };
    enum : int { a = 1, c, d, e, f, x, y, z, r1, r2, r3, r4 };
    setFrame(0, a, a, 255);
    setFrame(1, x, r1, 0);
    setFrame(2, x, r2, 0);
    setFrame(3, c, c, 255);
    setFrame(4, x, r3, 0);
    setFrame(5, y, z, 1);
    setFrame(6, x, r1, 255);
    setFrame(7, x, z, 255);
    setFrame(10, x, r4, 0);
    setFrame(11, d, z, 1);
    setFrame(12, e, e, 255);
    setFrame(13, x, r2, 255);
    setFrame(14, f, f, 255);
    setFrame(15, y, z, 255);
    // Two shape-mode frames with the same pixels lit in different shades.
    for (std::size_t frame : {std::size_t{8}, std::size_t{9}}) {
        IndexedFrame& original = snapshot.frames[frame].originalSd;
        for (std::size_t i = 0; i < original.pixels.size(); ++i) {
            const bool lit = (i * 7 + i / 5) % 3 == 0;
            original.pixels[i] = !lit ? uint8_t{0}
                                      : frame == 8 ? uint8_t{1}
                                                   : static_cast<uint8_t>(1 + i % 3);
        }
        snapshot.frames[frame].shapeComparisonMode = 1;
    }
    // One flat color per frame: the picture tells which frame was rendered.
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        Rgb565Frame& colors = nativeColors(snapshot, frame);
        std::fill(colors.pixels.begin(), colors.pixels.end(), flatColor(frame));
    }
    // The inactive frame: the plain colors of its original and too short to count.
    {
        const std::vector<uint16_t> plain = colorizing::legacy::legacyOriginalColors(kColorCount);
        Rgb565Frame& colors = nativeColors(snapshot, kInactiveFrame);
        for (std::size_t i = 0; i < colors.pixels.size(); ++i) {
            colors.pixels[i] = plain[snapshot.frames[kInactiveFrame].originalSd.pixels[i]];
        }
        snapshot.frames[kInactiveFrame].durationMs = 5;
    }
    // A sprite with dynamic content, in the frame list of frame 3 and found nowhere. In a
    // project with 256x64 original frames it makes the subset start with a frame of its own
    // (see RuntimePreview.cpp), which must not disturb the search either.
    const std::size_t sprite = addSprite(snapshot);
    snapshot.sprites[sprite].dynamicMaskSd.pixels[1] = 0;
    snapshot.sprites[sprite].dynamicMaskHd.pixels[1] = 0;
    setSpriteSlot(snapshot, 3, 0, sprite, 0, 0, rom.width - 1, rom.height - 1);
    return snapshot;
}

bool samePlane(const std::optional<RuntimePlane>& lhs, const std::optional<RuntimePlane>& rhs)
{
    return lhs.has_value() == rhs.has_value() &&
           (!lhs || (lhs->image.size == rhs->image.size && lhs->image.pixels == rhs->image.pixels));
}

std::vector<uint8_t> wholeCrom(const ProjectDocument& document)
{
    std::ostringstream stream;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrom(stream, document.toSnapshot(), error),
           "the project is written: " + error);
    const std::string bytes = stream.str();
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

// What libserum does with the whole project loaded, step by step. libserum does not say
// which frame it identified when it reports the picture already on display; `identified` is
// then taken from its search rule, as the preview does.
struct Step {
    RuntimeFrame frame;
    std::optional<std::size_t> identified;
    bool reported = false;
};

std::vector<Step> playWholeProject(const ProjectDocument& document,
                                   const std::vector<uint8_t>& crom,
                                   const std::vector<std::size_t>& order,
                                   PlaneRequest request)
{
    RuntimePreview& preview = RuntimePreview::instance();
    const FrameIdentification rule(document);
    std::string error;
    expect(preview.openCrom(crom, document.geometry(), document.frameCount(), request, &error),
           "the whole project loads: " + error);
    std::vector<Step> steps;
    std::size_t searchStart = 0;
    for (std::size_t frame : order) {
        Step step;
        const std::optional<std::size_t> expected =
            rule.identify(document.originalFrame(frame), searchStart);
        step.frame = preview.colorize(document.originalFrame(frame));
        expect(step.frame.ok(), "the whole project colorizes: " + step.frame.error);
        step.reported = !step.frame.unchanged;
        if (step.reported) {
            // The search rule is libserum's: wherever libserum says which frame it
            // identified, the rule has to say the same.
            expect(step.frame.frameIndex == expected,
                   "frame " + std::to_string(frame) + " after frame " +
                       std::to_string(searchStart) + ": libserum identifies frame " +
                       (step.frame.frameIndex ? std::to_string(*step.frame.frameIndex)
                                              : std::string("none")) +
                       ", the search rule says " +
                       (expected ? std::to_string(*expected) : std::string("none")));
        }
        step.identified = step.reported ? step.frame.frameIndex : expected;
        if (step.identified) {
            searchStart = *step.identified;
        }
        steps.push_back(std::move(step));
    }
    preview.close();
    return steps;
}

struct PlaybackTotals {
    std::size_t steps = 0;
    std::size_t otherFrameIdentified = 0;
    std::size_t unchanged = 0;
    std::size_t skipped = 0;
    std::size_t windows = 0;
    std::size_t largestWindow = 0;
};

// The same order through the preview, as the Runtime view feeds it: a new window whenever
// the loaded one does not play the frame.
void expectPlaybackLikeWholeProject(const ProjectDocument& document,
                                    const std::vector<Step>& whole,
                                    const std::vector<std::size_t>& order,
                                    PlaneRequest request,
                                    std::size_t windowFrames,
                                    PlaybackTotals& totals)
{
    RuntimePreview& preview = RuntimePreview::instance();
    preview.close();
    for (std::size_t i = 0; i < order.size(); ++i) {
        const std::string text = "window of " + std::to_string(windowFrames) + ", step " +
                                 std::to_string(i) + ", frame " + std::to_string(order[i]);
        if (!preview.sequenceHasFrame(order[i])) {
            const std::vector<std::size_t> rest(order.begin() + static_cast<std::ptrdiff_t>(i),
                                                order.end());
            std::string error;
            const bool opened =
                i == 0 ? preview.openSequence(document, rest, request, &error, windowFrames)
                       : preview.continueSequence(document, rest, request, &error, windowFrames);
            expect(opened, text + ": the window loads: " + error);
            expect(preview.sequenceHasFrame(order[i]), text + ": the window plays the frame");
            ++totals.windows;
            totals.largestWindow =
                std::max(totals.largestWindow, preview.sequenceLoadedFrameCount());
        }
        const RuntimeFrame played = preview.renderSequenceFrame(order[i]);
        const Step& expected = whole[i];
        expect(played.ok(), text + ": no error (" + played.error + ")");
        expect(played.frameIndex == expected.identified,
               text + ": identified as frame " +
                   (played.frameIndex ? std::to_string(*played.frameIndex) : std::string("none")) +
                   ", with the whole project as frame " +
                   (expected.identified ? std::to_string(*expected.identified)
                                        : std::string("none")));
        expect(played.identified == expected.identified.has_value(),
               text + ": identified or not, as with the whole project");
        expect(played.unchanged == expected.frame.unchanged,
               text + ": reported as unchanged exactly when the whole project does");
        expect(played.skipped == expected.frame.skipped,
               text + ": skipped exactly when the whole project skips it");
        expect(played.expectedFrameIndex == played.frameIndex,
               text + ": the search rule and libserum agree");
        expect(samePlane(played.rows32, expected.frame.rows32) &&
                   samePlane(played.rows64, expected.frame.rows64),
               text + ": the same picture as with the whole project");
        ++totals.steps;
        totals.otherFrameIdentified += played.frameIndex != order[i] ? 1 : 0;
        totals.unchanged += played.unchanged ? 1 : 0;
        totals.skipped += played.skipped ? 1 : 0;
    }
    preview.close();
}

void testSearchRule(PlaneLayout layout)
{
    const ProjectDocument document =
        ProjectDocument::fromSnapshot(makeProjectWithSharedHashes(layout));
    const FrameIdentification rule(document);
    const auto original = [&](std::size_t frame) -> const IndexedFrame& {
        return document.originalFrame(frame);
    };
    using Frames = std::vector<std::size_t>;
    expect(rule.matches(original(0)) == Frames{0}, "a frame like no other matches itself");
    expect(rule.matches(original(1)) == (Frames{1, 2, 4, 6, 10}),
           "frames with the same original under their mask match, and a frame without mask "
           "with the whole original");
    expect(rule.matches(original(7)) == (Frames{1, 2, 4, 5, 7, 10, 11}),
           "a frame matches the frames of every mask it looks the same under");
    expect(rule.matches(original(9)) == (Frames{8, 9}), "shape mode compares lit pixels");

    // From frame 0 the search meets the bucket without mask first (frame 0 is in it), so
    // the unmasked copy wins over the masked frames before it.
    expect(rule.identify(original(1), 0) == std::size_t{6}, "the bucket met first decides");
    // From frame 1 it meets mask 0 first, and takes that bucket's first match from there on.
    expect(rule.identify(original(1), 1) == std::size_t{1} &&
               rule.identify(original(1), 2) == std::size_t{2} &&
               rule.identify(original(1), 4) == std::size_t{4} &&
               rule.identify(original(1), 10) == std::size_t{10},
           "within a bucket the first match at or after the search start wins");
    // From frame 5 (mask 1, no match) the bucket without mask comes next, at frame 6.
    expect(rule.identify(original(1), 5) == std::size_t{6},
           "a bucket without a match is passed over");
    // From frame 5 (mask 1) the next buckets are: no mask at frame 6, mask 0 at frame 10.
    expect(rule.identify(original(2), 5) == std::size_t{13},
           "a frame that matches nothing still decides which bucket is met first");
    expect(rule.identify(original(1), 11) == std::size_t{6},
           "the search wraps around at the last frame");
    expect(rule.identify(original(9), 9) == std::size_t{9} &&
               rule.identify(original(9), 10) == std::size_t{8},
           "shape-mode frames are found the same way");
    IndexedFrame unknown = original(0);
    unknown.pixels[5] ^= 3;
    expect(!rule.identify(unknown, 0) && rule.matches(unknown).empty(),
           "a ROM frame no frame matches is not identified");

    // The frames a window needs: the matches, where the search can start, and the frame
    // each of their buckets is met at from every such start.
    const Frames forFrame3 = rule.candidates(document, {3}, 3);
    expect(forFrame3 == Frames{3}, "a frame like no other needs itself only");
    const Frames forFrame5 = rule.candidates(document, {5}, 0);
    // Matches 5, 11, 15 and the start 0; their buckets: no mask (0, 15) and mask 1 (5, 11).
    // From 0: no mask at 0, mask 1 at 5. From 5: 6 and 5. From 11: 12 and 11. From 15: 15
    // and 5.
    expect(forFrame5 == (Frames{0, 5, 6, 11, 12, 15}),
           "the frames the buckets are met at are part of the window");
}

void testPlayback(PlaneLayout layout)
{
    const ProjectDocument document =
        ProjectDocument::fromSnapshot(makeProjectWithSharedHashes(layout));
    const std::vector<uint8_t> crom = wholeCrom(document);
    const PlaneRequest request = nativeRequest(document.geometry());

    std::vector<std::vector<std::size_t>> orders;
    std::vector<std::size_t> inOrder;
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        inOrder.push_back(frame);
    }
    // In project order, twice: the second round starts where the first ended.
    orders.push_back(inOrder);
    orders.back().insert(orders.back().end(), inOrder.begin(), inOrder.end());
    // Backwards.
    orders.emplace_back(inOrder.rbegin(), inOrder.rend());
    // Jumping about, with frames fed several times in a row.
    {
        std::vector<std::size_t> jumping;
        std::size_t frame = 1;
        for (std::size_t i = 0; i < 120; ++i) {
            jumping.push_back(frame);
            if (i % 5 != 0) {
                frame = (frame * 7 + i * 3 + 5) % kFrames;
            }
        }
        orders.push_back(std::move(jumping));
    }
    // Only frames that share hashes.
    orders.push_back({2, 1, 4, 13, 6, 10, 7, 5, 11, 15, 7, 1, 1, 9, 8, 9, 2, 12, 2, 12, 1});
    // The original of frame 1 identified as frame 1, 4, 6 and 10 in turn. Fed first, the
    // original of frame 4 is identified as frame 1: no frame without mask matches it, and
    // frame 1 is the first of mask 0 that does.
    orders.push_back({4, 1, 3, 4, 1, 5, 1, 10, 1, 15, 1, 12, 1});

    PlaybackTotals totals;
    std::set<std::size_t> identifiedForFrame1;
    for (const std::vector<std::size_t>& order : orders) {
        const std::vector<Step> whole = playWholeProject(document, crom, order, request);
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == 1 && whole[i].identified) {
                identifiedForFrame1.insert(*whole[i].identified);
            }
        }
        for (std::size_t windowFrames : {std::size_t{1}, std::size_t{2}, std::size_t{3},
                                         std::size_t{5}, std::size_t{48}}) {
            expectPlaybackLikeWholeProject(document, whole, order, request, windowFrames, totals);
        }
    }
    // The test means something only if the hard cases occurred.
    expect(totals.otherFrameIdentified > 50,
           "ROM frames were identified as other frames than the one fed (" +
               std::to_string(totals.otherFrameIdentified) + ")");
    expect(identifiedForFrame1.size() >= 4,
           "one ROM frame was identified as several frames, depending on the order");
    expect(totals.unchanged > 0, "frames were reported as unchanged");
    expect(totals.skipped > 0, "the inactive frame was identified and skipped");
    expect(totals.windows > totals.steps / 6, "windows were continued");
    expect(totals.largestWindow < kFrames + 2,
           "a window holds the frames it needs, not more than the project");
}

}  // namespace

void runPlaybackIdentificationTests()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        g_context = layoutName(layout);
        testSearchRule(layout);
        testPlayback(layout);
    }
}
