#include "legacy/LegacyByteSource.h"

#include <algorithm>
#include <cstring>

#include "legacy/LegacyProjectIo.h"
#include "platform/Utf8Path.h"

namespace colorizing::legacy {

namespace {

// Large enough that reading costs nothing next to parsing, small next to any project.
constexpr std::size_t kFileBufferBytes = std::size_t{1} << 20;
// How often progress is reported and the cancel flag is looked at.
constexpr std::size_t kReportIntervalBytes = std::size_t{1} << 20;

}  // namespace

LegacyByteSource::LegacyByteSource(const uint8_t* bytes,
                                   std::size_t size,
                                   LegacyIoProgress* progress,
                                   uint64_t progressBase)
    : m_window(bytes)
    , m_windowSize(bytes ? size : 0)
    , m_size(bytes ? size : 0)
    , m_open(bytes != nullptr || size == 0)
    , m_progress(progress)
    , m_progressBase(progressBase)
{
}

LegacyByteSource::LegacyByteSource(const std::string& utf8Path,
                                   LegacyIoProgress* progress,
                                   uint64_t progressBase)
    : m_progress(progress)
    , m_progressBase(progressBase)
{
    m_file.open(platform::pathFromUtf8(utf8Path), std::ios::binary);
    if (!m_file) {
        return;
    }
    m_file.seekg(0, std::ios::end);
    const std::streamoff end = m_file.tellg();
    m_file.seekg(0, std::ios::beg);
    if (end < 0 || !m_file) {
        return;
    }
    m_size = static_cast<std::size_t>(end);
    m_buffer.resize(std::min(kFileBufferBytes, std::max<std::size_t>(m_size, 1)));
    m_window = m_buffer.data();
    m_open = true;
}

void LegacyByteSource::reportProgress()
{
    if (!m_progress) {
        m_nextReport = m_size;
        return;
    }
    m_progress->done.store(m_progressBase + offset());
    if (m_progress->cancel.load()) {
        m_cancelled = true;
    }
    m_nextReport = offset() + kReportIntervalBytes;
}

bool LegacyByteSource::fill(std::size_t count)
{
    if (m_cancelled) {
        return false;
    }
    if (offset() >= m_nextReport) {
        reportProgress();
        if (m_cancelled) {
            return false;
        }
    }
    if (available() >= count || !m_file.is_open()) {
        return available() >= count;
    }
    // Keep what is left of the window, then read behind it.
    const std::size_t keep = available();
    std::memmove(m_buffer.data(), m_buffer.data() + m_position, keep);
    m_windowOffset += m_position;
    m_position = 0;
    const std::size_t want =
        std::min(m_buffer.size() - keep, m_size - (m_windowOffset + keep));
    if (want > 0) {
        m_file.read(reinterpret_cast<char*>(m_buffer.data() + keep),
                    static_cast<std::streamsize>(want));
        const auto got = static_cast<std::size_t>(std::max<std::streamsize>(m_file.gcount(), 0));
        if (got != want) {
            m_readFailed = true;
            m_windowSize = keep + got;
            return available() >= count;
        }
    }
    m_windowSize = keep + want;
    return available() >= count;
}

const uint8_t* LegacyByteSource::peek(std::size_t count, std::size_t& availableBytes)
{
    const std::size_t wanted = std::min({count, remaining(), m_buffer.empty() ? count
                                                                             : m_buffer.size()});
    fill(wanted);
    availableBytes = std::min(count, available());
    return m_window + m_position;
}

bool LegacyByteSource::skip(std::size_t count)
{
    if (m_cancelled || count > remaining()) {
        return false;
    }
    while (count > 0) {
        if (available() == 0 && !fill(1)) {
            return false;
        }
        const std::size_t step = std::min(count, available());
        m_position += step;
        count -= step;
    }
    return fill(0);
}

bool LegacyByteSource::readBytes(uint8_t* out, std::size_t count)
{
    if (m_cancelled || count > remaining()) {
        return false;
    }
    while (count > 0) {
        if (available() == 0 && !fill(1)) {
            return false;
        }
        const std::size_t step = std::min(count, available());
        std::memcpy(out, m_window + m_position, step);
        m_position += step;
        out += step;
        count -= step;
    }
    return fill(0);
}

bool LegacyByteSource::readU16(uint16_t& out)
{
    if (available() < 2 && !fill(2)) {
        return false;
    }
    const uint8_t* bytes = m_window + m_position;
    out = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    m_position += 2;
    return true;
}

bool LegacyByteSource::readU32(uint32_t& out)
{
    if (available() < 4 && !fill(4)) {
        return false;
    }
    const uint8_t* bytes = m_window + m_position;
    out = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
          (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    m_position += 4;
    return true;
}

bool LegacyByteSource::readU16s(uint16_t* out, std::size_t count)
{
    if (m_cancelled || count > remaining() / 2) {
        return false;
    }
    while (count > 0) {
        if (available() < 2 && !fill(2)) {
            return false;
        }
        const std::size_t step = std::min(count, available() / 2);
        const uint8_t* bytes = m_window + m_position;
        for (std::size_t i = 0; i < step; ++i) {
            out[i] = static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
        }
        m_position += 2 * step;
        out += step;
        count -= step;
    }
    return fill(0);
}

bool LegacyByteSource::readU32s(uint32_t* out, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (!readU32(out[i])) {
            return false;
        }
    }
    return fill(0);
}

}  // namespace colorizing::legacy
