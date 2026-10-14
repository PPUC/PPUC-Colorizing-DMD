#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace colorizing::exporting {

// Writes an animated GIF (GIF89a) of RGB565 pictures, each with a color table of its own, that
// loops forever. A picture with more than 256 colors keeps its 255 most frequent ones and
// shows each other pixel in the nearest of them.
class GifWriter {
public:
    GifWriter(std::ostream& stream, int width, int height);

    // pixels: width * height RGB565 values, row by row. delayCentiseconds: how long the
    // picture shows, in 1/100 s (GIF's unit); viewers show 0 and 1 as about 10.
    void addFrame(const std::vector<uint16_t>& pixels, int delayCentiseconds);
    // Writes the trailer. Returns whether every write succeeded.
    bool finish();

    [[nodiscard]] std::size_t frameCount() const;

private:
    void writeHeader();

    std::ostream& m_stream;
    int m_width;
    int m_height;
    std::size_t m_frames = 0;
    bool m_finished = false;
};

// RGB888 of an RGB565 color, as the editor shows it (5 and 6 bits widened by repeating their
// top bits).
void rgb565ToRgb888(uint16_t color, uint8_t& red, uint8_t& green, uint8_t& blue);

}  // namespace colorizing::exporting
