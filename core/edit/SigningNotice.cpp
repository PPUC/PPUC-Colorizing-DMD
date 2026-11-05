#include "edit/SigningNotice.h"

#include <string_view>
#include <vector>

namespace colorizing::edit {

namespace {

// 5x7 capitals, a row per byte, bit 4 the leftmost pixel (libserum draws the words with the
// same font when a project has no notice).
constexpr uint8_t kFont[26][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
};
constexpr std::string_view kText = "COLORIZATION MODIFIED";
constexpr uint16_t kWhite = 0xffff;
constexpr uint16_t kBlack = 0x0000;

void drawText(document::Rgb565Frame& picture)
{
    const int width = picture.size.width;
    const int height = picture.size.height;
    const int textWidth = static_cast<int>(kText.size()) * 6 - 1;
    int scale = height >= 64 && textWidth * 2 <= width ? 2 : 1;
    std::vector<uint16_t> pixels(static_cast<std::size_t>(width) * height, kBlack);
    const int left = (width - textWidth * scale) / 2;
    const int top = (height - 7 * scale) / 2;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int tx = x - left;
            const int ty = y - top;
            if (tx < 0 || ty < 0 || tx >= textWidth * scale || ty >= 7 * scale) {
                continue;
            }
            const int column = (tx / scale) % 6;
            const char c = kText[static_cast<std::size_t>((tx / scale) / 6)];
            if (c >= 'A' && c <= 'Z' && column < 5 &&
                ((kFont[c - 'A'][ty / scale] >> (4 - column)) & 1) != 0) {
                pixels[static_cast<std::size_t>(y) * width + x] = kWhite;
            }
        }
    }
    picture.pixels.assign(pixels.begin(), pixels.end());
}

}  // namespace

document::FrameRecord makeSigningNoticeFrame(const document::ProjectDocument& document)
{
    const document::Size rom = document.originalFrameSize();
    document::IndexedFrame original;
    original.size = rom;
    original.pixels.assign(static_cast<std::size_t>(rom.width) * rom.height, uint8_t{0});
    document::FrameRecord frame = document.makeUncolorizedFrame(std::move(original), 0);
    drawText(frame.colorizedSd);
    if (frame.colorizedHd) {
        drawText(*frame.colorizedHd);
    }
    frame.signingNotice = true;
    return frame;
}

std::unique_ptr<StructureCommand> addSigningNotice(document::ProjectDocument& document)
{
    if (document.signingNoticeFrame()) {
        return nullptr;
    }
    std::vector<document::FrameRecord> frames;
    frames.push_back(makeSigningNoticeFrame(document));
    return addFrames(document, std::move(frames));
}

}  // namespace colorizing::edit
