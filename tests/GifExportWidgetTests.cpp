// The animated GIF export: pictures of the composed frames, rotation steps, timing.

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageReader>

#include "FrameImageUtils.h"
#include "GifExport.h"
#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "export/GifWriter.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: GIF export: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

void runGifExportWidgetTests()
{
    if (!QImageReader::supportedImageFormats().contains("gif")) {
        std::cout << "GIF export tests skipped: Qt has no GIF reader here\n";
        return;
    }
    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary);
    snapshot.frames[2].durationMs = 5;
    snapshot.frames[0].durationMs = 100;
    // Frame 5 has a rotation that steps every 500 ms.
    snapshot.frames[5].durationMs = 1200;
    const document::ProjectDocument document =
        document::ProjectDocument::fromSnapshot(std::move(snapshot));
    expect(app::colorRotationSteps(document, 5, render::Resolution::Sd, 1200) ==
               std::vector<uint32_t>({0, 500, 1000}),
           "the rotation steps within the duration");

    app::GifExportOptions options;
    options.plane = render::Resolution::Sd;
    options.pixelScale = 5;
    std::ostringstream stream;
    legacy::LegacyIoProgress progress;
    const app::GifExportResult result =
        app::writeAnimatedGif(document, {2, 0, 5}, options, stream, &progress);
    expect(result.ok, "written: " + result.error);
    // Frame 2 (5 ms) is too short to show: its time goes to frame 0.
    expect(result.pictures == 4, "pictures: " + std::to_string(result.pictures));
    expect(progress.done.load() == 3 && progress.total.load() == 3, "progress counts frames");

    const std::string bytes = stream.str();
    QByteArray data(bytes.data(), static_cast<qsizetype>(bytes.size()));
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "gif");
    std::vector<QImage> images;
    std::vector<int> delays;
    while (reader.canRead()) {
        const QImage image = reader.read();
        if (image.isNull()) {
            break;
        }
        images.push_back(image.convertToFormat(QImage::Format_RGB32));
        delays.push_back(reader.nextImageDelay());
    }
    expect(images.size() == 4, "four pictures read back");
    expect(images[0].width() == 128 * 5 && images[0].height() == 32 * 5, "five pixels a dot");
    expect(delays == std::vector<int>({110, 500, 500, 200}),
           "the delays add up as the durations do");

    const document::Rgb565Frame frame0 =
        app::composeResolvedFrame(document, 0, render::Resolution::Sd, true);
    uint8_t r = 0, g = 0, b = 0;
    exporting::rgb565ToRgb888(frame0.pixels[1], r, g, b);
    expect(images[0].pixel(5 + 1, 1) == qRgb(r, g, b), "a dot has the frame's color");
    expect(images[0].pixel(5 + 4, 1) == qRgb(0, 0, 0), "and a black gap");
    expect(images[0].pixel(5, 0) == qRgb(0, 0, 0) && images[0].pixel(5 + 3, 3) == qRgb(0, 0, 0) &&
               images[0].pixel(5 + 1, 0) == qRgb(r, g, b),
           "the dot's corners are left out, as the Win32 editor rounds them");
    expect(images[1] != images[2], "the rotation step changes the picture");

    // With the originals: the original frame above, 15 pixels apart, in the editor's shades.
    app::GifExportOptions withOriginals = options;
    withOriginals.includeOriginal = true;
    std::ostringstream originalStream;
    expect(app::writeAnimatedGif(document, {0}, withOriginals, originalStream).ok,
           "written with the originals");
    const std::string originalBytes = originalStream.str();
    QByteArray originalData(originalBytes.data(), static_cast<qsizetype>(originalBytes.size()));
    QBuffer originalBuffer(&originalData);
    originalBuffer.open(QIODevice::ReadOnly);
    QImageReader originalReader(&originalBuffer, "gif");
    const QImage both = originalReader.read().convertToFormat(QImage::Format_RGB32);
    const render::Size originalSize = document.originalFrameSize();
    const int top = originalSize.height * 5 + app::kGifOriginalGap;
    expect(both.width() == std::max(128, originalSize.width) * 5 &&
               both.height() == top + 32 * 5,
           "the original takes its rows and the gap above the colorized picture");
    const QColor shade = app::originalIndexToColor(
        document.originalFrame(0).pixels[1], document.colorCount());
    const QColor drawn = QColor::fromRgb(both.pixel(5 + 1, 1));
    expect(std::abs(drawn.red() - shade.red()) < 9 && std::abs(drawn.green() - shade.green()) < 5 &&
               std::abs(drawn.blue() - shade.blue()) < 9,
           "the original's dot has the shade the editor shows");
    expect(both.pixel(5 + 1, top + 1) == qRgb(r, g, b), "the colorized dot sits below it");
    expect(both.pixel(5 + 1, top - 2) == qRgb(0, 0, 0), "with black between them");

    // Both planes: the 128x32 picture above the 256x64 one; a frame without the latter leaves
    // its part black, and without any frame that has it there is none.
    const auto firstPicture = [](const std::string& gif) {
        QByteArray data(gif.data(), static_cast<qsizetype>(gif.size()));
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "gif");
        std::vector<QImage> pictures;
        while (reader.canRead()) {
            const QImage image = reader.read();
            if (image.isNull()) {
                break;
            }
            pictures.push_back(image.convertToFormat(QImage::Format_RGB32));
        }
        return pictures;
    };
    app::GifExportOptions planes;
    planes.pixelScale = 2;
    planes.bothPlanes = true;
    std::ostringstream sdOnly;
    expect(app::writeAnimatedGif(document, {0}, planes, sdOnly).ok,
           "both planes written without any 256x64 picture");
    const std::vector<QImage> sdPictures = firstPicture(sdOnly.str());
    expect(!sdPictures.empty() && sdPictures[0].height() == 32 * 2,
           "without a 256x64 picture there is only the 128x32 one");
    document::ProjectSnapshot withHd =
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary);
    withHd.frames[0].durationMs = 100;
    withHd.frames[1].durationMs = 100;
    document::Rgb565Frame hdPicture;
    hdPicture.size = {256, 64};
    hdPicture.pixels = document::CompactArray<uint16_t>(256 * 64, 0xf800);
    withHd.frames[0].colorizedHd = hdPicture;
    const document::ProjectDocument hdDocument =
        document::ProjectDocument::fromSnapshot(std::move(withHd));
    std::ostringstream twoPlanes;
    expect(app::writeAnimatedGif(hdDocument, {0, 1}, planes, twoPlanes).ok,
           "both planes written");
    const std::vector<QImage> twoPictures = firstPicture(twoPlanes.str());
    const int hdTop = 32 * 2 + app::kGifOriginalGap;
    expect(twoPictures.size() == 2 && twoPictures[0].width() == 256 * 2 &&
               twoPictures[0].height() == hdTop + 64 * 2,
           "the 256x64 picture below the 128x32 one, the width of the wider");
    expect(twoPictures[0].pixel(0, hdTop) == qRgb(255, 0, 0),
           "the frame's 256x64 picture in its part");
    expect(twoPictures[1].pixel(0, hdTop) == qRgb(0, 0, 0),
           "a frame without one leaves the part black");

    document::ProjectSnapshot withoutHd =
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary);
    const document::ProjectDocument noHd =
        document::ProjectDocument::fromSnapshot(std::move(withoutHd));
    options.plane = render::Resolution::Hd;
    std::ostringstream unused;
    expect(!app::writeAnimatedGif(noHd, {0}, options, unused).ok,
           "a frame without the plane is refused");
}
