// File -> Export Frames PNG...: the frame content picture for external tools, its colors
// importing back unchanged, and the dialog's choice of frames.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QImage>

#include "FrameImageUtils.h"
#include "ImageImport.h"
#include "dialogs/FramesPngExportDialog.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frames PNG export: " << message << '\n';
        std::exit(1);
    }
}

// Every RGB565 value, as the editor shows it, imports back as itself.
void testColorsRoundTrip()
{
    for (uint32_t value = 0; value <= 0xffff; ++value) {
        const auto color = static_cast<uint16_t>(value);
        if (app::rgb565FromColor(app::rgb565ToColor(color).rgb()) != color) {
            expect(false, "RGB565 value " + std::to_string(value) + " does not round-trip");
        }
    }
}

// One frame with a green background behind its first two pixels: the first unlit, the second
// lit (the frame's own color stays there).
document::ProjectDocument makeDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "frames_png_export";
    document::FrameBackgroundData backgroundData{
        0,
        document::makeIndexedFrame(render::kSdFrameSize, 0),
        document::makeIndexedFrame(render::kHdFrameSize, 0),
    };
    backgroundData.maskSd.pixels[0] = 1;
    backgroundData.maskSd.pixels[1] = 1;
    document::IndexedFrame original = document::makeIndexedFrame(render::kSdFrameSize, 0);
    original.pixels[1] = 3;
    original.pixels[2] = 3;
    document::Rgb565Frame colorized = document::makeRgb565Frame(render::kSdFrameSize, 0);
    colorized.pixels[1] = 0x1234;
    colorized.pixels[2] = 0x8421;
    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        original,
        colorized,
        document::makeRgb565Frame(render::kHdFrameSize, 0),
        0,
        255,
        std::nullopt,
        backgroundData,
    });
    snapshot.backgrounds.push_back(document::BackgroundRecord{
        document::makeRgb565Frame(render::kSdFrameSize, 0x07e0),
        document::makeRgb565Frame(render::kHdFrameSize, 0x07e0),
    });
    return document::ProjectDocument::fromSnapshot(snapshot);
}

void testContentImage()
{
    const document::ProjectDocument document = makeDocument();
    const QImage image = app::renderFrameContentImage(document, 0, render::Resolution::Sd);
    expect(image.width() == 128 && image.height() == 32, "one image pixel per DMD pixel");
    expect(image.hasAlphaChannel(), "the image has transparency");
    expect(qAlpha(image.pixel(0, 0)) == 0, "the background pixel is transparent");
    expect(qAlpha(image.pixel(1, 0)) == 255 &&
               app::rgb565FromColor(image.pixel(1, 0)) == 0x1234,
           "a lit pixel under the mask keeps the frame's own color");
    expect(app::rgb565FromColor(image.pixel(2, 0)) == 0x8421, "a frame pixel keeps its color");
    expect(qAlpha(image.pixel(3, 0)) == 255 && app::rgb565FromColor(image.pixel(3, 0)) == 0,
           "an unlit pixel without background is opaque black");
    // The picture as shown has the background there.
    const document::Rgb565Frame shown =
        app::composeResolvedFrame(document, 0, render::Resolution::Sd, true);
    expect(shown.pixels[0] == 0x07e0, "the background shows through as shown");
}

void testDialogFrames()
{
    app::FramesPngExportDialog dialog(
        {5, 3, 4, 3}, 10, 0, {{render::Resolution::Sd, QStringLiteral("SD")}},
        render::Resolution::Sd, QStringLiteral("/tmp/frames"));
    expect(dialog.frames() == std::vector<std::size_t>{3, 4, 5},
           "the selected frames, in order, once each");
    expect(dialog.canExport(), "the selection can be exported");
    dialog.setRange(7, 9);
    expect(dialog.frames() == std::vector<std::size_t>{7, 8, 9}, "a range of frames");
    dialog.setRange(9, 7);
    expect(dialog.frames().empty() && !dialog.canExport(), "an empty range exports nothing");
    dialog.setRange(2, 2);
    dialog.setFolder(QString());
    expect(!dialog.canExport(), "no export without a folder");
    expect(!dialog.contentOnly(), "the picture as shown is the default");
    dialog.setContentOnly(true);
    expect(dialog.contentOnly(), "the content only can be chosen");
}

}  // namespace

void runFramesPngExportWidgetTests()
{
    testColorsRoundTrip();
    testContentImage();
    testDialogFrames();
}
