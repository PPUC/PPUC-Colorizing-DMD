// The project's license: the document, its undo step and the .cRPX chunk LICN.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "edit/LicenseEdits.h"
#include "legacy/LegacyProjectFileReader.h"
#include "platform/Utf8Path.h"
#include "project/ProjectFile.h"
#include "ProjectTestFiles.h"

namespace test_files = colorizing::test_files;

namespace {

using namespace colorizing;
using document::LicenseKind;
using document::ProjectLicense;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: license: " << message << '\n';
        std::exit(1);
    }
}

ProjectLicense makeLicense()
{
    ProjectLicense license;
    license.kind = LicenseKind::CcByNcSa;
    license.attribution = "Kraken & D\xc3\xa9j\xc3\xa0 Vu";
    license.url = "https://example.org/colorization";
    return license;
}

void testNames()
{
    expect(document::licenseShortName(LicenseKind::CcBySa) == "CC BY-SA 4.0" &&
               document::licenseShortName(LicenseKind::Cc0) == "CC0 1.0" &&
               document::licenseShortName(LicenseKind::Custom).empty(),
           "the licenses' short names");
    expect(document::licenseDeedUrl(LicenseKind::CcByNc) ==
                   "https://creativecommons.org/licenses/by-nc/4.0/" &&
               document::licenseDeedUrl(LicenseKind::AllRightsReserved).empty(),
           "the Creative Commons deeds");
    expect(!ProjectLicense{}.isSet() && makeLicense().isSet(), "a license is set or not");
}

void testDocumentAndUndo()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    expect(!document.license().isSet(), "a project starts without a license");
    const uint64_t revision = document.revision();
    const auto command = edit::setLicense(document, makeLicense());
    expect(command && document.license() == makeLicense() && document.isDirty() &&
               document.revision() != revision,
           "setting the license changes the project");
    expect(edit::setLicense(document, makeLicense()) == nullptr,
           "setting the same license is no step");
    command->revert(document);
    expect(!document.license().isSet(), "undo takes the license back");
    command->apply(document);
    expect(document.license() == makeLicense(), "redo sets it again");
    expect(document.toSnapshot().license == makeLicense(), "a snapshot carries it");

    ProjectLicense unknown;
    unknown.kind = static_cast<LicenseKind>(document::kLastLicenseKind + 1);
    bool refused = false;
    try {
        (void)edit::setLicense(document, unknown);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && document.license() == makeLicense(), "an unknown kind is refused");
}

std::filesystem::path tempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("colorizing_qt_v2_license_" + run + "_" + std::to_string(counter++) + suffix);
}

std::vector<char> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), {});
}

void writeFile(const std::filesystem::path& path, const std::vector<char>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Where the chunk LICN starts in the file, or npos.
std::size_t chunkStart(const std::vector<char>& bytes)
{
    const std::string text(bytes.begin(), bytes.end());
    return text.rfind("LICN");
}

void testFiles()
{
    document::ProjectSnapshot snapshot;
    snapshot.license = makeLicense();
    snapshot.license.customText = "Kept text\nof two lines";
    // The fixed files of a project with that license; the cRPX is changed below, so a copy.
    const std::filesystem::path cromPath = test_files::legacyFixturePath("license.cROM");
    const std::filesystem::path crpxPath = tempPath(".cRPX");
    std::filesystem::copy_file(test_files::legacyFixturePath("license.cRPX"), crpxPath);
    const legacy::LegacyProjectPaths paths{platform::utf8FromPath(cromPath),
                                           platform::utf8FromPath(crpxPath)};
    legacy::LegacyLoadResult loaded = legacy::LegacyProjectFileReader().load(paths);
    expect(loaded.ok && loaded.snapshot.license == snapshot.license,
           "and loads with its license (chunk LICN)");

    // A kind of a later editor is read as a custom license with its texts.
    std::vector<char> bytes = readFile(crpxPath);
    const std::size_t start = chunkStart(bytes);
    expect(start != std::string::npos, "the .cRPX has the chunk");
    bytes[start + 16] = 99;
    writeFile(crpxPath, bytes);
    loaded = legacy::LegacyProjectFileReader().load(paths);
    expect(loaded.ok && loaded.snapshot.license.kind == LicenseKind::Custom &&
               loaded.snapshot.license.attribution == snapshot.license.attribution,
           "an unknown kind is read as a custom license");

    // A text longer than the chunk.
    bytes[start + 16 + 4] = 0x7f;
    writeFile(crpxPath, bytes);
    loaded = legacy::LegacyProjectFileReader().load(paths);
    expect(!loaded.ok && loaded.error.message.find("license") != std::string::npos,
           "a damaged chunk is refused: " + loaded.error.message);

    std::filesystem::remove(crpxPath);
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// The kinds the editor offers allow no changes; their terms say what the choices allow.
void testTermsAndText()
{
    expect(!document::licenseAllowsChanges(LicenseKind::CcByNcNd) &&
               !document::licenseAllowsChanges(LicenseKind::PpucColorization) &&
               document::licenseAllowsChanges(LicenseKind::CcBySa),
           "which kinds allow changes");
    expect(document::licenseShortName(LicenseKind::CcByNcNd) == "CC BY-NC-ND 4.0" &&
               document::licenseDeedUrl(LicenseKind::CcByNd) ==
                   "https://creativecommons.org/licenses/by-nd/4.0/",
           "the no-changes Creative Commons licenses");

    ProjectLicense cc;
    cc.kind = LicenseKind::CcByNcNd;
    std::string terms;
    for (const std::string& term : document::licenseTerms(cc)) {
        terms += term + "\n";
    }
    expect(contains(terms, "not commercially") && !contains(terms, "CC+"),
           "CC BY-NC-ND without the permission");
    cc.commercialWithPpucDmd = true;
    terms.clear();
    for (const std::string& term : document::licenseTerms(cc)) {
        terms += term + "\n";
    }
    expect(contains(terms, "Additional permission (CC+)") && contains(terms, "PPUC DMD") &&
               contains(terms, "PIN2DMD"),
           "and with the CC+ permission for a PPUC DMD");

    ProjectLicense ppuc;
    ppuc.kind = LicenseKind::PpucColorization;
    ppuc.attribution = "Kraken";
    ppuc.url = "https://example.org/colorization";
    ppuc.realMachinesOnlyPpucDmd = true;
    const std::string text = document::colorizationText("afm_113b", ppuc);
    expect(text.rfind("Serum colorization\nROM: afm_113b\nAuthor: Kraken\n"
                      "License: PPUC Colorization License 1.0\n",
                      0) == 0,
           "the text starts with its fields: " + text);
    expect(contains(text, "Commercial use is not allowed.") &&
               contains(text, "On a real pinball machine the colorization may be used only "
                              "with a PPUC DMD") &&
               contains(text, "Link: https://example.org/colorization"),
           "the PPUC license's choices");
    expect(contains(text, "Do not change this file") &&
               contains(text, "do not remove or change its signature") &&
               contains(text, "join the Serum community") && contains(text, "AI assistants"),
           "the terms every cROMc carries");
}

// The project file keeps the new kinds and their choices.
void testProjectFile()
{
    document::ProjectSnapshot snapshot =
        document::ProjectDocument::fromSnapshot(
            runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 1))
            .toSnapshot();
    snapshot.license.kind = LicenseKind::PpucColorization;
    snapshot.license.commercialWithPpucDmd = true;
    snapshot.license.realMachinesOnlyPpucDmd = true;
    const std::filesystem::path path = tempPath(".cdmd");
    const legacy::LegacySaveResult saved =
        project::saveProjectFile(platform::utf8FromPath(path), snapshot, {"test"});
    expect(saved.ok, "saved: " + saved.error.message);
    const legacy::LegacyLoadResult loaded =
        project::loadProjectFile(platform::utf8FromPath(path));
    expect(loaded.ok && loaded.snapshot.license == snapshot.license,
           "the PPUC license and its choices come back");
    std::filesystem::remove(path);
}

}  // namespace

void runLicenseTests()
{
    testNames();
    testDocumentAndUndo();
    testFiles();
    testTermsAndText();
    testProjectFile();
}
