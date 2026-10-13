// Compares the editor's own renderer (app/FrameImageUtils) with libserum, frame by frame, on a
// real project. Nothing is written.
//
// Usage: colorizing_qt_v2_preview_diff <cROM|cRZ> <cRP> [first[-last]] [--step N] [--examples N]
//                                      [--pixels N] [--sequence N]
//
// --step N compares every Nth frame, --examples N lists N frames per class of difference, and
// --pixels N prints the first N differing pixels of every differing frame.
//
// --sequence N also plays the frames through libserum as the Runtime view's playback does, N
// frames per loaded sequence, and counts the frames whose picture then differs from the one
// libserum gives for the frame alone (it keeps state from frame to frame).
//
// For every frame in the range the plane with the ROM frame's size is compared, and in an
// ordinary project also the 256x64 plane of frames that have HD data. The 256x64 plane of a
// frame without HD is libserum's upscale of the 128x32 picture, which the editor does not
// reproduce; it is not compared.
//
// libserum does not report where it found sprites, so the sprite rectangles used to classify a
// differing pixel are the ones the editor's matcher finds. A pixel is counted in the first
// class that applies:
//   sprite        inside a sprite rectangle the editor found
//   sprite area   inside the search area of a sprite slot, outside every found rectangle
//   shadow        the editor or libserum shows one of the frame's dynamic shadow colors
//   dynamic       under the plane's dynamic mask (the 256x64 plane: or under the 128x32 one)
//   background    the frame's background shows there
//   static        none of the above
//
// Exit status: 0 when every compared plane is identical, 1 otherwise, 2 on a usage or load error.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "FrameImageUtils.h"
#include "document/ProjectDocument.h"
#include "legacy/LegacyProjectFileReader.h"
#include "render/ProjectGeometry.h"
#include "runtime/RuntimePreview.h"

namespace {

using colorizing::app::SpriteMatch;
using colorizing::document::ProjectDocument;
using colorizing::document::Rgb565Frame;
using colorizing::render::PlaneLayout;
using colorizing::render::Resolution;
using colorizing::runtime::PlaneRequest;
using colorizing::runtime::RuntimeFrame;
using colorizing::runtime::RuntimePlane;
using colorizing::runtime::RuntimePreview;

enum PixelClass : std::size_t {
    kSprite,
    kSpriteArea,
    kShadow,
    kDynamic,
    kBackground,
    kStatic,
    kClassCount,
};

constexpr std::array<const char*, kClassCount> kClassNames{
    "sprite", "sprite area", "shadow", "dynamic", "background", "static"};

using ClassCounts = std::array<std::size_t, kClassCount>;

struct PlaneTotals {
    std::string name;
    std::size_t compared = 0;
    std::size_t identical = 0;
    std::size_t withoutPicture = 0;
    std::size_t differingPixels = 0;
    ClassCounts pixels{};
    ClassCounts frames{};
    // Per class: the frames with the most differing pixels of it.
    std::array<std::vector<std::pair<std::size_t, std::size_t>>, kClassCount> examples;
};

double millisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

bool isShadowColor(const ProjectDocument& document, std::size_t frame, Resolution plane,
                   uint16_t color)
{
    const auto& dynamic = document.dynamicData(frame);
    if (!dynamic) {
        return false;
    }
    // libserum takes the shadows of a 128x32 project's dynamic content from either table.
    for (const bool hd : {false, true}) {
        if (hd != (plane == Resolution::Hd) &&
            document.geometry().layout() != PlaneLayout::Ordinary) {
            continue;
        }
        const auto& directions = hd ? dynamic->shadowDirectionsHd : dynamic->shadowDirectionsSd;
        const auto& colors = hd ? dynamic->shadowColorsHd : dynamic->shadowColorsSd;
        for (std::size_t set = 0; set < directions.size() && set < colors.size(); ++set) {
            if (directions[set] != 0 && colors[set] == color) {
                return true;
            }
        }
    }
    return false;
}

PixelClass classify(const ProjectDocument& document,
                    std::size_t frame,
                    Resolution plane,
                    const std::vector<SpriteMatch>& matches,
                    int x,
                    int y,
                    uint16_t editorColor,
                    uint16_t runtimeColor)
{
    const colorizing::render::Point rom =
        document.geometry().toRomFrame(plane, colorizing::render::Point{x, y});
    for (const SpriteMatch& match : matches) {
        if (rom.x >= match.frameX && rom.y >= match.frameY &&
            rom.x < match.frameX + match.width && rom.y < match.frameY + match.height) {
            return kSprite;
        }
    }
    for (const auto& reference : document.frameSpriteReferences(frame)) {
        if (reference.spriteId == 255) {
            break;
        }
        if (rom.x >= reference.minX && rom.y >= reference.minY && rom.x <= reference.maxX &&
            rom.y <= reference.maxY) {
            return kSpriteArea;
        }
    }
    if (isShadowColor(document, frame, plane, editorColor) ||
        isShadowColor(document, frame, plane, runtimeColor)) {
        return kShadow;
    }
    const auto& dynamic = document.dynamicData(frame);
    if (dynamic) {
        const auto& mask = plane == Resolution::Hd ? dynamic->maskHd : dynamic->maskSd;
        const std::size_t index = static_cast<std::size_t>(y) * mask.size.width + x;
        if (index < mask.pixels.size() && mask.pixels[index] != 255) {
            return kDynamic;
        }
        if (plane == Resolution::Hd && document.geometry().layout() == PlaneLayout::Ordinary) {
            const std::size_t sdIndex =
                static_cast<std::size_t>(rom.y) * dynamic->maskSd.size.width + rom.x;
            if (sdIndex < dynamic->maskSd.pixels.size() && dynamic->maskSd.pixels[sdIndex] != 255) {
                return kDynamic;
            }
        }
    }
    const auto& background = document.backgroundData(frame);
    if (background && background->backgroundId < document.backgroundCount()) {
        const auto& mask = plane == Resolution::Hd ? background->maskHd : background->maskSd;
        const auto& original = document.originalFrame(frame);
        const std::size_t index = static_cast<std::size_t>(y) * mask.size.width + x;
        const std::size_t romIndex = static_cast<std::size_t>(rom.y) * original.size.width + rom.x;
        if (index < mask.pixels.size() && mask.pixels[index] != 0 &&
            original.pixels[romIndex] == 0) {
            return kBackground;
        }
    }
    return kStatic;
}

void comparePlane(const ProjectDocument& document,
                  std::size_t frame,
                  Resolution plane,
                  const Rgb565Frame& editor,
                  const RuntimePlane* runtime,
                  const std::vector<SpriteMatch>& matches,
                  std::size_t pixelsToPrint,
                  PlaneTotals& totals)
{
    ++totals.compared;
    if (!runtime) {
        // libserum shows nothing: the editor is right only if it shows nothing either.
        const bool editorBlank = std::all_of(editor.pixels.begin(), editor.pixels.end(),
                                             [](uint16_t pixel) { return pixel == 0; });
        ++totals.withoutPicture;
        if (editorBlank) {
            ++totals.identical;
        }
        return;
    }
    if (runtime->image.size != editor.size) {
        std::cerr << "frame " << frame << ": plane sizes differ\n";
        std::exit(2);
    }
    ClassCounts counts{};
    std::size_t differing = 0;
    for (int y = 0; y < editor.size.height; ++y) {
        for (int x = 0; x < editor.size.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * editor.size.width + x;
            if (editor.pixels[index] == runtime->image.pixels[index]) {
                continue;
            }
            const PixelClass pixelClass = classify(document, frame, plane, matches, x, y,
                                                   editor.pixels[index],
                                                   runtime->image.pixels[index]);
            ++counts[pixelClass];
            if (differing++ < pixelsToPrint) {
                std::cout << "frame " << frame << ' ' << editor.size.width << 'x'
                          << editor.size.height << " (" << x << ',' << y << "): editor 0x"
                          << std::hex << editor.pixels[index] << ", libserum 0x"
                          << runtime->image.pixels[index] << std::dec << ", "
                          << kClassNames[pixelClass] << '\n';
            }
        }
    }
    if (differing == 0) {
        ++totals.identical;
        return;
    }
    totals.differingPixels += differing;
    for (std::size_t c = 0; c < kClassCount; ++c) {
        if (counts[c] == 0) {
            continue;
        }
        totals.pixels[c] += counts[c];
        ++totals.frames[c];
        totals.examples[c].emplace_back(counts[c], frame);
    }
}

void printTotals(PlaneTotals& totals, std::size_t exampleCount)
{
    if (totals.compared == 0) {
        return;
    }
    std::cout << totals.name << ": " << totals.compared << " frames compared, "
              << totals.identical << " identical, " << (totals.compared - totals.identical)
              << " different, " << totals.differingPixels << " pixels differ";
    if (totals.withoutPicture > 0) {
        std::cout << "; libserum shows no picture for " << totals.withoutPicture;
    }
    std::cout << '\n';
    for (std::size_t c = 0; c < kClassCount; ++c) {
        if (totals.pixels[c] == 0) {
            continue;
        }
        auto& examples = totals.examples[c];
        std::sort(examples.begin(), examples.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first != rhs.first ? lhs.first > rhs.first : lhs.second < rhs.second;
        });
        std::cout << "  " << kClassNames[c] << ": " << totals.pixels[c] << " pixels in "
                  << totals.frames[c] << " frames; e.g.";
        for (std::size_t i = 0; i < examples.size() && i < exampleCount; ++i) {
            std::cout << " frame " << examples[i].second << " (" << examples[i].first << ")";
        }
        std::cout << '\n';
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    std::vector<std::string> positional;
    std::size_t step = 1;
    std::size_t exampleCount = 5;
    std::size_t pixelsToPrint = 0;
    std::size_t sequenceFrames = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--step" || argument == "--examples" || argument == "--pixels" ||
             argument == "--sequence") &&
            i + 1 < argc) {
            const std::size_t value = std::strtoull(argv[++i], nullptr, 10);
            if (argument == "--pixels") {
                pixelsToPrint = value;
            } else if (argument == "--sequence") {
                sequenceFrames = value;
            } else {
                (argument == "--step" ? step : exampleCount) = std::max<std::size_t>(1, value);
            }
        } else {
            positional.push_back(argument);
        }
    }
    if (positional.size() < 2 || positional.size() > 3) {
        std::cerr << "usage: " << argv[0]
                  << " <cROM|cRZ> <cRP> [first[-last]] [--step N] [--examples N] [--pixels N]"
                     " [--sequence N]\n";
        return 2;
    }

    std::optional<ProjectDocument> loadedDocument;
    {
        const colorizing::legacy::LegacyProjectFileReader reader;
        const auto loaded = reader.load({positional[0], positional[1]});
        if (!loaded.ok) {
            std::cerr << "load failed: " << loaded.error.message << '\n';
            return 2;
        }
        loadedDocument = ProjectDocument::fromSnapshot(loaded.snapshot);
    }
    const ProjectDocument& document = *loadedDocument;
    if (document.frameCount() == 0) {
        std::cerr << "the project has no frames\n";
        return 2;
    }

    std::size_t first = 0;
    std::size_t last = document.frameCount() - 1;
    if (positional.size() == 3) {
        const std::string& range = positional[2];
        const std::size_t dash = range.find('-');
        first = std::strtoull(range.c_str(), nullptr, 10);
        last = dash == std::string::npos ? first
                                         : std::strtoull(range.c_str() + dash + 1, nullptr, 10);
        last = std::min(last, document.frameCount() - 1);
        if (first > last) {
            std::cerr << "the frame range is empty\n";
            return 2;
        }
    }

    const colorizing::render::ProjectGeometry& geometry = document.geometry();
    const bool ordinary = geometry.layout() == PlaneLayout::Ordinary;
    const Resolution native = geometry.nativePlane();
    const auto planeText = [&](Resolution plane) {
        const auto size = geometry.planeSize(plane);
        return std::to_string(size.width) + "x" + std::to_string(size.height);
    };
    PlaneTotals nativeTotals;
    nativeTotals.name = "native plane " + planeText(native);
    PlaneTotals hdTotals;
    hdTotals.name = "HD plane " + planeText(Resolution::Hd) + " (frames with HD)";

    RuntimePreview& preview = RuntimePreview::instance();
    colorizing::runtime::RuntimeCost cost;
    double editorMs = 0.0;
    std::size_t renders = 0;
    std::size_t notIdentified = 0;
    std::size_t otherFrameIdentified = 0;
    std::size_t skippedUpscaled = 0;
    std::size_t maxCromBytes = 0;
    double maxRenderMs = 0.0;
    for (std::size_t frame = first; frame <= last; frame += step) {
        const std::vector<SpriteMatch> matches =
            colorizing::app::matchFrameSprites(document, frame);
        const auto render = [&](PlaneRequest request) {
            RuntimeFrame result = preview.renderFrame(document, frame, request);
            if (!result.ok()) {
                std::cerr << "frame " << frame << ": libserum: " << result.error << '\n';
                std::exit(2);
            }
            cost.subsetMs += result.cost.subsetMs;
            cost.serializeMs += result.cost.serializeMs;
            cost.loadMs += result.cost.loadMs;
            cost.colorizeMs += result.cost.colorizeMs;
            cost.cromBytes += result.cost.cromBytes;
            maxCromBytes = std::max(maxCromBytes, result.cost.cromBytes);
            maxRenderMs = std::max(maxRenderMs, result.cost.totalMs());
            ++renders;
            return result;
        };
        const auto compose = [&](Resolution plane) {
            const auto start = std::chrono::steady_clock::now();
            Rgb565Frame composed =
                colorizing::app::composeResolvedFrame(document, frame, plane, true);
            editorMs += millisecondsSince(start);
            return composed;
        };

        const RuntimeFrame nativeResult =
            render(ordinary ? PlaneRequest::Rows32 : PlaneRequest::Rows64);
        if (!nativeResult.identified) {
            ++notIdentified;
        } else if (nativeResult.frameIndex != frame) {
            ++otherFrameIdentified;
        }
        comparePlane(document, frame, native, compose(native),
                     nativeResult.plane(ordinary ? PlaneRequest::Rows32 : PlaneRequest::Rows64),
                     matches, pixelsToPrint, nativeTotals);
        if (!ordinary) {
            continue;
        }
        if (!document.hasHdFrame(frame)) {
            ++skippedUpscaled;
            continue;
        }
        const RuntimeFrame hdResult = render(PlaneRequest::Rows64);
        comparePlane(document, frame, Resolution::Hd, compose(Resolution::Hd),
                     hdResult.plane(PlaneRequest::Rows64), matches, pixelsToPrint, hdTotals);
    }

    // Playback: the same frames in order, sequenceFrames of them per loaded colorization.
    std::size_t sequences = 0;
    std::size_t sequencePlayed = 0;
    std::size_t sequenceDifferent = 0;
    std::size_t sequenceOtherFrame = 0;
    std::vector<std::size_t> sequenceExamples;
    double sequenceLoadMs = 0.0;
    double sequenceColorizeMs = 0.0;
    const PlaneRequest nativeRows = ordinary ? PlaneRequest::Rows32 : PlaneRequest::Rows64;
    for (std::size_t chunk = first; sequenceFrames > 0 && chunk <= last;
         chunk += sequenceFrames * step) {
        std::vector<std::size_t> frames;
        for (std::size_t frame = chunk; frame <= last && frames.size() < sequenceFrames;
             frame += step) {
            frames.push_back(frame);
        }
        // The stills first: a still replaces whatever is loaded.
        std::vector<RuntimeFrame> stills;
        stills.reserve(frames.size());
        for (std::size_t frame : frames) {
            stills.push_back(preview.renderFrame(document, frame, nativeRows));
        }
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        if (!preview.openSequence(document, frames, nativeRows, &error)) {
            std::cerr << "sequence at frame " << chunk << ": libserum: " << error << '\n';
            return 2;
        }
        sequenceLoadMs += millisecondsSince(start);
        ++sequences;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const RuntimeFrame played = preview.renderSequenceFrame(frames[i]);
            sequenceColorizeMs += played.cost.colorizeMs;
            ++sequencePlayed;
            if (played.frameIndex != frames[i]) {
                ++sequenceOtherFrame;
            }
            const RuntimePlane* playedPlane = played.plane(nativeRows);
            const RuntimePlane* stillPlane = stills[i].plane(nativeRows);
            const bool same = (playedPlane == nullptr) == (stillPlane == nullptr) &&
                              (!playedPlane ||
                               playedPlane->image.pixels == stillPlane->image.pixels);
            if (!same) {
                ++sequenceDifferent;
                if (sequenceExamples.size() < exampleCount) {
                    sequenceExamples.push_back(frames[i]);
                }
            }
        }
        preview.close();
    }

    std::cout << document.romName() << ": " << document.frameCount() << " frames, "
              << document.spriteCount() << " sprites, " << document.backgroundCount()
              << " backgrounds, " << document.colorCount() << " colors; frames " << first << "-"
              << last << (step > 1 ? " step " + std::to_string(step) : std::string()) << '\n';
    printTotals(nativeTotals, exampleCount);
    printTotals(hdTotals, exampleCount);
    if (ordinary) {
        std::cout << "not compared: the upscaled 256x64 plane of " << skippedUpscaled
                  << " frames without HD\n";
    }
    if (notIdentified > 0 || otherFrameIdentified > 0) {
        std::cout << "libserum did not identify " << notIdentified
                  << " frames and identified another frame for " << otherFrameIdentified << '\n';
    }
    if (renders > 0) {
        const double count = static_cast<double>(renders);
        std::cout.setf(std::ios::fixed);
        std::cout.precision(3);
        std::cout << "runtime render, mean of " << renders << ": subset " << cost.subsetMs / count
                  << " ms, serialize " << cost.serializeMs / count << " ms, load "
                  << cost.loadMs / count << " ms, colorize " << cost.colorizeMs / count
                  << " ms, total " << cost.totalMs() / count << " ms (slowest " << maxRenderMs
                  << " ms); cROM "
                  << cost.cromBytes / renders << " bytes mean, " << maxCromBytes << " max\n";
        std::cout << "editor render, mean: " << editorMs / count << " ms\n";
    }
    if (sequences > 0) {
        std::cout << "played in " << sequences << " sequences of up to " << sequenceFrames
                  << " frames: " << sequencePlayed << " frames, " << sequenceDifferent
                  << " with another picture than alone, " << sequenceOtherFrame
                  << " identified as another frame";
        for (std::size_t frame : sequenceExamples) {
            std::cout << (frame == sequenceExamples.front() ? "; e.g. frame " : ", ") << frame;
        }
        std::cout << "; load " << sequenceLoadMs / static_cast<double>(sequences)
                  << " ms per sequence, colorize "
                  << sequenceColorizeMs / static_cast<double>(sequencePlayed)
                  << " ms per frame\n";
    }
    const bool identical =
        nativeTotals.identical == nativeTotals.compared && hdTotals.identical == hdTotals.compared;
    return identical ? 0 : 1;
}
