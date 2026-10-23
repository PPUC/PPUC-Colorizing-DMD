// The project file (project/ProjectFile.h) and its JSON (project/Json.h): a project saved and
// opened again is the same project, down to the bytes of its cROM and .cRPX.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "legacy/CrpxFile.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"
#include "project/Json.h"
#include "project/ProjectFile.h"
#include "project/ZipArchive.h"

namespace {

using namespace colorizing;
using document::ProjectDocument;
using document::ProjectSnapshot;
using project::JsonValue;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: project file: " << message << '\n';
        std::exit(1);
    }
}

void testJson()
{
    JsonValue root = JsonValue::object();
    root.set("text", "a \"quoted\" \\ line\nwith \xc3\xbc and \x01");
    root.set("negative", int64_t{-9223372036854775807LL - 1});
    root.set("big", int64_t{9223372036854775807LL});
    root.set("flag", true);
    root.set("nothing", JsonValue());
    JsonValue list = JsonValue::array();
    list.push(1);
    list.push(JsonValue::object());
    list.push(JsonValue::array());
    root.set("list", std::move(list));
    const std::string text = project::writeJson(root);
    std::string error;
    const std::optional<JsonValue> parsed = project::parseJson(text, &error);
    expect(parsed && project::writeJson(*parsed) == text, "JSON reads back as written: " + error);
    const project::JsonObjectReader reader(*parsed, "test");
    expect(reader.string("text") == *root.find("text")->stringValue() &&
               reader.integer("negative", INT64_MIN, 0) == INT64_MIN &&
               reader.boolean("flag", false) && !reader.has("nothing") &&
               reader.array("list").size() == 3,
           "the values read back");

    const std::optional<JsonValue> escaped =
        project::parseJson(R"({"s": "\u00fc\ud83d\ude00\/"})", &error);
    expect(escaped && *escaped->find("s")->stringValue() == "\xc3\xbc\xf0\x9f\x98\x80/",
           "escapes and surrogate pairs become UTF-8");
    for (const char* bad : {"{\"a\": 1.5}", "{\"a\": 1e3}", "[1,]", "{} x", "\"\\ud800\"",
                            "9223372036854775808", "{\"a\" 1}", "01", "[\"a\nb\"]"}) {
        expect(!project::parseJson(bad, &error) && !error.empty(),
               std::string("no JSON: ") + bad);
    }
    std::string deep(65, '[');
    deep += std::string(65, ']');
    expect(!project::parseJson(deep), "nesting is limited");
    bool thrown = false;
    try {
        (void)reader.integer("big", 0, 10);
    } catch (const project::JsonFormatError& failure) {
        thrown = std::string(failure.what()).find("\"big\" of the test") != std::string::npos;
    }
    expect(thrown, "a value out of range names itself");
}

// A project with every kind of content the file holds, through a document so that every
// component has an ID.
ProjectSnapshot makeRichProject(render::PlaneLayout layout)
{
    ProjectSnapshot snapshot = runtime_fixtures::makeFullProject(layout);
    snapshot.libserumTarget = document::LibserumTarget::Libserum28;
    document::EditorMetadata& tables = snapshot.editorMetadata;
    document::setFixedName(tables.maskNames, 1, document::kMaskNameBytes, "Score m\xc3\xa4sk");
    tables.sectionCount = 2;
    tables.sectionFirstFrames[0] = 0;
    tables.sectionFirstFrames[1] = 4;
    document::setFixedName(tables.sectionNames, 0, document::kSectionNameBytes, "Attract");
    document::setFixedName(tables.sectionNames, 1, document::kSectionNameBytes, "Game");
    tables.spriteSourceFrames[1] = 3;
    tables.spriteSourceRects[4] = 2;
    tables.spriteSourceRects[5] = 3;
    tables.spriteSourceRects[6] = 20;
    tables.spriteSourceRects[7] = 10;
    tables.spriteSourceMirrors[3] = 1;
    tables.editColors[2] = 0xf81f;
    tables.activeColorSets[5] = 1;
    tables.colorSets[5 * document::kColorSetColorCount + 3] = 0x07e0;
    document::setFixedName(tables.colorSetNames, 5, document::kColorSetNameBytes, "Fire");
    tables.currentColorSet = 5;
    tables.drawColorMode = 1;
    tables.maskSelectionMode = -1;
    tables.imagePositionCount = 1;
    document::setFixedName(tables.imagePositionNames, 0, document::kImagePositionNameBytes,
                           "Logo");
    tables.imagePositions[3] = -40;
    tables.timeElapsedMs = 123456;
    tables.hasPupPack = 1;
    tables.pupPackName = "afm_113b";
    tables.importedPalettes.assign(2 * document::kImportedPaletteBytes, 7);
    snapshot.palettes.assign(document::kPaletteCount, document::PaletteRecord{});
    for (std::size_t p = 0; p < snapshot.palettes.size(); ++p) {
        snapshot.palettes[p].colors.assign(document::kPaletteColorCount,
                                           static_cast<uint16_t>(p * 3));
        snapshot.palettes[p].name = p == 2 ? "Blue" : "";
    }
    snapshot.frames[2].tags = {"Multiball", "Score"};
    snapshot.frames[3].scene = document::SceneFrameRef{51000, 1, 0};
    document::ColorizationSettings settings;
    document::SceneDefinition scene;
    scene.sceneId = 51000;
    scene.frameCount = 1;
    scene.random = true;
    settings.scenes.push_back(scene);
    settings.shadowOffset = document::kShadowOffsetProportional;
    snapshot.colorizationSettings = settings;
    snapshot.frames[4].triggerId = 17;
    snapshot.frames[4].durationMs = 2500;
    snapshot.frames[5].originalOnlyDynamicSets = 0x24;
    document::LightEffect effect;
    effect.id = 1;
    effect.name = "Rainbow";
    snapshot.speakerLights.effects.push_back(effect);
    snapshot.speakerLights.defaults[0] = {document::LightActionKind::Effect, 1, 0};
    snapshot.frames[1].lights[1] = {document::LightActionKind::Color, 0, 0x00123456u, true};
    snapshot.helperLines.push_back({"Middle", true, 64, render::Resolution::Sd, true});
    document::SavedImageZone zone;
    zone.name = "Top";
    zone.end = document::PictureZone{1, 2, 3, 4};
    zone.contrast = -5;
    snapshot.imageZones.push_back(zone);
    snapshot.license.kind = document::LicenseKind::CcBySa;
    snapshot.license.attribution = "Kraken";
    // A reference that names no component, as a legacy file can have it.
    snapshot.frames[6].comparisonMaskId = 40;
    return ProjectDocument::fromSnapshot(std::move(snapshot)).toSnapshot();
}

std::string cromBytes(const ProjectSnapshot& snapshot)
{
    std::ostringstream stream;
    std::string error;
    expect(legacy::writeLegacyCrom(stream, snapshot, error), "the cROM is written: " + error);
    return stream.str();
}

std::string crpxBytes(const ProjectSnapshot& snapshot)
{
    std::ostringstream stream;
    std::string error;
    expect(legacy::writeCrpx(stream, snapshot, error), "the .cRPX is written: " + error);
    return stream.str();
}

bool sameMeta(const ProjectSnapshot& a, const ProjectSnapshot& b)
{
    if (a.projectId != b.projectId || a.comparisonMaskMeta != b.comparisonMaskMeta ||
        a.frames.size() != b.frames.size() || a.sprites.size() != b.sprites.size() ||
        a.backgrounds.size() != b.backgrounds.size() || a.palettes.size() != b.palettes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.frames.size(); ++i) {
        if (a.frames[i].meta != b.frames[i].meta) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.sprites.size(); ++i) {
        if (a.sprites[i].meta != b.sprites[i].meta) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.backgrounds.size(); ++i) {
        if (a.backgrounds[i].meta != b.backgrounds[i].meta) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.palettes.size(); ++i) {
        if (a.palettes[i].meta != b.palettes[i].meta) {
            return false;
        }
    }
    return true;
}

using Entries = std::map<std::string, std::string>;

Entries entriesOf(const ProjectSnapshot& snapshot)
{
    Entries entries;
    project::forEachProjectEntry(snapshot, {"test"},
                                 [&](const std::string& name, const std::string& bytes) {
                                     expect(entries.emplace(name, bytes).second,
                                            "each entry once: " + name);
                                 });
    return entries;
}

std::optional<ProjectSnapshot> fromEntries(const Entries& entries, std::string& error)
{
    return project::readProjectEntries(
        [&entries](const std::string& name) -> std::optional<std::string> {
            const auto found = entries.find(name);
            if (found == entries.end()) {
                return std::nullopt;
            }
            return found->second;
        },
        error);
}

void testRoundTrip(render::PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeRichProject(layout);
    snapshot.frames[0].meta.author = "Kraken";
    snapshot.frames[0].meta.modifiedMs = 1761200000000;
    snapshot.frames[0].meta.revision = document::ComponentId::generate();
    snapshot.frames[0].meta.contentHash = 0xfedcba9876543210ULL;
    const Entries entries = entriesOf(snapshot);
    expect(entries.count("manifest.json") && entries.count("order.json") &&
               entries.count("palettes.json") &&
               entries.count("frames/" + snapshot.frames[3].meta.id.toString() + ".bin") &&
               entries.count("sprites/" + snapshot.sprites[0].meta.id.toString() + ".bin"),
           "one entry per component, named by its ID");
    std::string error;
    const std::optional<ProjectSnapshot> read = fromEntries(entries, error);
    expect(read.has_value(), "the entries read back: " + error);
    expect(cromBytes(*read) == cromBytes(snapshot), "the same cROM");
    expect(crpxBytes(*read) == crpxBytes(snapshot), "the same .cRPX");
    expect(sameMeta(*read, snapshot), "the same IDs and change information");
    expect(read->license == snapshot.license && read->geometry == snapshot.geometry &&
               read->frames[6].comparisonMaskId == 40,
           "the license, the layout and a reference to nothing");
    expect(entriesOf(*read) == entries, "written again, the same entries");
}

std::filesystem::path tempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("colorizing_qt_v2_project_" + run + "_" + std::to_string(counter++) + suffix);
}

void testFile()
{
    const ProjectSnapshot snapshot = makeRichProject(render::PlaneLayout::Ordinary);
    const std::filesystem::path path = tempPath(".cdmd");
    const std::string utf8 = platform::utf8FromPath(path);
    expect(project::hasProjectFileExtension(utf8) &&
               project::hasProjectFileExtension("x.CDMD") &&
               !project::hasProjectFileExtension("x.cRZ"),
           "the extension");
    legacy::LegacyIoProgress progress;
    const legacy::LegacySaveResult saved =
        project::saveProjectFile(utf8, snapshot, {"test"}, &progress);
    expect(saved.ok && progress.done.load() == progress.total.load(),
           "the project is saved: " + saved.error.message);
    expect(!std::filesystem::exists(path.string() + ".saving"), "no temporary file is left");
    legacy::LegacyLoadResult loaded = project::loadProjectFile(utf8, &progress);
    expect(loaded.ok && cromBytes(loaded.snapshot) == cromBytes(snapshot) &&
               sameMeta(loaded.snapshot, snapshot),
           "and opened again: " + loaded.error.message);

    // Damaged files.
    project::ZipReader reader(utf8);
    Entries entries;
    for (const std::string& name : reader.entryNames()) {
        entries[name] = *reader.read(name);
    }
    const auto refused = [&](Entries changed, const std::string& expected) {
        std::string error;
        const std::optional<ProjectSnapshot> read = fromEntries(changed, error);
        expect(!read && error.find(expected) != std::string::npos,
               "refused with \"" + expected + "\": " + error);
    };
    const std::string frameEntry = "frames/" + snapshot.frames[2].meta.id.toString() + ".bin";
    Entries changed = entries;
    changed.erase(frameEntry);
    refused(changed, "lacks the frame " + snapshot.frames[2].meta.id.toString());
    changed = entries;
    changed[frameEntry].resize(changed[frameEntry].size() - 3);
    refused(changed, "frame 2 is damaged");
    changed = entries;
    changed[frameEntry] = entries["frames/" + snapshot.frames[3].meta.id.toString() + ".bin"];
    refused(changed, "another ID");
    changed = entries;
    const std::string version = "\"formatVersion\": 1";
    changed["manifest.json"].replace(changed["manifest.json"].find(version), version.size(),
                                     "\"formatVersion\": 2");
    refused(changed, "newer editor");
    changed = entries;
    changed["order.json"] = "{";
    refused(changed, "order.json is damaged");

    std::ofstream(path, std::ios::binary | std::ios::trunc) << "no zip";
    loaded = project::loadProjectFile(utf8);
    expect(!loaded.ok && loaded.error.message.find("no zip") != std::string::npos,
           "a file that is no zip archive: " + loaded.error.message);
    std::filesystem::remove(path);
}

// A real project, when COLORIZING_TEST_PROJECT names its cROM or cRZ (the editor file beside
// it): converted to the project file and back, it has the same cROM.
void testRealProject()
{
    const char* path = std::getenv("COLORIZING_TEST_PROJECT");
    if (!path) {
        return;
    }
    const std::string crom = path;
    const std::string stem = crom.substr(0, crom.find_last_of('.'));
    legacy::LegacyProjectPaths paths;
    paths.cromPath = crom;
    paths.crpPath = std::filesystem::exists(stem + ".cRPX") ? stem + ".cRPX" : stem + ".cRP";
    legacy::LegacyLoadResult loaded = legacy::LegacyProjectFileReader().load(paths);
    expect(loaded.ok, "the real project loads: " + loaded.error.message);
    const ProjectSnapshot snapshot =
        ProjectDocument::fromSnapshot(std::move(loaded.snapshot)).toSnapshot();
    const std::filesystem::path file = tempPath(".cdmd");
    const auto start = std::chrono::steady_clock::now();
    expect(project::saveProjectFile(platform::utf8FromPath(file), snapshot, {"test"}).ok,
           "the real project is saved");
    const auto saved = std::chrono::steady_clock::now();
    const legacy::LegacyLoadResult reloaded =
        project::loadProjectFile(platform::utf8FromPath(file));
    const auto opened = std::chrono::steady_clock::now();
    expect(reloaded.ok && cromBytes(reloaded.snapshot) == cromBytes(snapshot),
           "the real project has the same cROM: " + reloaded.error.message);
    const auto ms = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    };
    std::cout << "real project: " << std::filesystem::file_size(file) << " bytes, saved in "
              << ms(saved - start) << " ms, opened in " << ms(opened - saved) << " ms, .cRPX "
              << (crpxBytes(reloaded.snapshot) == crpxBytes(snapshot) ? "identical"
                                                                      : "differs")
              << '\n';
    std::filesystem::remove(file);
}

}  // namespace

void runProjectFileTests()
{
    testJson();
    for (const render::PlaneLayout layout :
         {render::PlaneLayout::Ordinary, render::PlaneLayout::NativeExtra,
          render::PlaneLayout::NativeOriginal}) {
        testRoundTrip(layout);
    }
    testFile();
    testRealProject();
}
