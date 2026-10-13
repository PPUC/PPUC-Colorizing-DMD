#include "import/FrameDumpReader.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace colorizing::importing {

namespace {

constexpr std::size_t kBufferBytes = 1 << 16;
// The longest valid line is a row of 256 digits.
constexpr std::size_t kMaxLineBytes = 1024;
constexpr int kMaxRows = 64;
constexpr int kPaddedRows = 32;
constexpr int kPaddingRowsAbove = 8;
constexpr uint8_t kNoDigit = 0xff;

std::array<uint8_t, 256> buildDigitTable()
{
    std::array<uint8_t, 256> table;
    table.fill(kNoDigit);
    for (int digit = 0; digit < 10; ++digit) {
        table[static_cast<std::size_t>('0' + digit)] = static_cast<uint8_t>(digit);
    }
    for (int digit = 0; digit < 6; ++digit) {
        table[static_cast<std::size_t>('a' + digit)] = static_cast<uint8_t>(10 + digit);
        table[static_cast<std::size_t>('A' + digit)] = static_cast<uint8_t>(10 + digit);
    }
    return table;
}

const std::array<uint8_t, 256>& digitTable()
{
    static const std::array<uint8_t, 256> table = buildDigitTable();
    return table;
}

bool isBlankCharacter(char character)
{
    return character == ' ' || character == '\t' || character == '\r';
}

bool isSupportedSize(int width, int height)
{
    return (width == 128 && (height == 16 || height == 32)) ||
           (width == 192 && height == 64) || (width == 256 && height == 64);
}

std::string sizeText(int width, int height)
{
    return std::to_string(width) + "x" + std::to_string(height);
}

// What the character is called in a message; control characters are not printed.
std::string characterText(char character)
{
    const auto value = static_cast<unsigned char>(character);
    if (value >= 0x20 && value < 0x7f) {
        return std::string("'") + character + "'";
    }
    static const char digits[] = "0123456789abcdef";
    return std::string("byte 0x") + digits[value >> 4] + digits[value & 0xf];
}

}  // namespace

std::string DumpError::text() const
{
    return line == 0 ? message : "line " + std::to_string(line) + ": " + message;
}

FrameDumpReader::FrameDumpReader(std::istream& stream)
    : m_stream(stream)
    , m_buffer(kBufferBytes)
{
}

const DumpError& FrameDumpReader::error() const
{
    return m_error;
}

bool FrameDumpReader::hasFrameSize() const
{
    return m_dumpFrameSize.width > 0;
}

render::Size FrameDumpReader::dumpFrameSize() const
{
    return m_dumpFrameSize;
}

render::Size FrameDumpReader::storedFrameSize() const
{
    return isPadded() ? render::Size{m_dumpFrameSize.width, kPaddedRows} : m_dumpFrameSize;
}

bool FrameDumpReader::isPadded() const
{
    return m_dumpFrameSize.height == 16;
}

std::optional<std::size_t> FrameDumpReader::truncatedFrameLine() const
{
    return m_truncatedFrameLine;
}

uint64_t FrameDumpReader::bytesRead() const
{
    return m_bytesRead;
}

bool FrameDumpReader::fillBuffer()
{
    const std::size_t kept = m_bufferEnd - m_bufferBegin;
    if (kept > 0 && m_bufferBegin > 0) {
        std::memmove(m_buffer.data(), m_buffer.data() + m_bufferBegin, kept);
    }
    m_bufferBegin = 0;
    m_bufferEnd = kept;
    m_stream.read(m_buffer.data() + kept, static_cast<std::streamsize>(m_buffer.size() - kept));
    const auto count = static_cast<std::size_t>(m_stream.gcount());
    m_bufferEnd += count;
    m_bytesRead += count;
    if (count == 0) {
        m_streamEnded = true;
    }
    return count > 0;
}

bool FrameDumpReader::readLine(std::string_view& line)
{
    if (m_finished) {
        return false;
    }
    for (;;) {
        const char* begin = m_buffer.data() + m_bufferBegin;
        const std::size_t available = m_bufferEnd - m_bufferBegin;
        const auto* lineEnd = static_cast<const char*>(std::memchr(begin, '\n', available));
        std::size_t length = 0;
        if (lineEnd) {
            length = static_cast<std::size_t>(lineEnd - begin);
            m_bufferBegin += length + 1;
        } else if (available > kMaxLineBytes) {
            (void)fail(m_line + 1, "the line is longer than " + std::to_string(kMaxLineBytes) +
                                       " characters; this is not a frame dump");
            return false;
        } else if (!m_streamEnded) {
            (void)fillBuffer();
            continue;
        } else if (available == 0) {
            return false;
        } else {
            length = available;
            m_bufferBegin = m_bufferEnd;
        }
        if (length > kMaxLineBytes) {
            (void)fail(m_line + 1, "the line is longer than " + std::to_string(kMaxLineBytes) +
                                       " characters; this is not a frame dump");
            return false;
        }
        ++m_line;
        line = std::string_view(begin, length);
        if (m_line == 1 && line.substr(0, 3) == "\xEF\xBB\xBF") {
            line.remove_prefix(3);
        }
        while (!line.empty() && isBlankCharacter(line.back())) {
            line.remove_suffix(1);
        }
        while (!line.empty() && isBlankCharacter(line.front())) {
            line.remove_prefix(1);
        }
        return true;
    }
}

FrameDumpReader::LineKind FrameDumpReader::classify(std::string_view line, uint32_t* timecode)
{
    if (line.empty()) {
        return LineKind::Blank;
    }
    std::string_view digits;
    if (line.size() > 2 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
        digits = line.substr(2);
    } else if (line.size() > 1 && (line[0] == 'x' || line[0] == 'X')) {
        digits = line.substr(1);
    } else {
        return LineKind::Other;
    }
    if (digits.size() > 8) {
        return LineKind::Other;
    }
    uint32_t value = 0;
    for (char character : digits) {
        const uint8_t digit = digitTable()[static_cast<unsigned char>(character)];
        if (digit == kNoDigit) {
            return LineKind::Other;
        }
        value = (value << 4) | digit;
    }
    if (timecode) {
        *timecode = value;
    }
    return LineKind::Timecode;
}

std::optional<std::size_t> FrameDumpReader::decodeRow(std::string_view line,
                                                      uint8_t* target,
                                                      uint16_t& usedShades)
{
    const std::array<uint8_t, 256>& table = digitTable();
    for (std::size_t column = 0; column < line.size(); ++column) {
        const uint8_t digit = table[static_cast<unsigned char>(line[column])];
        if (digit == kNoDigit) {
            return column;
        }
        target[column] = digit;
        usedShades = static_cast<uint16_t>(usedShades | (1u << digit));
    }
    return std::nullopt;
}

bool FrameDumpReader::restIsBlank()
{
    std::string_view line;
    while (readLine(line)) {
        if (!line.empty()) {
            return false;
        }
    }
    return m_error.message.empty();
}

FrameDumpReader::Status FrameDumpReader::fail(std::size_t line, std::string message)
{
    // The first error stands: what follows it is a consequence.
    if (m_error.message.empty()) {
        m_error.line = line;
        m_error.message = std::move(message);
    }
    m_finished = true;
    return Status::Error;
}

FrameDumpReader::Status FrameDumpReader::endTruncated(std::size_t frameLine)
{
    if (!m_error.message.empty()) {
        return Status::Error;
    }
    m_truncatedFrameLine = frameLine;
    m_finished = true;
    return Status::End;
}

void FrameDumpReader::store(DumpFrame& frame, const std::vector<uint8_t>& rows) const
{
    frame.pixels.size = storedFrameSize();
    if (!isPadded()) {
        frame.pixels.pixels = rows;
        return;
    }
    const auto width = static_cast<std::size_t>(m_dumpFrameSize.width);
    frame.pixels.pixels.assign(width * kPaddedRows, 0);
    std::copy(rows.begin(), rows.end(),
              frame.pixels.pixels.begin() +
                  static_cast<std::ptrdiff_t>(width * kPaddingRowsAbove));
    frame.usedShades = static_cast<uint16_t>(frame.usedShades | 1u);
}

FrameDumpReader::Status FrameDumpReader::readFirstFrame(DumpFrame& frame, std::size_t frameLine)
{
    std::size_t width = 0;
    int height = 0;
    bool endedByBlankLine = false;
    m_rows.clear();
    std::string_view line;
    for (;;) {
        if (!readLine(line)) {
            if (!m_error.message.empty()) {
                return Status::Error;
            }
            break;
        }
        uint32_t timecode = 0;
        const LineKind kind = classify(line, &timecode);
        if (kind == LineKind::Blank) {
            endedByBlankLine = true;
            break;
        }
        if (kind == LineKind::Timecode) {
            m_pendingTimecode = timecode;
            break;
        }
        const std::size_t rowLine = m_line;
        if (width == 0) {
            if (line.size() != 128 && line.size() != 192 && line.size() != 256) {
                return fail(rowLine, "the first row has " + std::to_string(line.size()) +
                                         " characters; frames are 128, 192 or 256 pixels wide");
            }
            width = line.size();
        }
        if (height == kMaxRows) {
            return fail(rowLine, "the first frame has more than " + std::to_string(kMaxRows) +
                                     " rows");
        }
        m_rows.resize(m_rows.size() + width);
        uint8_t* target = m_rows.data() + m_rows.size() - width;
        const std::string_view digits = line.substr(0, std::min(line.size(), width));
        const std::optional<std::size_t> badColumn = decodeRow(digits, target, frame.usedShades);
        if (badColumn) {
            return fail(rowLine, "column " + std::to_string(*badColumn + 1) + ": " +
                                     characterText(line[*badColumn]) +
                                     " is not a hexadecimal digit");
        }
        if (line.size() != width) {
            const std::size_t length = line.size();
            if (length < width && restIsBlank()) {
                return endTruncated(frameLine);
            }
            return fail(rowLine, "the row has " + std::to_string(length) +
                                     " digits, the rows before it have " +
                                     std::to_string(width));
        }
        ++height;
    }

    if (!isSupportedSize(static_cast<int>(width), height)) {
        const std::size_t endLine = m_line;
        // A dump that ends inside its first frame holds no frame at all.
        if (!m_pendingTimecode && (!endedByBlankLine || restIsBlank())) {
            return endTruncated(frameLine);
        }
        if (height == 0) {
            return fail(endLine, "the frame that starts at line " + std::to_string(frameLine) +
                                     " has no rows");
        }
        return fail(frameLine, "the first frame is " + sizeText(static_cast<int>(width), height) +
                                   "; supported are 128x16, 128x32, 192x64 and 256x64");
    }
    m_dumpFrameSize = render::Size{static_cast<int>(width), height};
    store(frame, m_rows);
    return Status::Frame;
}

FrameDumpReader::Status FrameDumpReader::readFollowingFrame(DumpFrame& frame,
                                                            std::size_t frameLine)
{
    const auto width = static_cast<std::size_t>(m_dumpFrameSize.width);
    const int height = m_dumpFrameSize.height;
    m_rows.resize(width * static_cast<std::size_t>(height));
    std::string_view line;
    for (int row = 0; row < height; ++row) {
        if (!readLine(line)) {
            return endTruncated(frameLine);
        }
        const std::size_t rowLine = m_line;
        const LineKind kind = classify(line, nullptr);
        if (kind != LineKind::Other) {
            if (kind == LineKind::Blank && restIsBlank()) {
                return endTruncated(frameLine);
            }
            return fail(rowLine, "the frame that starts at line " + std::to_string(frameLine) +
                                     " has " + std::to_string(row) + " rows, the first frame of "
                                     "the dump has " + std::to_string(height) +
                                     "; frames of different sizes in one dump are not supported");
        }
        const std::string_view digits = line.substr(0, std::min(line.size(), width));
        const std::optional<std::size_t> badColumn =
            decodeRow(digits, m_rows.data() + static_cast<std::size_t>(row) * width,
                      frame.usedShades);
        if (badColumn) {
            return fail(rowLine, "column " + std::to_string(*badColumn + 1) + ": " +
                                     characterText(line[*badColumn]) +
                                     " is not a hexadecimal digit");
        }
        if (line.size() != width) {
            const std::size_t length = line.size();
            if (length < width && restIsBlank()) {
                return endTruncated(frameLine);
            }
            return fail(rowLine, "the row has " + std::to_string(length) + " digits, the frames "
                                     "of this dump are " + std::to_string(width) +
                                     " pixels wide; frames of different sizes in one dump are "
                                     "not supported");
        }
    }
    store(frame, m_rows);
    return Status::Frame;
}

FrameDumpReader::Status FrameDumpReader::next(DumpFrame& frame)
{
    if (m_finished) {
        return m_error.message.empty() ? Status::End : Status::Error;
    }
    frame.usedShades = 0;
    if (m_pendingTimecode) {
        frame.timecode = *m_pendingTimecode;
        m_pendingTimecode.reset();
    } else {
        std::string_view line;
        for (;;) {
            if (!readLine(line)) {
                m_finished = true;
                return m_error.message.empty() ? Status::End : Status::Error;
            }
            const LineKind kind = classify(line, &frame.timecode);
            if (kind == LineKind::Timecode) {
                break;
            }
            if (kind == LineKind::Blank) {
                continue;
            }
            if (hasFrameSize() &&
                line.size() == static_cast<std::size_t>(m_dumpFrameSize.width)) {
                return fail(m_line, "a row follows the " +
                                        std::to_string(m_dumpFrameSize.height) +
                                        " rows of a frame; frames of different sizes in one "
                                        "dump are not supported");
            }
            return fail(m_line, "expected the timecode line of a frame: 0x and up to 8 "
                                "hexadecimal digits");
        }
    }
    frame.line = m_line;
    return hasFrameSize() ? readFollowingFrame(frame, frame.line)
                          : readFirstFrame(frame, frame.line);
}

}  // namespace colorizing::importing
