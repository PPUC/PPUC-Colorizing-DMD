#include "export/GifWriter.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace colorizing::exporting {

namespace {

void put16(std::ostream& stream, int value)
{
    stream.put(static_cast<char>(value & 0xff));
    stream.put(static_cast<char>((value >> 8) & 0xff));
}

int colorDistance(uint16_t a, uint16_t b)
{
    uint8_t ar = 0, ag = 0, ab = 0, br = 0, bg = 0, bb = 0;
    rgb565ToRgb888(a, ar, ag, ab);
    rgb565ToRgb888(b, br, bg, bb);
    const int dr = ar - br;
    const int dg = ag - bg;
    const int db = ab - bb;
    return dr * dr * 2 + dg * dg * 4 + db * db * 3;
}

// The color table of a picture and each pixel's index into it.
struct IndexedPicture {
    std::vector<uint16_t> table;
    std::vector<uint8_t> indices;
};

IndexedPicture indexPicture(const std::vector<uint16_t>& pixels)
{
    std::unordered_map<uint16_t, std::size_t> counts;
    for (uint16_t pixel : pixels) {
        ++counts[pixel];
    }
    std::vector<std::pair<uint16_t, std::size_t>> byCount(counts.begin(), counts.end());
    std::sort(byCount.begin(), byCount.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    IndexedPicture picture;
    const std::size_t kept = byCount.size() <= 256 ? byCount.size() : 255;
    std::unordered_map<uint16_t, uint8_t> index;
    for (std::size_t i = 0; i < kept; ++i) {
        picture.table.push_back(byCount[i].first);
        index.emplace(byCount[i].first, static_cast<uint8_t>(i));
    }
    for (std::size_t i = kept; i < byCount.size(); ++i) {
        int best = std::numeric_limits<int>::max();
        uint8_t bestIndex = 0;
        for (std::size_t t = 0; t < kept; ++t) {
            const int distance = colorDistance(byCount[i].first, picture.table[t]);
            if (distance < best) {
                best = distance;
                bestIndex = static_cast<uint8_t>(t);
            }
        }
        index.emplace(byCount[i].first, bestIndex);
    }
    picture.indices.reserve(pixels.size());
    for (uint16_t pixel : pixels) {
        picture.indices.push_back(index.at(pixel));
    }
    return picture;
}

// GIF's variable-length LZW, in sub-blocks of up to 255 bytes.
void writeLzw(std::ostream& stream, const std::vector<uint8_t>& indices, int minimumCodeSize)
{
    stream.put(static_cast<char>(minimumCodeSize));
    const int clearCode = 1 << minimumCodeSize;
    const int endCode = clearCode + 1;
    std::vector<uint8_t> block;
    uint32_t bitBuffer = 0;
    int bitCount = 0;
    int codeSize = minimumCodeSize + 1;
    const auto flushBlock = [&]() {
        if (!block.empty()) {
            stream.put(static_cast<char>(block.size()));
            stream.write(reinterpret_cast<const char*>(block.data()),
                         static_cast<std::streamsize>(block.size()));
            block.clear();
        }
    };
    const auto emit = [&](int code) {
        bitBuffer |= static_cast<uint32_t>(code) << bitCount;
        bitCount += codeSize;
        while (bitCount >= 8) {
            block.push_back(static_cast<uint8_t>(bitBuffer & 0xff));
            bitBuffer >>= 8;
            bitCount -= 8;
            if (block.size() == 255) {
                flushBlock();
            }
        }
    };

    // (prefix code << 8 | next index) -> code
    std::unordered_map<uint32_t, int> table;
    int nextCode = endCode + 1;
    emit(clearCode);
    if (!indices.empty()) {
        int prefix = indices[0];
        for (std::size_t i = 1; i < indices.size(); ++i) {
            const uint32_t key = (static_cast<uint32_t>(prefix) << 8) | indices[i];
            const auto found = table.find(key);
            if (found != table.end()) {
                prefix = found->second;
                continue;
            }
            emit(prefix);
            if (nextCode < 4096) {
                table.emplace(key, nextCode++);
                if (nextCode > (1 << codeSize) && codeSize < 12) {
                    ++codeSize;
                }
            } else {
                emit(clearCode);
                table.clear();
                nextCode = endCode + 1;
                codeSize = minimumCodeSize + 1;
            }
            prefix = indices[i];
        }
        emit(prefix);
        // The decoder adds an entry for the last code too, and may widen the code size
        // before it reads the end code.
        if (nextCode < 4096 && nextCode >= (1 << codeSize) && codeSize < 12) {
            ++codeSize;
        }
    }
    emit(endCode);
    if (bitCount > 0) {
        block.push_back(static_cast<uint8_t>(bitBuffer & 0xff));
    }
    flushBlock();
    stream.put(0);
}

}  // namespace

void rgb565ToRgb888(uint16_t color, uint8_t& red, uint8_t& green, uint8_t& blue)
{
    const int r = (color >> 11) & 0x1f;
    const int g = (color >> 5) & 0x3f;
    const int b = color & 0x1f;
    red = static_cast<uint8_t>((r << 3) | (r >> 2));
    green = static_cast<uint8_t>((g << 2) | (g >> 4));
    blue = static_cast<uint8_t>((b << 3) | (b >> 2));
}

GifWriter::GifWriter(std::ostream& stream, int width, int height)
    : m_stream(stream),
      m_width(width),
      m_height(height)
{
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535) {
        throw std::invalid_argument("a GIF picture has 1 to 65535 pixels per side");
    }
    writeHeader();
}

void GifWriter::writeHeader()
{
    m_stream.write("GIF89a", 6);
    put16(m_stream, m_width);
    put16(m_stream, m_height);
    // No global color table; every picture has its own.
    m_stream.put(0);
    m_stream.put(0);
    m_stream.put(0);
    // NETSCAPE2.0: loop forever.
    m_stream.put(static_cast<char>(0x21));
    m_stream.put(static_cast<char>(0xff));
    m_stream.put(11);
    m_stream.write("NETSCAPE2.0", 11);
    m_stream.put(3);
    m_stream.put(1);
    put16(m_stream, 0);
    m_stream.put(0);
}

void GifWriter::addFrame(const std::vector<uint16_t>& pixels, int delayCentiseconds)
{
    if (m_finished) {
        throw std::logic_error("the GIF is finished");
    }
    if (pixels.size() != static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height)) {
        throw std::invalid_argument("a picture has the size of the GIF");
    }
    const IndexedPicture picture = indexPicture(pixels);
    int tableBits = 1;
    while ((std::size_t{1} << tableBits) < picture.table.size()) {
        ++tableBits;
    }

    // Graphic control extension: the delay, no transparency, disposal "leave".
    m_stream.put(static_cast<char>(0x21));
    m_stream.put(static_cast<char>(0xf9));
    m_stream.put(4);
    m_stream.put(static_cast<char>(1 << 2));
    put16(m_stream, std::clamp(delayCentiseconds, 0, 65535));
    m_stream.put(0);
    m_stream.put(0);

    // Image descriptor with a local color table.
    m_stream.put(static_cast<char>(0x2c));
    put16(m_stream, 0);
    put16(m_stream, 0);
    put16(m_stream, m_width);
    put16(m_stream, m_height);
    m_stream.put(static_cast<char>(0x80 | (tableBits - 1)));
    for (std::size_t i = 0; i < (std::size_t{1} << tableBits); ++i) {
        uint8_t red = 0, green = 0, blue = 0;
        if (i < picture.table.size()) {
            rgb565ToRgb888(picture.table[i], red, green, blue);
        }
        m_stream.put(static_cast<char>(red));
        m_stream.put(static_cast<char>(green));
        m_stream.put(static_cast<char>(blue));
    }
    writeLzw(m_stream, picture.indices, std::max(2, tableBits));
    ++m_frames;
}

bool GifWriter::finish()
{
    if (!m_finished) {
        m_stream.put(static_cast<char>(0x3b));
        m_stream.flush();
        m_finished = true;
    }
    return static_cast<bool>(m_stream);
}

std::size_t GifWriter::frameCount() const
{
    return m_frames;
}

}  // namespace colorizing::exporting
