#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::importing {

// A frame dump is a text file. Each frame is a line with its time in milliseconds as a
// hexadecimal number ("0x" and up to 8 digits), followed by one line per row with one
// hexadecimal digit per pixel, the pixel's shade. Blank lines may stand between frames.
// Lines end with LF or CRLF.
//
// The frames of a dump have one size, that of its first frame: 128x32, 192x64 or 256x64 as
// they are, or 128x16, which is stored in the middle of a 128x32 frame with 8 blank rows
// above and below.

struct DumpError {
    std::string message;
    // 1-based; 0 when the error is not about a line.
    std::size_t line = 0;

    // "line 12: ..."
    [[nodiscard]] std::string text() const;
};

struct DumpFrame {
    // In the stored size, padded if the dump has 128x16 frames.
    document::IndexedFrame pixels;
    uint32_t timecode = 0;
    // Bit n is set if a pixel of the stored frame has shade n.
    uint16_t usedShades = 0;
    // The line of the frame's timecode.
    std::size_t line = 0;
};

// Reads a dump frame by frame. Memory use is one frame and a read buffer, whatever the size
// of the dump.
//
// The reader is strict: every line has to be a timecode, a row of the frame's width or
// blank, and every frame has to have the rows of the first one. Anything else is an error
// that names the line. The one exception is the last frame of a dump that ends inside it or
// is damaged (a row of another width, too few rows, a character that is no digit) with no
// frame after it, which is what an interrupted recording leaves: that frame is dropped and
// reported by truncatedFrameLine().
class FrameDumpReader {
public:
    enum class Status {
        Frame,
        End,
        Error,
    };

    // The stream has to be opened in binary mode and outlive the reader.
    explicit FrameDumpReader(std::istream& stream);

    [[nodiscard]] Status next(DumpFrame& frame);
    [[nodiscard]] const DumpError& error() const;

    // Known once the first frame was read.
    [[nodiscard]] bool hasFrameSize() const;
    // The size of the frames in the file.
    [[nodiscard]] render::Size dumpFrameSize() const;
    // The size of the frames returned: dumpFrameSize(), or 128x32 for a 128x16 dump.
    [[nodiscard]] render::Size storedFrameSize() const;
    [[nodiscard]] bool isPadded() const;

    // The timecode line of a last frame that was dropped because the dump ends inside it.
    [[nodiscard]] std::optional<std::size_t> truncatedFrameLine() const;
    [[nodiscard]] uint64_t bytesRead() const;

private:
    enum class LineKind {
        Blank,
        Timecode,
        Other,
    };

    // The next line without its line end and surrounding blanks; false at the end of the
    // stream or after an error.
    [[nodiscard]] bool readLine(std::string_view& line);
    [[nodiscard]] bool fillBuffer();
    [[nodiscard]] static LineKind classify(std::string_view line, uint32_t* timecode);
    // Decodes the digits of a row into target; the column of the first character that is
    // not a hexadecimal digit otherwise.
    [[nodiscard]] static std::optional<std::size_t> decodeRow(std::string_view line,
                                                              uint8_t* target,
                                                              uint16_t& usedShades);
    // Whether nothing but blank lines follows; reads to the end of the stream.
    [[nodiscard]] bool restIsBlank();
    // Whether no timecode line follows, i.e. no further frame; reads to the end of the stream.
    [[nodiscard]] bool noFrameFollows();
    [[nodiscard]] Status fail(std::size_t line, std::string message);
    [[nodiscard]] Status endTruncated(std::size_t frameLine);
    [[nodiscard]] Status readFirstFrame(DumpFrame& frame, std::size_t frameLine);
    [[nodiscard]] Status readFollowingFrame(DumpFrame& frame, std::size_t frameLine);
    void store(DumpFrame& frame, const std::vector<uint8_t>& rows) const;

    std::istream& m_stream;
    std::vector<char> m_buffer;
    std::size_t m_bufferBegin = 0;
    std::size_t m_bufferEnd = 0;
    bool m_streamEnded = false;
    uint64_t m_bytesRead = 0;
    std::size_t m_line = 0;
    // A timecode line that ended the first frame and starts the next.
    std::optional<uint32_t> m_pendingTimecode;
    bool m_finished = false;
    render::Size m_dumpFrameSize{0, 0};
    std::vector<uint8_t> m_rows;
    std::optional<std::size_t> m_truncatedFrameLine;
    DumpError m_error;
};

}  // namespace colorizing::importing
