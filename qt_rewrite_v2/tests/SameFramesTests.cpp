// Same frames under a comparison mask and shape mode, the frames "delete same frames"
// removes, the pixels that differ among frames, and writing them into a comparison mask.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/SameFrames.h"
#include "runtime/FrameIdentification.h"

using colorizing::document::IndexedFrame;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectSnapshot;
using colorizing::document::kNoComparisonMaskId;
using colorizing::edit::ComparisonRule;
using colorizing::edit::SameFrameCounts;
using colorizing::edit::SameFrameFinder;
using colorizing::render::ProjectGeometry;
using colorizing::render::Size;
using colorizing::runtime::FrameIdentification;

namespace document = colorizing::document;
namespace edit = colorizing::edit;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: same frames: " << message << '\n';
        std::exit(1);
    }
}

std::string listText(const std::vector<std::size_t>& values)
{
    std::string text = "{";
    for (std::size_t value : values) {
        text += (text.size() > 1 ? "," : "") + std::to_string(value);
    }
    return text + "}";
}

void expectFrames(const std::vector<std::size_t>& actual,
                  const std::vector<std::size_t>& expected,
                  const std::string& message)
{
    expect(actual == expected,
           message + ": " + listText(actual) + ", expected " + listText(expected));
}

IndexedFrame pattern(int seed)
{
    IndexedFrame frame = document::makeIndexedFrame(Size{128, 32});
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            frame.pixels[static_cast<std::size_t>(y * 128 + x)] =
                static_cast<uint8_t>((x * 7 + y * 3 + x / (seed + 2) + seed * 5) % 4);
        }
    }
    return frame;
}

ProjectDocument documentOf(const std::vector<IndexedFrame>& originals)
{
    ProjectSnapshot snapshot;
    snapshot.romName = "same";
    snapshot.colorCount = 4;
    for (const IndexedFrame& original : originals) {
        snapshot.frames.push_back(
            document::makeUncolorizedFrameSnapshot(ProjectGeometry(), 4, original, 30));
    }
    return ProjectDocument::fromSnapshot(std::move(snapshot));
}

// Frames 0 to 7, masks 0 (the left 16 columns) and 1 (the top 8 rows):
//   0  A                              no mask
//   1  A with another pixel 5         mask 0   (pixel 5 is under mask 0)
//   2  A                              no mask
//   3  A in other shades              no mask, shape mode
//   4  C                              no mask
//   5  A with another pixel in row 9   mask 0   (column 100: under neither mask)
//   6  frame 1 again                  no mask
//   7  C                              mask 1
ProjectDocument makeDocument()
{
    const IndexedFrame a = pattern(1);
    IndexedFrame aUnderMask = a;
    aUnderMask.pixels[5] = static_cast<uint8_t>(aUnderMask.pixels[5] ^ 1);
    IndexedFrame aShades = a;
    for (uint8_t& pixel : aShades.pixels) {
        pixel = pixel == 0 ? uint8_t{0} : static_cast<uint8_t>(1 + pixel % 3);
    }
    IndexedFrame aOutside = a;
    // Row 9, column 100: outside both masks.
    aOutside.pixels[9 * 128 + 100] = static_cast<uint8_t>(aOutside.pixels[9 * 128 + 100] ^ 1);
    const IndexedFrame c = pattern(4);

    ProjectDocument project =
        documentOf({a, aUnderMask, a, aShades, c, aOutside, aUnderMask, c});
    expect(project.appendComparisonMask() == 0 && project.appendComparisonMask() == 1, "masks");
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
    project.setShapeComparisonMode(3, 1);
    project.setComparisonMaskId(5, 0);
    project.setComparisonMaskId(7, 1);
    return project;
}

// The Win32 editor's CheckSameFrames: pixel by pixel.
bool samePixels(const ProjectDocument& project,
                std::size_t frame,
                std::size_t other,
                ComparisonRule rule)
{
    const std::vector<uint8_t>& a = project.originalFrame(frame).pixels;
    const std::vector<uint8_t>& b = project.originalFrame(other).pixels;
    const std::vector<uint8_t>* mask = rule.maskId < project.comparisonMaskCount()
                                           ? &project.comparisonMask(rule.maskId).pixels
                                           : nullptr;
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (mask && (*mask)[index] > 0) {
            continue;
        }
        if (rule.shapeMode ? (a[index] == 0) != (b[index] == 0) : a[index] != b[index]) {
            return false;
        }
    }
    return true;
}

std::vector<std::size_t> samePixelFrames(const ProjectDocument& project,
                                         std::size_t frame,
                                         ComparisonRule rule)
{
    std::vector<std::size_t> frames;
    for (std::size_t other = 0; other < project.frameCount(); ++other) {
        if (other != frame && samePixels(project, frame, other, rule)) {
            frames.push_back(other);
        }
    }
    return frames;
}

// The Win32 editor's IDC_DELSELSAMEFR / IDC_DELALLSAMEFR loop: frame after frame, each
// deleting what is the same as it, with the frames renumbered as they go.
std::vector<std::size_t> deletedByWin32Loop(const ProjectDocument& project,
                                            const std::vector<std::size_t>& references,
                                            const std::vector<std::size_t>& protectedFrames)
{
    std::vector<std::size_t> alive;
    for (std::size_t frame = 0; frame < project.frameCount(); ++frame) {
        alive.push_back(frame);
    }
    for (std::size_t reference : references) {
        if (std::find(alive.begin(), alive.end(), reference) == alive.end()) {
            continue;
        }
        const ComparisonRule rule = edit::comparisonRuleOf(project, reference);
        std::vector<std::size_t> kept;
        for (std::size_t frame : alive) {
            const bool isProtected = std::find(protectedFrames.begin(), protectedFrames.end(),
                                               frame) != protectedFrames.end();
            if (frame == reference || isProtected || !samePixels(project, reference, frame, rule)) {
                kept.push_back(frame);
            }
        }
        alive = kept;
    }
    std::vector<std::size_t> deleted;
    for (std::size_t frame = 0; frame < project.frameCount(); ++frame) {
        if (std::find(alive.begin(), alive.end(), frame) == alive.end()) {
            deleted.push_back(frame);
        }
    }
    return deleted;
}

std::vector<ComparisonRule> allRules(const ProjectDocument& project)
{
    std::vector<ComparisonRule> rules;
    for (bool shapeMode : {false, true}) {
        rules.push_back({kNoComparisonMaskId, shapeMode});
        for (std::size_t mask = 0; mask < project.comparisonMaskCount(); ++mask) {
            rules.push_back({static_cast<uint8_t>(mask), shapeMode});
        }
    }
    return rules;
}

void testSameFrames()
{
    const ProjectDocument project = makeDocument();
    SameFrameFinder finder;

    expect(edit::comparisonRuleOf(project, 0).maskId == kNoComparisonMaskId &&
               !edit::comparisonRuleOf(project, 0).shapeMode &&
               edit::comparisonRuleOf(project, 1).maskId == 0 &&
               edit::comparisonRuleOf(project, 3).shapeMode &&
               edit::comparisonRuleOf(project, 7).maskId == 1,
           "the rule of a frame");

    // Under the frame's own rule.
    expectFrames(finder.sameFrames(project, 0, edit::comparisonRuleOf(project, 0)), {2},
                 "same as frame 0 without a mask");
    expectFrames(finder.sameFrames(project, 1, edit::comparisonRuleOf(project, 1)), {0, 2, 6},
                 "same as frame 1 under mask 0");
    expectFrames(finder.sameFrames(project, 5, edit::comparisonRuleOf(project, 5)), {},
                 "frame 5 differs outside its mask");
    expectFrames(finder.sameFrames(project, 4, edit::comparisonRuleOf(project, 4)), {7},
                 "same as frame 4");
    expectFrames(finder.sameFrames(project, 7, edit::comparisonRuleOf(project, 7)), {4},
                 "same as frame 7 under mask 1");
    expect(finder.sameFrameCount(project, 1, {0, false}) == 3, "the count is the list's size");

    // Every frame under every rule is what a pixel comparison finds.
    for (const ComparisonRule rule : allRules(project)) {
        for (std::size_t frame = 0; frame < project.frameCount(); ++frame) {
            const std::string what = "frame " + std::to_string(frame) + ", mask " +
                                     std::to_string(rule.maskId) +
                                     (rule.shapeMode ? ", shape mode" : "");
            expectFrames(finder.sameFrames(project, frame, rule),
                         samePixelFrames(project, frame, rule), what);
            expect(finder.sameFrameCount(project, frame, rule) ==
                       samePixelFrames(project, frame, rule).size(),
                   what + ": count");
        }
    }
    // Shape mode finds the frame in other shades and the frames that differ by a shade.
    expect(finder.sameFrameCount(project, 3, {kNoComparisonMaskId, true}) >
               finder.sameFrameCount(project, 3, {kNoComparisonMaskId, false}),
           "shape mode finds more");

    // The runtime's search rule: frame G is the same as frame F under F's rule exactly
    // when the runtime can identify G's original as F.
    const FrameIdentification rule(project);
    for (std::size_t g = 0; g < project.frameCount(); ++g) {
        const std::vector<std::size_t> matches = rule.matches(project.originalFrame(g));
        for (std::size_t f = 0; f < project.frameCount(); ++f) {
            if (f == g) {
                continue;
            }
            const std::vector<std::size_t> same =
                finder.sameFrames(project, f, edit::comparisonRuleOf(project, f));
            expect(std::binary_search(same.begin(), same.end(), g) ==
                       std::binary_search(matches.begin(), matches.end(), f),
                   "frame " + std::to_string(g) + " is the same as frame " + std::to_string(f) +
                       " exactly when the runtime identifies it as that frame");
        }
    }

    // A mask the project does not have is no mask.
    expectFrames(finder.sameFrames(project, 0, {9, false}),
                 finder.sameFrames(project, 0, {kNoComparisonMaskId, false}),
                 "a mask ID without a mask");
    bool thrown = false;
    try {
        (void)finder.sameFrames(project, 8, {});
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    expect(thrown, "a frame that does not exist");
}

void testCountsPerMask()
{
    const ProjectDocument project = makeDocument();
    SameFrameFinder finder;
    const std::vector<SameFrameCounts> counts = finder.countsPerMask(project, 1);
    expect(counts.size() == 3 && counts[0].maskId == kNoComparisonMaskId &&
               counts[1].maskId == 0 && counts[2].maskId == 1,
           "one line without a mask and one per mask");
    for (const SameFrameCounts& line : counts) {
        expect(line.fullComparison == samePixelFrames(project, 1, {line.maskId, false}).size() &&
                   line.shapeMode == samePixelFrames(project, 1, {line.maskId, true}).size(),
               "counts of mask " + std::to_string(line.maskId));
    }
    // Frame 1 without a mask has only its copy; mask 0 hides its one other pixel.
    expect(counts[0].fullComparison == 1 && counts[1].fullComparison == 3,
           "mask 0 makes frame 1 the same as three frames");

    std::vector<std::size_t> steps;
    const std::vector<SameFrameCounts> stopped =
        finder.countsPerMask(project, 1, [&](std::size_t done, std::size_t total) {
            expect(total == 6, "two rules per line");
            steps.push_back(done);
            return done < 3;
        });
    expectFrames(steps, {0, 1, 2, 3}, "progress before each rule");
    expect(stopped.size() == 1, "stopped: the complete lines so far");
}

void testDuplicates()
{
    const ProjectDocument project = makeDocument();
    SameFrameFinder finder;
    expectFrames(finder.duplicatesOf(project, {0}, {}), {2}, "same as frame 0");
    expectFrames(finder.duplicatesOf(project, {1}, {}), {0, 2, 6}, "same as frame 1");
    expectFrames(finder.duplicatesOf(project, {1}, {2}), {0, 6}, "a protected frame stays");
    // Frame 0 removes frame 2, frame 1 then removes frame 0: a reference can go for a
    // later one.
    expectFrames(finder.duplicatesOf(project, {1, 0}, {}), {0, 2, 6}, "two references");
    // A frame removed for an earlier reference is no reference any more.
    expectFrames(finder.duplicatesOf(project, {0, 2}, {}), {2}, "a removed reference");
    expectFrames(finder.duplicatesOf(project, {5}, {}), {}, "nothing is the same as frame 5");
    expectFrames(finder.duplicatesOf(project, {}, {}), {}, "no reference");
    expectFrames(finder.duplicatesOf(project, {1, 1, 1}, {0, 2, 6}), {}, "all protected");

    const std::vector<std::vector<std::size_t>> protections{{}, {2}, {0, 6}, {7}, {1, 3, 5}};
    for (const std::vector<std::size_t>& protectedFrames : protections) {
        std::vector<std::size_t> all;
        for (std::size_t frame = 0; frame < project.frameCount(); ++frame) {
            all.push_back(frame);
        }
        const std::string what = "protected " + listText(protectedFrames);
        expectFrames(finder.allDuplicates(project, protectedFrames),
                     deletedByWin32Loop(project, all, protectedFrames), "all duplicates, " + what);
        expectFrames(finder.duplicatesOf(project, {1, 4, 6}, protectedFrames),
                     deletedByWin32Loop(project, {1, 4, 6}, protectedFrames),
                     "duplicates of a selection, " + what);
    }
    const std::vector<std::size_t> all = finder.allDuplicates(project, {});
    expect(!all.empty() && all.size() < project.frameCount(), "some frames go, not all");

    // Frames that are all the same: one stays, the protected one if there is one.
    const ProjectDocument same = documentOf({pattern(2), pattern(2), pattern(2)});
    expectFrames(finder.allDuplicates(same, {}), {1, 2}, "the first of the same frames stays");
    expectFrames(finder.allDuplicates(same, {1}), {0, 2}, "a protected frame stays instead");
    expectFrames(finder.allDuplicates(same, {0, 1, 2}), {}, "all protected");
    bool thrown = false;
    try {
        (void)finder.duplicatesOf(same, {3}, {});
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    expect(thrown, "a reference that does not exist");
}

void testCommonPixelsAndMasks()
{
    ProjectDocument project = makeDocument();
    const Size size = project.originalFrameSize();
    const IndexedFrame differing = edit::differingPixels(project, {0, 1, 5, 2});
    expect(differing.size == size, "the size of the ROM frame");
    for (std::size_t index = 0; index < differing.pixels.size(); ++index) {
        const bool expected = index == 5 || index == 9 * 128 + 100;
        expect((differing.pixels[index] == 1) == expected && differing.pixels[index] <= 1,
               "differing pixel " + std::to_string(index));
    }
    const IndexedFrame none = edit::differingPixels(project, {4});
    expect(std::all_of(none.pixels.begin(), none.pixels.end(), [](uint8_t v) { return v == 0; }),
           "one frame has no differing pixel");
    const IndexedFrame identical = edit::differingPixels(project, {0, 2});
    expect(std::all_of(identical.pixels.begin(), identical.pixels.end(),
                       [](uint8_t v) { return v == 0; }),
           "identical frames have no differing pixel");

    expectFrames(edit::framesUsingComparisonMask(project, 0), {1, 5}, "frames with mask 0");
    expectFrames(edit::framesUsingComparisonMask(project, 1), {7}, "frames with mask 1");
    expectFrames(edit::framesUsingComparisonMask(project, kNoComparisonMaskId), {0, 2, 3, 4, 6},
                 "frames without a mask");

    // The differing pixels as mask 0: frames 0, 1, 2, 5 and 6 are then the same.
    SameFrameFinder finder;
    expectFrames(finder.sameFrames(project, 5, {0, false}), {}, "before the mask is replaced");
    const IndexedFrame before = project.comparisonMask(0);
    const std::unique_ptr<edit::EditCommand> command =
        edit::replaceComparisonMask(project, 0, differing);
    expect(command != nullptr, "a command for a mask that changes");
    expect(project.comparisonMask(0).pixels == before.pixels, "not applied yet");
    expect(command->target().kind == edit::EditKind::ComparisonMask && command->target().index == 0,
           "a comparison mask edit");
    command->apply(project);
    expect(project.comparisonMask(0).pixels == differing.pixels,
           "the mask is the differing pixels");
    // The finder notices the changed mask.
    expectFrames(finder.sameFrames(project, 5, {0, false}), {0, 1, 2, 6},
                 "after the mask is replaced");
    expect(edit::replaceComparisonMask(project, 0, differing) == nullptr,
           "no command when the mask is that already");
    command->revert(project);
    expect(project.comparisonMask(0).pixels == before.pixels, "undo restores the mask");
    expectFrames(finder.sameFrames(project, 5, {0, false}), {}, "and what is the same");

    // Any value but 0 masks and is stored as 1.
    IndexedFrame odd = document::makeIndexedFrame(size);
    odd.pixels[3] = 200;
    edit::replaceComparisonMask(project, 1, odd)->apply(project);
    expect(project.comparisonMask(1).pixels[3] == 1 && project.comparisonMask(1).pixels[4] == 0,
           "mask values are 0 and 1");

    bool thrown = false;
    try {
        (void)edit::replaceComparisonMask(project, 2, differing);
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    expect(thrown, "a mask that does not exist");
    thrown = false;
    try {
        (void)edit::replaceComparisonMask(project, 0, document::makeIndexedFrame(Size{192, 64}));
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    expect(thrown, "a mask of another size");
}

}  // namespace

void runSameFramesTests()
{
    testSameFrames();
    testCountsPerMask();
    testDuplicates();
    testCommonPixelsAndMasks();
}
