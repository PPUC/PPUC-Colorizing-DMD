#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "legacy/LegacyProjectIo.h"
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::legacy::detail {

// What the legacy writers share: the cROM writer of the core (LegacyProjectSerializer.cpp)
// and the deprecated cRP writer of the tests (tests/legacy-writers). Not part of the API.

inline constexpr std::size_t kRomNameBytes = 64;
inline constexpr std::size_t kFlushThresholdBytes = 1 << 20;

// Thrown by Writer when its progress asks to cancel; caught where the file is written.
struct WriteCancelled {
};

class Writer {
public:
    Writer(std::ostream& stream, LegacySectionOffsets* sections, LegacyIoProgress* progress)
        : m_stream(&stream)
        , m_sections(sections)
        , m_progress(progress)
    {
    }
    // Into memory only: what was written stays in buffer().
    Writer() = default;

    [[nodiscard]] std::vector<uint8_t>& buffer() { return m_buffer; }

    void section(const char* name)
    {
        if (m_sections) {
            m_sections->emplace_back(name, m_offset);
        }
    }

    void u8(uint8_t value)
    {
        m_buffer.push_back(value);
        advance(1);
    }

    void u16(uint16_t value)
    {
        m_buffer.push_back(static_cast<uint8_t>(value & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        advance(2);
    }

    void u32(uint32_t value)
    {
        m_buffer.push_back(static_cast<uint8_t>(value & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
        advance(4);
    }

    template <typename Values>
    void bytes(const Values& values)
    {
        m_buffer.insert(m_buffer.end(), values.begin(), values.end());
        advance(values.size());
    }

    template <typename Values>
    void u16s(const Values& values)
    {
        const std::size_t start = m_buffer.size();
        m_buffer.resize(start + 2 * values.size());
        uint8_t* out = m_buffer.data() + start;
        for (const uint16_t value : values) {
            *out++ = static_cast<uint8_t>(value & 0xff);
            *out++ = static_cast<uint8_t>((value >> 8) & 0xff);
        }
        advance(2 * values.size());
    }

    template <typename Values>
    void u32s(const Values& values)
    {
        for (const uint32_t value : values) {
            u32(value);
        }
    }

    void repeatU8(uint8_t value, std::size_t count)
    {
        m_buffer.insert(m_buffer.end(), count, value);
        advance(count);
    }

    void repeatU16(uint16_t value, std::size_t count)
    {
        const std::size_t start = m_buffer.size();
        m_buffer.resize(start + 2 * count);
        uint8_t* out = m_buffer.data() + start;
        for (std::size_t i = 0; i < count; ++i) {
            *out++ = static_cast<uint8_t>(value & 0xff);
            *out++ = static_cast<uint8_t>((value >> 8) & 0xff);
        }
        advance(2 * count);
    }

    // Writes the values, or `count` copies of `fallback` when they were never populated.
    template <typename Values>
    void bytesOr(const Values& values, std::size_t count, uint8_t fallback)
    {
        if (values.size() == count) {
            bytes(values);
        } else {
            repeatU8(fallback, count);
        }
    }

    template <typename Values>
    void u16sOr(const Values& values, std::size_t count, uint16_t fallback)
    {
        if (values.size() == count) {
            u16s(values);
        } else {
            repeatU16(fallback, count);
        }
    }

    void name(const std::string& value, std::size_t length)
    {
        for (std::size_t i = 0; i < length; ++i) {
            u8(i + 1 < length && i < value.size() ? static_cast<uint8_t>(value[i]) : uint8_t{0});
        }
    }

    [[nodiscard]] bool finish()
    {
        flush();
        m_stream->flush();
        return static_cast<bool>(*m_stream);
    }

private:
    void advance(std::size_t count)
    {
        m_offset += count;
        if (m_stream && m_buffer.size() >= kFlushThresholdBytes) {
            flush();
        }
    }

    void flush()
    {
        m_stream->write(reinterpret_cast<const char*>(m_buffer.data()),
                       static_cast<std::streamsize>(m_buffer.size()));
        if (m_progress) {
            m_progress->done.fetch_add(m_buffer.size());
            if (m_progress->cancel.load()) {
                m_buffer.clear();
                throw WriteCancelled{};
            }
        }
        m_buffer.clear();
    }

    std::ostream* m_stream = nullptr;
    LegacySectionOffsets* m_sections = nullptr;
    LegacyIoProgress* m_progress = nullptr;
    std::vector<uint8_t> m_buffer;
    std::size_t m_offset = 0;
};

}  // namespace colorizing::legacy::detail
