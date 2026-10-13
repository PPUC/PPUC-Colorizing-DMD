// Loads a real .cROM/.cRP project, serializes it again and reports where the result differs from
// the files on disk. The first argument may be a .cRZ archive; the comparison is then against
// the cROM stored in it. Nothing is written. Usage: colorizing_qt_v2_roundtrip <cROM|cRZ> <cRP>
//
// The 96x32 data of a project with 192x64 original frames is discarded on load, so its
// extra-plane blocks are expected to differ from a file that had such data. Those blocks are
// reported as expected and do not count; every other block and the cRP must be identical.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "legacy/LegacyCrzArchive.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectSerializer.h"

namespace {

using colorizing::legacy::LegacySectionOffsets;

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
}

bool report(const char* label,
            const std::vector<uint8_t>& original,
            const std::string& written,
            const LegacySectionOffsets& sections,
            bool extraPlaneDiscarded)
{
    std::cout << label << ": " << original.size() << " bytes on disk, " << written.size()
              << " bytes serialized\n";
    bool identical = original.size() == written.size();
    bool expectedDifference = false;
    for (std::size_t i = 0; i + 1 < sections.size(); ++i) {
        const std::size_t begin = sections[i].second;
        const std::size_t end = sections[i + 1].second;
        std::size_t differing = 0;
        std::size_t first = 0;
        for (std::size_t offset = begin; offset < end; ++offset) {
            const bool same = offset < original.size() &&
                              original[offset] == static_cast<uint8_t>(written[offset]);
            if (!same && differing++ == 0) {
                first = offset;
            }
        }
        if (differing > 0) {
            const bool expected = extraPlaneDiscarded &&
                                  colorizing::legacy::isLegacyExtraPlaneSection(sections[i].first);
            if (expected) {
                expectedDifference = true;
            } else {
                identical = false;
            }
            std::cout << "  " << sections[i].first << ": " << differing << " of " << (end - begin)
                      << " bytes differ, first at offset " << first
                      << (expected ? " (expected: 96x32 data is discarded)" : "") << '\n';
        }
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

    const colorizing::legacy::LegacyProjectFileReader reader;
    const auto loaded = reader.load({argv[1], argv[2]});
    if (!loaded.ok) {
        std::cerr << "load failed: " << loaded.error.message << '\n';
        return 2;
    }

    std::ostringstream crom;
    std::ostringstream crp;
    LegacySectionOffsets cromSections;
    LegacySectionOffsets crpSections;
    std::string error;
    if (!colorizing::legacy::writeLegacyCrom(crom, loaded.snapshot, error, &cromSections) ||
        !colorizing::legacy::writeLegacyCrp(crp, loaded.snapshot, error, &crpSections)) {
        std::cerr << "serialize failed: " << error << '\n';
        return 2;
    }

    std::vector<uint8_t> cromOnDisk;
    if (colorizing::legacy::hasCrzExtension(argv[1])) {
        if (!colorizing::legacy::readCrzArchive(argv[1], cromOnDisk, error)) {
            std::cerr << "load failed: " << error << '\n';
            return 2;
        }
    } else {
        cromOnDisk = readFile(argv[1]);
    }

    const bool discarded = loaded.snapshot.geometry.discardsExtraPlane();
    const bool cromSame = report("cROM", cromOnDisk, crom.str(), cromSections, discarded);
    const bool crpSame = report("cRP", readFile(argv[2]), crp.str(), crpSections, false);
    return cromSame && crpSame ? 0 : 1;
}
