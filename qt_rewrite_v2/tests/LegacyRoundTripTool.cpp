// Loads a real .cROM/.cRP project into a ProjectDocument, serializes it again the way Save does
// (ProjectDocument::toSnapshot(), then writeLegacyCrom() / writeLegacyCrp()) and reports where
// the result differs from the files on disk. The first argument may be a .cRZ archive; the
// comparison is then against the cROM stored in it. Nothing is written: the serialized bytes
// are compared with the files as they are produced, so the tool holds the project and small
// buffers only (plus, for a .cRZ, the cROM libserum unpacks). Usage:
// colorizing_qt_v2_roundtrip <cROM|cRZ> <cRP>
//
// The 96x32 data of a project with 192x64 original frames is discarded on load, so its
// extra-plane blocks are expected to differ from a file that had such data. Those blocks are
// reported as expected and do not count; every other block and the cRP must be identical.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "legacy/LegacyCrzArchive.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectSerializer.h"

namespace {

using colorizing::legacy::LegacySectionOffsets;

// The bytes the serializer writes, compared with a reference as they arrive. Differences are
// counted per section of `sections`, which the serializer fills before it writes a section.
class ComparingBuffer final : public std::streambuf {
public:
    ComparingBuffer(const LegacySectionOffsets& sections, const uint8_t* reference,
                    std::size_t referenceSize)
        : m_sections(sections)
        , m_memory(reference)
        , m_referenceSize(referenceSize)
    {
    }

    ComparingBuffer(const LegacySectionOffsets& sections, const std::string& referencePath)
        : m_sections(sections)
        , m_file(referencePath, std::ios::binary)
    {
        m_file.seekg(0, std::ios::end);
        m_referenceSize = static_cast<std::size_t>(std::max<std::streamoff>(m_file.tellg(), 0));
        m_file.seekg(0, std::ios::beg);
    }

    struct Difference {
        std::size_t count = 0;
        std::size_t first = 0;
    };

    [[nodiscard]] std::size_t written() const { return m_offset; }
    [[nodiscard]] std::size_t referenceSize() const { return m_referenceSize; }
    // Section index -> difference.
    [[nodiscard]] const std::map<std::size_t, Difference>& differences() const
    {
        return m_differences;
    }

protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override
    {
        const auto size = static_cast<std::size_t>(count);
        const uint8_t* reference = referenceBytes(size);
        for (std::size_t i = 0; i < size; ++i) {
            const std::size_t offset = m_offset + i;
            const bool same = offset < m_referenceSize &&
                              reference[i] == static_cast<uint8_t>(data[i]);
            if (!same) {
                note(offset);
            }
        }
        m_offset += size;
        return count;
    }

    int_type overflow(int_type character) override
    {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        const char value = traits_type::to_char_type(character);
        xsputn(&value, 1);
        return character;
    }

private:
    // The reference bytes at m_offset, as many as there are up to `count`; the rest of the
    // returned buffer is not compared (the offsets are past the reference's end).
    const uint8_t* referenceBytes(std::size_t count)
    {
        if (m_memory) {
            m_chunk.assign(count, 0);
            if (m_offset < m_referenceSize) {
                const std::size_t available = std::min(count, m_referenceSize - m_offset);
                std::copy(m_memory + m_offset, m_memory + m_offset + available, m_chunk.begin());
            }
            return m_chunk.data();
        }
        m_chunk.assign(count, 0);
        m_file.read(reinterpret_cast<char*>(m_chunk.data()), static_cast<std::streamsize>(count));
        m_file.clear();
        return m_chunk.data();
    }

    void note(std::size_t offset)
    {
        const auto after = std::upper_bound(
            m_sections.begin(), m_sections.end(), offset,
            [](std::size_t value, const auto& section) { return value < section.second; });
        const std::size_t section =
            after == m_sections.begin() ? 0
                                        : static_cast<std::size_t>(after - m_sections.begin()) - 1;
        Difference& difference = m_differences[section];
        if (difference.count++ == 0) {
            difference.first = offset;
        }
    }

    const LegacySectionOffsets& m_sections;
    const uint8_t* m_memory = nullptr;
    std::ifstream m_file;
    std::size_t m_referenceSize = 0;
    std::size_t m_offset = 0;
    std::vector<uint8_t> m_chunk;
    std::map<std::size_t, Difference> m_differences;
};

bool report(const char* label,
            const ComparingBuffer& buffer,
            const LegacySectionOffsets& sections,
            bool extraPlaneDiscarded)
{
    std::cout << label << ": " << buffer.referenceSize() << " bytes on disk, " << buffer.written()
              << " bytes serialized\n";
    bool identical = buffer.referenceSize() == buffer.written();
    bool expectedDifference = false;
    for (const auto& [index, difference] : buffer.differences()) {
        const bool known = index + 1 < sections.size();
        const std::string name = index < sections.size() ? sections[index].first : "?";
        const std::size_t length =
            known ? sections[index + 1].second - sections[index].second : 0;
        const bool expected =
            extraPlaneDiscarded && colorizing::legacy::isLegacyExtraPlaneSection(name);
        if (expected) {
            expectedDifference = true;
        } else {
            identical = false;
        }
        std::cout << "  " << name << ": " << difference.count << " of " << length
                  << " bytes differ, first at offset " << difference.first
                  << (expected ? " (expected: 96x32 data is discarded)" : "") << '\n';
    }
    std::cout << (!identical ? "  DIFFERENT\n"
                             : expectedDifference
                                   ? "  identical except the discarded 96x32 extra plane\n"
                                   : "  identical\n");
    return identical;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <cROM|cRZ> <cRP>\n";
        return 2;
    }

    std::optional<colorizing::document::ProjectDocument> document;
    {
        const colorizing::legacy::LegacyProjectFileReader reader;
        colorizing::legacy::LegacyLoadResult loaded = reader.load({argv[1], argv[2]});
        if (!loaded.ok) {
            std::cerr << "load failed: " << loaded.error.message << '\n';
            return 2;
        }
        try {
            document = colorizing::document::ProjectDocument::fromSnapshot(
                std::move(loaded.snapshot));
        } catch (const std::exception& ex) {
            std::cerr << "load failed: " << ex.what() << '\n';
            return 2;
        }
    }
    // What Save writes: a snapshot that shares the document's blocks.
    const colorizing::document::ProjectSnapshot snapshot = document->toSnapshot();
    const bool discarded = snapshot.geometry.discardsExtraPlane();
    std::string error;

    bool cromSame = false;
    {
        colorizing::legacy::CrzContent crz;
        LegacySectionOffsets sections;
        std::optional<ComparingBuffer> buffer;
        if (colorizing::legacy::hasCrzExtension(argv[1])) {
            if (!colorizing::legacy::readCrzArchive(argv[1], crz, error)) {
                std::cerr << "load failed: " << error << '\n';
                return 2;
            }
            buffer.emplace(sections, crz.data(), crz.size());
        } else {
            buffer.emplace(sections, std::string(argv[1]));
        }
        std::ostream stream(&*buffer);
        if (!colorizing::legacy::writeLegacyCrom(stream, snapshot, error, &sections)) {
            std::cerr << "serialize failed: " << error << '\n';
            return 2;
        }
        cromSame = report("cROM", *buffer, sections, discarded);
    }

    LegacySectionOffsets sections;
    ComparingBuffer crpBuffer(sections, std::string(argv[2]));
    std::ostream crpStream(&crpBuffer);
    if (!colorizing::legacy::writeLegacyCrp(crpStream, snapshot, error, &sections)) {
        std::cerr << "serialize failed: " << error << '\n';
        return 2;
    }
    const bool crpSame = report("cRP", crpBuffer, sections, false);
    return cromSame && crpSame ? 0 : 1;
}
