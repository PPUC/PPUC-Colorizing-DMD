#include "document/Upscale.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

using colorizing::document::ProjectDocument;
using colorizing::document::Rgb565Frame;
using colorizing::document::UpscaleFilter;
using colorizing::render::Resolution;
using colorizing::render::Size;
namespace document = colorizing::document;
namespace edit = colorizing::edit;
namespace fixtures = colorizing::runtime_fixtures;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: upscale: " << message << '\n';
        std::exit(1);
    }
}

uint16_t at(const Rgb565Frame& frame, int x, int y)
{
    return frame.pixels[static_cast<std::size_t>(y) * frame.size.width + x];
}

}  // namespace

void runUpscaleTests()
{
    const UpscaleFilter all[] = {UpscaleFilter::Nearest, UpscaleFilter::Bilinear,
                                 UpscaleFilter::Bicubic, UpscaleFilter::Lanczos4,
                                 UpscaleFilter::Scale2x};
    Rgb565Frame flat = document::makeRgb565Frame(Size{8, 4}, 0x5a6b);
    for (UpscaleFilter filter : all) {
        const Rgb565Frame up = document::upscale2x(flat, filter);
        expect(up.size.width == 16 && up.size.height == 8,
               document::upscaleFilterName(filter) + ": twice the size");
        expect(std::set<uint16_t>(up.pixels.begin(), up.pixels.end()) ==
                   std::set<uint16_t>{0x5a6b},
               document::upscaleFilterName(filter) + ": a flat picture stays flat");
    }

    // Nearest doubles every pixel.
    Rgb565Frame checker = document::makeRgb565Frame(Size{2, 2}, 0);
    checker.pixels[1] = 0xffff;
    checker.pixels[2] = 0xffff;
    const Rgb565Frame nearest = document::upscale2x(checker, UpscaleFilter::Nearest);
    expect(at(nearest, 1, 1) == 0 && at(nearest, 2, 0) == 0xffff && at(nearest, 3, 3) == 0,
           "nearest doubles");

    // Scale2x keeps the colors and rounds a diagonal step:
    //   . . .        the pixel at (1,1) has white above and to the left, so its top left
    //   . X X        quarter turns white.
    //   . X X
    Rgb565Frame step = document::makeRgb565Frame(Size{3, 3}, 0xffff);
    for (int y = 1; y < 3; ++y) {
        for (int x = 1; x < 3; ++x) {
            step.pixels[static_cast<std::size_t>(y) * 3 + x] = 0x001f;
        }
    }
    const Rgb565Frame scaled = document::upscale2x(step, UpscaleFilter::Scale2x);
    expect(at(scaled, 2, 2) == 0xffff && at(scaled, 3, 3) == 0x001f && at(scaled, 3, 2) == 0x001f,
           "scale2x rounds the corner");
    expect(std::set<uint16_t>(scaled.pixels.begin(), scaled.pixels.end()) ==
               std::set<uint16_t>({0xffff, 0x001f}),
           "scale2x makes no new colors");

    // Bilinear as OpenCV: centres aligned, edges repeated. A row black, white becomes
    // black, 3/4 black, 1/4 black, white.
    Rgb565Frame row = document::makeRgb565Frame(Size{2, 1}, 0);
    row.pixels[1] = 0xffff;
    const Rgb565Frame linear = document::upscale2x(row, UpscaleFilter::Bilinear);
    expect(at(linear, 0, 0) == 0 && at(linear, 3, 0) == 0xffff && at(linear, 0, 1) == 0,
           "bilinear keeps the ends");
    // 0.25 * 255 = 63.75 -> 64 -> 5 bits 8, 6 bits 16.
    expect(at(linear, 1, 0) == ((8 << 11) | (16 << 5) | 8) &&
               at(linear, 2, 0) == ((23 << 11) | (47 << 5) | 23),
           "bilinear blends a quarter and three quarters");

    // Create HD with a filter, and its undo.
    document::ProjectSnapshot snapshot =
        fixtures::makeProject(colorizing::render::PlaneLayout::Ordinary, 2);
    ProjectDocument project = ProjectDocument::fromSnapshot(std::move(snapshot));
    document::FrameHdParts parts;
    parts.filter = UpscaleFilter::Scale2x;
    std::unique_ptr<edit::EditCommand> create = edit::createFrameHd(project, 0, parts);
    expect(project.hasHdFrame(0) &&
               project.colorizedFrame(0, Resolution::Hd).pixels ==
                   document::upscale2x(project.colorizedFrame(0, Resolution::Sd),
                                       UpscaleFilter::Scale2x)
                       .pixels,
           "Create HD uses the filter");
    create->revert(project);
    expect(!project.hasHdFrame(0), "undo removes it");
}
