// Loads a real .cROM/.cRP project into a ProjectDocument and checks that nothing of it is lost.
// Usage: colorizing_qt_v2_roundtrip <cROM|cRZ> <cRP|cRPX>
//
// The cRP (writeLegacyCrp(), deprecated, tests/legacy-writers) must be byte-identical with the
// file on disk; a .cRPX is compared with writeCrpx() (its sections are then 24 bytes early).
// The cROM of the editor is the one libserum 2.9 reads (writeLegacyCrom(): header revision 21,
// format version 1), so it cannot be byte-identical with a cROM of the Win32 editor: it is
// written into a temporary file, read back with the cRP, written again, and the two writes
// must be byte-identical. The bytes are compared as they are produced.

#include <algorithm>
#include <cstdint>
#include <filesystem>
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
#include "legacy/CrpxFile.h"
#include "legacy/LegacyCrzArchive.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectSerializer.h"
#include "legacy/LegacyCrpWriter.h"
#include "legacy/CrpxWriter.h"

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

bool report(const char* label, const ComparingBuffer& buffer, const LegacySectionOffsets& sections)
{
    std::cout << label << ": " << buffer.referenceSize() << " bytes on disk, " << buffer.written()
              << " bytes serialized\n";
    bool identical = buffer.referenceSize() == buffer.written();
    for (const auto& [index, difference] : buffer.differences()) {
        const bool known = index + 1 < sections.size();
        const std::string name = index < sections.size() ? sections[index].first : "?";
        const std::size_t length =
            known ? sections[index + 1].second - sections[index].second : 0;
        identical = false;
        std::cout << "  " << name << ": " << difference.count << " of " << length
                  << " bytes differ, first at offset " << difference.first << '\n';
    }
    std::cout << (identical ? "  identical\n" : "  DIFFERENT\n");
    return identical;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <cROM|cRZ> <cRP|cRPX>\n";
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
    std::string error;

    bool cromSame = false;
    {
        const std::filesystem::path written =
            std::filesystem::temp_directory_path() / "colorizing_roundtrip.cROM";
        {
            std::ofstream file(written, std::ios::binary | std::ios::trunc);
            if (!colorizing::legacy::writeLegacyCrom(file, snapshot, error) || !file.flush()) {
                std::cerr << "serialize failed: " << error << '\n';
                return 2;
            }
        }
        const colorizing::legacy::LegacyProjectFileReader reader;
        colorizing::legacy::LegacyLoadResult reloaded =
            reader.load({written.string(), argv[2]});
        if (!reloaded.ok) {
            std::filesystem::remove(written);
            std::cerr << "the written cROM does not load: " << reloaded.error.message << '\n';
            return 2;
        }
        const colorizing::document::ProjectSnapshot again =
            colorizing::document::ProjectDocument::fromSnapshot(std::move(reloaded.snapshot))
                .toSnapshot();
        LegacySectionOffsets sections;
        ComparingBuffer buffer(sections, written.string());
        std::ostream stream(&buffer);
        if (!colorizing::legacy::writeLegacyCrom(stream, again, error, &sections)) {
            std::filesystem::remove(written);
            std::cerr << "serialize failed: " << error << '\n';
            return 2;
        }
        cromSame = report("cROM (format version 1, written, read back, written again)", buffer,
                          sections);
        std::filesystem::remove(written);
    }

    LegacySectionOffsets sections;
    ComparingBuffer crpBuffer(sections, std::string(argv[2]));
    std::ostream crpStream(&crpBuffer);
    const bool crpx = colorizing::legacy::hasCrpxExtension(argv[2]);
    if (!(crpx ? colorizing::legacy::writeCrpx
               : colorizing::legacy::writeLegacyCrp)(crpStream, snapshot, error, &sections,
                                                     nullptr)) {
        std::cerr << "serialize failed: " << error << '\n';
        return 2;
    }
    const bool crpSame = report(crpx ? "cRPX" : "cRP", crpBuffer, sections);
    return cromSame && crpSame ? 0 : 1;
}
