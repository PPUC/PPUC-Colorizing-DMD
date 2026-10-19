// The GIF writer, read back with Qt's GIF reader.

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageReader>

#include "export/GifWriter.h"

namespace {

using colorizing::exporting::GifWriter;
using colorizing::exporting::rgb565ToRgb888;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: GIF writer: " << message << '\n';
        std::exit(1);
    }
}

std::vector<QImage> readBack(const std::string& bytes, std::vector<int>* delays = nullptr)
{
    QByteArray data(bytes.data(), static_cast<qsizetype>(bytes.size()));
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "gif");
    std::vector<QImage> images;
    while (reader.canRead()) {
        const QImage image = reader.read();
        if (image.isNull()) {
            break;
        }
        images.push_back(image.convertToFormat(QImage::Format_RGB32));
        if (delays) {
            delays->push_back(reader.nextImageDelay());
        }
    }
    return images;
}

bool samePixels(const QImage& image, const std::vector<uint16_t>& pixels, int width)
{
    for (int i = 0; i < static_cast<int>(pixels.size()); ++i) {
        uint8_t r = 0, g = 0, b = 0;
        rgb565ToRgb888(pixels[static_cast<std::size_t>(i)], r, g, b);
        if (image.pixel(i % width, i / width) != qRgb(r, g, b)) {
            return false;
        }
    }
    return true;
}

}  // namespace

void runGifWriterWidgetTests()
{
    if (!QImageReader::supportedImageFormats().contains("gif")) {
        std::cout << "GIF writer tests skipped: Qt has no GIF reader here\n";
        return;
    }
    // A few colors; a noisy picture that fills the code table several times; a picture with
    // more than 256 colors.
    const int width = 300;
    const int height = 200;
    std::vector<uint16_t> few(static_cast<std::size_t>(width * height));
    std::vector<uint16_t> noisy(few.size());
    std::vector<uint16_t> many(few.size());
    uint32_t seed = 12345;
    for (std::size_t i = 0; i < few.size(); ++i) {
        few[i] = (i / 37) % 3 == 0 ? 0xf800 : ((i / 11) % 2 ? 0x07e0 : 0x001f);
        seed = seed * 1103515245u + 12345u;
        noisy[i] = static_cast<uint16_t>(((seed >> 16) % 7) * 0x0841);
        many[i] = static_cast<uint16_t>(i % 1000 * 61);
    }
    std::ostringstream stream;
    GifWriter writer(stream, width, height);
    writer.addFrame(few, 5);
    writer.addFrame(noisy, 12);
    writer.addFrame(many, 100);
    expect(writer.finish() && writer.frameCount() == 3, "written");

    std::vector<int> delays;
    const std::vector<QImage> images = readBack(stream.str(), &delays);
    expect(images.size() == 3, "three pictures read back");
    expect(images[0].width() == width && images[0].height() == height, "the size");
    expect(samePixels(images[0], few, width), "a picture with few colors is exact");
    expect(samePixels(images[1], noisy, width), "a long LZW stream with table resets is exact");
    expect(delays[0] == 50 && delays[1] == 120, "the delays in milliseconds");
    // The 255 most frequent colors stay exact, the others become a near one.
    int exact = 0;
    for (int i = 0; i < width * height; ++i) {
        uint8_t r = 0, g = 0, b = 0;
        rgb565ToRgb888(many[static_cast<std::size_t>(i)], r, g, b);
        exact += images[2].pixel(i % width, i / width) == qRgb(r, g, b) ? 1 : 0;
    }
    expect(exact > width * height / 5, "the frequent colors of a colorful picture are kept");
}
