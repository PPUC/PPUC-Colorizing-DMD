// Compares the editor's own renderer (app/FrameImageUtils) with libserum, frame by frame, on a
// real project. Nothing is written.
//
// Usage: colorizing_qt_v2_preview_diff <cROM|cRZ> <cRP> [first[-last]] [--step N] [--examples N]
//                                      [--pixels N] [--playback N [--no-whole]]
//
// --step N compares every Nth frame, --examples N lists N frames per class of difference, and
// --pixels N prints the first N differing pixels of every differing frame.
//
// --playback N does something else instead: it plays the frames of the range in order through
// the runtime preview as the Runtime view's playback does, in windows of at most N frames
// (RuntimePreview::openSequence(), the editor uses 48), and through libserum with the whole
// project loaded as one cROM, and compares every step: the frame identified, whether libserum
// reported the picture as unchanged or skipped the frame, and the pictures. It also compares
// what libserum says it identified with the search rule of runtime::FrameIdentification. It
// prints what the windows cost. The exit status is 0 if nothing differs. --no-whole leaves the
// whole project out (it needs the project's cROM in memory twice) and reports the windows only.
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
//   dynamic, upscale rounding
//                 256x64 plane of a 128x32 project, in or beside a dynamic zone: libserum
//                 shows the color of a neighboring 128x32 pixel or of the 256x64 frame below,
//                 which is what its Scale2x upscale of the zones does at edges; the editor
//                 doubles pixels
//   dynamic       in a dynamic zone of the plane the runtime takes them from, anything else
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
#include <sstream>
#include <string>
#include <vector>

#include "FrameImageUtils.h"
#include "document/ProjectDocument.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectSerializer.h"
#include "render/ProjectGeometry.h"
#include "runtime/FrameIdentification.h"
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
    kDynamicRounding,
    kBackground,
    kStatic,
    kClassCount,
};

constexpr std::array<const char*, kClassCount> kClassNames{
    "sprite", "sprite area", "shadow", "dynamic", "dynamic, upscale rounding", "background",
    "static"};

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
                    uint16_t runtimeColor,
                    const Rgb565Frame* scaledSource)
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
        // The zones the runtime uses: those of the plane with the ROM frame's size.
        const auto& mask = document.dynamicPlane() == Resolution::Hd ? dynamic->maskHd
                                                                      : dynamic->maskSd;
        const auto zoneAt = [&](int romX, int romY) {
            if (romX < 0 || romY < 0 || romX >= mask.size.width || romY >= mask.size.height) {
                return false;
            }
            return mask.pixels[static_cast<std::size_t>(romY) * mask.size.width + romX] != 255;
        };
        // The picture is the upscaled 128x32 one here. libserum's Scale2x gives a pixel the
        // color of its own 128x32 pixel or of one of that pixel's four neighbors; the editor
        // always takes the pixel's own.
        if (scaledSource) {
            constexpr std::array<std::array<int, 2>, 4> kNeighbors{{{0, -1}, {-1, 0}, {1, 0},
                                                                    {0, 1}}};
            for (const auto& offset : kNeighbors) {
                const int nx = rom.x + offset[0];
                const int ny = rom.y + offset[1];
                if (nx < 0 || ny < 0 || nx >= scaledSource->size.width ||
                    ny >= scaledSource->size.height || !(zoneAt(rom.x, rom.y) || zoneAt(nx, ny))) {
                    continue;
                }
                if (scaledSource->pixels[static_cast<std::size_t>(ny) *
                                             scaledSource->size.width +
                                         nx] == runtimeColor) {
                    return kDynamicRounding;
                }
            }
        }
        // Or libserum's upscale left the pixel to the 256x64 frame below the layer.
        if (scaledSource && zoneAt(rom.x, rom.y) && document.hasHdFrame(frame)) {
            const Rgb565Frame& below = document.colorizedFrame(frame, plane);
            if (below.pixels[static_cast<std::size_t>(y) * below.size.width + x] ==
                runtimeColor) {
                return kDynamicRounding;
            }
        }
        if (zoneAt(rom.x, rom.y)) {
            return kDynamic;
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
                  PlaneTotals& totals,
                  const Rgb565Frame* scaledSource = nullptr)
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
                                                   runtime->image.pixels[index],
                                                   scaledSource);
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

bool samePicture(const std::optional<RuntimePlane>& lhs, const std::optional<RuntimePlane>& rhs)
{
    return lhs.has_value() == rhs.has_value() &&
           (!lhs || (lhs->image.size == rhs->image.size && lhs->image.pixels == rhs->image.pixels));
}

std::string frameText(const std::optional<std::size_t>& frame)
{
    return frame ? std::to_string(*frame) : std::string("none");
}

// See --playback above. Returns the exit status.
int comparePlayback(const ProjectDocument& document,
                    const std::vector<std::size_t>& order,
                    std::size_t windowFrames,
                    bool withWholeProject,
                    std::size_t exampleCount)
{
    using colorizing::runtime::FrameIdentification;
    RuntimePreview& preview = RuntimePreview::instance();
    const PlaneRequest request = document.geometry().layout() == PlaneLayout::Ordinary
                                     ? PlaneRequest::Both
                                     : PlaneRequest::Rows64;
    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);

    auto start = std::chrono::steady_clock::now();
    const std::shared_ptr<const FrameIdentification> rule = preview.identification(document);
    std::cout << document.romName() << ": " << document.frameCount() << " frames; playing "
              << order.size() << " frames, " << order.front() << " to " << order.back()
              << "\nsearch rule of the whole project built in " << millisecondsSince(start)
              << " ms\n";

    // Through the whole project first; kept: what was identified and the pictures' hashes
    // would do, but the pictures themselves are small enough for a few thousand frames.
    struct WholeStep {
        std::optional<std::size_t> identified;
        bool unchanged = false;
        bool skipped = false;
        std::optional<RuntimePlane> rows32;
        std::optional<RuntimePlane> rows64;
    };
    std::vector<WholeStep> whole;
    std::size_t ruleMismatchesWhole = 0;
    if (withWholeProject) {
        std::vector<uint8_t> crom;
        {
            std::ostringstream stream;
            std::string error;
            if (!colorizing::legacy::writeLegacyCrom(stream, document.toSnapshot(), error)) {
                std::cerr << "the project cannot be written: " << error << '\n';
                return 2;
            }
            const std::string bytes = stream.str();
            crom.assign(bytes.begin(), bytes.end());
        }
        std::string error;
        start = std::chrono::steady_clock::now();
        if (!preview.openCrom(crom, document.geometry(), document.frameCount(), request, &error)) {
            std::cerr << "libserum does not load the whole project: " << error << '\n';
            return 2;
        }
        std::cout << "whole project: cROM of " << crom.size() << " bytes, loaded in "
                  << millisecondsSince(start) << " ms\n";
        std::size_t searchStart = 0;
        whole.reserve(order.size());
        for (std::size_t frame : order) {
            const std::optional<std::size_t> expected =
                rule->identify(document.originalFrame(frame), searchStart);
            RuntimeFrame result = preview.colorize(document.originalFrame(frame));
            if (!result.ok()) {
                std::cerr << "frame " << frame << ": libserum: " << result.error << '\n';
                return 2;
            }
            WholeStep step;
            step.unchanged = result.unchanged;
            step.skipped = result.skipped;
            // libserum does not say what it identified when it reports "unchanged".
            step.identified = result.unchanged ? expected : result.frameIndex;
            if (!result.unchanged && result.frameIndex != expected) {
                if (ruleMismatchesWhole++ < exampleCount) {
                    std::cout << "  whole project, frame " << frame << " after frame "
                              << searchStart << ": libserum identifies frame "
                              << frameText(result.frameIndex) << ", the search rule says "
                              << frameText(expected) << '\n';
                }
            }
            if (step.identified) {
                searchStart = *step.identified;
            }
            step.rows32 = std::move(result.rows32);
            step.rows64 = std::move(result.rows64);
            whole.push_back(std::move(step));
        }
        preview.close();
    }

    std::size_t windows = 0;
    std::size_t loadedFrames = 0;
    std::size_t maxLoadedFrames = 0;
    std::size_t cromBytes = 0;
    std::size_t maxCromBytes = 0;
    double loadMs = 0.0;
    double maxLoadMs = 0.0;
    double colorizeMs = 0.0;
    std::size_t otherFrame = 0;
    std::size_t notIdentified = 0;
    std::size_t unchanged = 0;
    std::size_t skipped = 0;
    std::size_t ruleMismatches = 0;
    std::size_t identificationMismatches = 0;
    std::size_t reportMismatches = 0;
    std::size_t pictureMismatches = 0;
    std::size_t rows32Mismatches = 0;
    std::size_t rows64Mismatches = 0;
    std::size_t differingPixels = 0;
    std::vector<std::string> otherFrameExamples;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const std::size_t frame = order[i];
        if (!preview.sequenceHasFrame(frame)) {
            std::vector<std::size_t> rest;
            for (std::size_t next = i; next < order.size() && rest.size() < windowFrames; ++next) {
                rest.push_back(order[next]);
            }
            std::string error;
            start = std::chrono::steady_clock::now();
            const bool opened =
                i == 0 ? preview.openSequence(document, rest, request, &error, windowFrames)
                       : preview.continueSequence(document, rest, request, &error, windowFrames);
            if (!opened) {
                std::cerr << "window at frame " << frame << ": libserum: " << error << '\n';
                return 2;
            }
            const double ms = millisecondsSince(start);
            loadMs += ms;
            maxLoadMs = std::max(maxLoadMs, ms);
            ++windows;
            loadedFrames += preview.sequenceLoadedFrameCount();
            maxLoadedFrames = std::max(maxLoadedFrames, preview.sequenceLoadedFrameCount());
            cromBytes += preview.sequenceCromBytes();
            maxCromBytes = std::max(maxCromBytes, preview.sequenceCromBytes());
        }
        const RuntimeFrame played = preview.renderSequenceFrame(frame);
        if (!played.ok()) {
            std::cerr << "frame " << frame << ": libserum: " << played.error << '\n';
            return 2;
        }
        colorizeMs += played.cost.colorizeMs;
        unchanged += played.unchanged ? 1 : 0;
        skipped += played.skipped ? 1 : 0;
        notIdentified += played.identified ? 0 : 1;
        ruleMismatches += played.expectedFrameIndex != played.frameIndex ? 1 : 0;
        if (played.frameIndex && *played.frameIndex != frame) {
            ++otherFrame;
            if (otherFrameExamples.size() < exampleCount) {
                otherFrameExamples.push_back(std::to_string(frame) + " as " +
                                             std::to_string(*played.frameIndex));
            }
        }
        if (!withWholeProject) {
            continue;
        }
        const WholeStep& expected = whole[i];
        if (played.frameIndex != expected.identified) {
            if (identificationMismatches++ < exampleCount) {
                std::cout << "  step " << i << ", frame " << frame << ": identified as frame "
                          << frameText(played.frameIndex) << ", with the whole project as frame "
                          << frameText(expected.identified) << '\n';
            }
        }
        reportMismatches +=
            played.unchanged != expected.unchanged || played.skipped != expected.skipped ? 1 : 0;
        if (!samePicture(played.rows32, expected.rows32) ||
            !samePicture(played.rows64, expected.rows64)) {
            rows32Mismatches += samePicture(played.rows32, expected.rows32) ? 0 : 1;
            rows64Mismatches += samePicture(played.rows64, expected.rows64) ? 0 : 1;
            for (const auto& planes : {std::make_pair(&played.rows32, &expected.rows32),
                                       std::make_pair(&played.rows64, &expected.rows64)}) {
                if (*planes.first && *planes.second &&
                    (*planes.first)->image.size == (*planes.second)->image.size) {
                    for (std::size_t p = 0; p < (*planes.first)->image.pixels.size(); ++p) {
                        differingPixels += (*planes.first)->image.pixels[p] !=
                                                   (*planes.second)->image.pixels[p]
                                               ? 1
                                               : 0;
                    }
                }
            }
            if (pictureMismatches++ < exampleCount) {
                std::cout << "  step " << i << ", frame " << frame
                          << ": another picture than with the whole project:";
                const auto describe = [](const char* name,
                                         const std::optional<RuntimePlane>& window,
                                         const std::optional<RuntimePlane>& wholeProject) {
                    if (window.has_value() != wholeProject.has_value()) {
                        std::cout << ' ' << name << (window ? " only in the window"
                                                            : " only with the whole project");
                        return;
                    }
                    if (!window) {
                        return;
                    }
                    std::size_t differing = 0;
                    std::size_t firstIndex = 0;
                    for (std::size_t p = 0; p < window->image.pixels.size(); ++p) {
                        if (window->image.pixels[p] != wholeProject->image.pixels[p]) {
                            if (differing++ == 0) {
                                firstIndex = p;
                            }
                        }
                    }
                    if (differing > 0) {
                        const std::size_t width =
                            static_cast<std::size_t>(window->image.size.width);
                        std::cout << ' ' << name << ' ' << differing << " pixels, first at ("
                                  << firstIndex % width << ',' << firstIndex / width << ") 0x"
                                  << std::hex << window->image.pixels[firstIndex] << " / 0x"
                                  << wholeProject->image.pixels[firstIndex] << std::dec;
                    }
                };
                describe("32 rows", played.rows32, expected.rows32);
                describe("64 rows", played.rows64, expected.rows64);
                std::cout << (played.unchanged ? " (reported as unchanged)" : "") << '\n';
            }
        }
    }
    preview.close();

    const double windowCount = static_cast<double>(std::max<std::size_t>(windows, 1));
    std::cout << "windows of up to " << windowFrames << " played frames: " << windows
              << " loaded; frames per window " << static_cast<double>(loadedFrames) / windowCount
              << " mean, " << maxLoadedFrames << " max; cROM per window "
              << static_cast<double>(cromBytes) / windowCount / 1e6 << " MB mean, "
              << static_cast<double>(maxCromBytes) / 1e6 << " MB max; load per window "
              << loadMs / windowCount << " ms mean, " << maxLoadMs << " ms max; colorize "
              << colorizeMs / static_cast<double>(order.size()) << " ms per frame\n";
    std::cout << "played: " << order.size() << " frames; " << otherFrame
              << " identified as another frame than the one fed";
    for (std::size_t i = 0; i < otherFrameExamples.size(); ++i) {
        std::cout << (i == 0 ? " (e.g. " : ", ") << otherFrameExamples[i]
                  << (i + 1 == otherFrameExamples.size() ? ")" : "");
    }
    std::cout << "; " << unchanged << " reported as unchanged, " << skipped << " skipped, "
              << notIdentified << " not identified\n";
    std::cout << "search rule against libserum: " << ruleMismatches
              << " differences in the windows";
    if (withWholeProject) {
        std::cout << ", " << ruleMismatchesWhole << " with the whole project";
    }
    std::cout << '\n';
    if (!withWholeProject) {
        std::cout << "not compared with the whole project (--no-whole)\n";
        return ruleMismatches == 0 ? 0 : 1;
    }
    std::cout << "windows against the whole project: " << identificationMismatches
              << " steps identified differently, " << reportMismatches
              << " reported differently (unchanged, skipped), " << pictureMismatches
              << " with another picture (32-row plane " << rows32Mismatches << ", 64-row plane "
              << rows64Mismatches << "; " << differingPixels << " pixels in all)\n";
    return identificationMismatches + reportMismatches + pictureMismatches + ruleMismatches +
                       ruleMismatchesWhole ==
                   0
               ? 0
               : 1;
}

}  // namespace

int main(int argc, char* argv[])
{
    std::vector<std::string> positional;
    std::size_t step = 1;
    std::size_t exampleCount = 5;
    std::size_t pixelsToPrint = 0;
    std::size_t playbackWindowFrames = 0;
    bool playbackWithWholeProject = true;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--no-whole") {
            playbackWithWholeProject = false;
        } else if ((argument == "--step" || argument == "--examples" || argument == "--pixels" ||
                    argument == "--playback") &&
                   i + 1 < argc) {
            const std::size_t value = std::strtoull(argv[++i], nullptr, 10);
            if (argument == "--pixels") {
                pixelsToPrint = value;
            } else if (argument == "--playback") {
                playbackWindowFrames = std::max<std::size_t>(1, value);
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
                     " [--playback N [--no-whole]]\n";
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

    if (playbackWindowFrames > 0) {
        std::vector<std::size_t> order;
        for (std::size_t frame = first; frame <= last; frame += step) {
            order.push_back(frame);
        }
        return comparePlayback(document, order, playbackWindowFrames, playbackWithWholeProject,
                               exampleCount);
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
        const Rgb565Frame nativePicture = compose(native);
        comparePlane(document, frame, native, nativePicture,
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
                     hdResult.plane(PlaneRequest::Rows64), matches, pixelsToPrint, hdTotals,
                     &nativePicture);
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
    const bool identical =
        nativeTotals.identical == nativeTotals.compared && hdTotals.identical == hdTotals.compared;
    return identical ? 0 : 1;
}
