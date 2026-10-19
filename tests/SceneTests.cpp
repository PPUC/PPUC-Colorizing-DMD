#include "runtime/Scenes.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "legacy/LegacyProjectSerializer.h"
#include "runtime/FrameTester.h"
#include "runtime/RuntimePreview.h"
#if defined(_MSC_VER)
#pragma warning(push, 0)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "SceneGenerator.h"
#include "serum-decode.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace {

using namespace colorizing;
namespace fs = std::filesystem;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: scenes: " << message << '\n';
        std::exit(1);
    }
}

fs::path uniqueDirectory(const std::string& name)
{
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return fs::temp_directory_path() / ("colorizing_qt_v2_scenes_" + run + "_" + name);
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

// The csv is read as libserum reads it, line for line.
void testCsvReadsAsLibserum()
{
    const fs::path directory = uniqueDirectory("csv");
    fs::create_directories(directory);
    const fs::path csv = directory / "scenes.csv";
    writeText(csv,
              "# id,frames,duration\r\n"
              "1001,4,40\r\n"
              "\n"
              "7,2\n"
              "8,x,100\n"
              "9,3,50,1,1,2,0,1,300,6\n"
              "10,2,60,0,0,1,4,0,5,33\n");
    const auto scenes = runtime::readSceneCsv(csv);
    SceneGenerator generator;
    expect(generator.parseCSV(csv.string()), "libserum reads the csv");
    const std::vector<SceneData>& expected = generator.getSceneData();
    expect(scenes && scenes->size() == expected.size() && expected.size() == 3,
           "the same lines are scenes");
    for (std::size_t index = 0; scenes && index < scenes->size(); ++index) {
        const document::SceneDefinition& scene = (*scenes)[index];
        const SceneData& data = expected[index];
        expect(scene.sceneId == data.sceneId && scene.frameCount == data.frameCount &&
                   scene.durationMs == data.durationPerFrame &&
                   scene.interruptable == data.interruptable &&
                   scene.immediateStart == data.immediateStart && scene.repeat == data.repeat &&
                   scene.frameGroups == (data.frameGroups == 0 ? 1 : data.frameGroups) &&
                   scene.random == data.random && scene.autoStartSeconds == data.autoStart &&
                   scene.options == data.sceneOptions,
               "scene " + std::to_string(index) + " has libserum's values");
    }
    expect(!runtime::readSceneCsv(directory / "missing.csv"), "no file, no scenes");

    const fs::path scaling = directory / "scaling.txt";
    writeText(scaling, "# comment\nLine-Doubling\nshadow-offset: proportional # two\nfoo: bar\n");
    const auto settings = runtime::readScalingFile(scaling);
    expect(settings && settings->scalingAlgorithm == document::kScalingLineDoubling &&
               settings->shadowOffset == document::kShadowOffsetProportional,
           "scaling.txt is read as libserum reads it");
    writeText(scaling, "algorithm: scale2x\n");
    const auto defaults = runtime::readScalingFile(scaling);
    expect(defaults && defaults->scalingAlgorithm == document::kScalingScale2x &&
               defaults->shadowOffset == document::kShadowOffsetNative,
           "what it does not set keeps its default");
    fs::remove_all(directory);
}

void testMarkers()
{
    document::SceneDefinition scene;
    scene.sceneId = 1234;
    scene.frameCount = 3;
    scene.frameGroups = 2;
    const document::IndexedFrame first = runtime::sceneMarkerFrame(scene, {1234, 1, 0}, 4);
    const document::IndexedFrame later = runtime::sceneMarkerFrame(scene, {1234, 2, 2}, 4);
    expect(first.size.width == 128 && first.size.height == 32 && first.isValid(),
           "a marker is a 128x32 frame");
    SceneGenerator generator;
    SceneData data;
    data.sceneId = 1234;
    data.frameCount = 3;
    data.frameGroups = 2;
    generator.setSceneData({data});
    generator.setDepth(2);
    std::vector<uint8_t> pixels(128 * 32, 0);
    // As libserum generates a group: from its first frame on.
    for (uint16_t index = 0; index <= 2; ++index) {
        generator.generateFrame(1234, index, pixels.data(), 2, true);
    }
    bool same = later.pixels.size() == pixels.size();
    uint8_t brightest = 0;
    for (std::size_t index = 0; same && index < pixels.size(); ++index) {
        same = later.pixels[index] == pixels[index];
        brightest = std::max<uint8_t>(brightest, later.pixels[index]);
    }
    expect(same && brightest == 3, "it is what libserum generates, in 4 shades");
    bool differs = false;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        differs = differs || first.pixels[index] != later.pixels[index];
    }
    expect(differs, "frames of a scene differ");
    const document::IndexedFrame sameIndex = runtime::sceneMarkerFrame(scene, {1234, 1, 2}, 4);
    bool groupsDiffer = false;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        groupsDiffer = groupsDiffer || sameIndex.pixels[index] != later.pixels[index];
    }
    expect(groupsDiffer, "and so do the same frames of two groups");
    uint8_t brightest16 = 0;
    const document::IndexedFrame sixteen = runtime::sceneMarkerFrame(scene, {1234, 1, 0}, 16);
    for (std::size_t index = 0; index < sixteen.pixels.size(); ++index) {
        brightest16 = std::max<uint8_t>(brightest16, sixteen.pixels[index]);
    }
    expect(brightest16 == 15, "and in 16 shades for a 16-color ROM");
    bool threw = false;
    try {
        (void)runtime::sceneMarkerFrame(scene, {1234, 3, 0}, 4);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "a group the scene does not have is refused");
}

std::vector<std::string> g_log;

void logLine(const char* format, va_list args, const void*)
{
    char line[512];
    std::vsnprintf(line, sizeof(line), format, args);
    g_log.emplace_back(line);
}

bool logged(const std::string& text)
{
    for (const std::string& line : g_log) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A project of the Win32 editor takes the scenes and scaling settings libserum used for it
// from the files beside it, with its scene frames linked; its cROM then carries them, and
// libserum plays those, whatever a pup.csv says.
void testImportAndCrom()
{
    runtime::RuntimePreview::instance().close();
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 4));
    expect(runtime::supportsScenes(document.geometry()), "a 128x32 project has scenes");
    document::SceneDefinition scene;
    scene.sceneId = 50;
    scene.frameCount = 2;
    scene.durationMs = 40;
    document.setOriginalFrame(1, runtime::sceneMarkerFrame(scene, {50, 1, 1},
                                                           document.colorCount()));
    document.setOriginalFrame(3, runtime::sceneMarkerFrame(scene, {50, 1, 0},
                                                           document.colorCount()));
    const fs::path directory = uniqueDirectory("import");
    fs::create_directories(directory);
    writeText(directory / (document.romName() + ".PUP.CSV"), "50,2,40\n");
    writeText(directory / "Scaling.txt", "line-doubling\n");

    const runtime::SidecarImport imported =
        runtime::importColorizationSidecars(document, directory);
    expect(imported.files.size() == 2 && imported.scenes == 1 && imported.linkedFrames == 2,
           "both files are read and the two scene frames linked");
    expect(document.colorizationSettings() &&
               document.colorizationSettings()->scenes == std::vector{scene} &&
               document.colorizationSettings()->scalingAlgorithm ==
                   document::kScalingLineDoubling,
           "the project has the settings");
    expect(document.sceneFrame(1) == document::SceneFrameRef{50, 1, 1} &&
               document.sceneFrame(3) == document::SceneFrameRef{50, 1, 0} &&
               !document.sceneFrame(0) && !document.sceneFrame(2),
           "each scene frame knows which it is");
    expect(runtime::importColorizationSidecars(document, directory).files.empty(),
           "a project that has settings imports nothing");

    // Another scene in the csv beside it changes nothing.
    writeText(directory / (document.romName() + ".PUP.CSV"), "60,2,40\n");
    const std::vector<uint8_t> crom = runtime::projectCrom(document);
    expect(legacy::legacyCromByteCount(document.toSnapshot()) == crom.size(),
           "the cROM with its settings block has the size counted for it");
    g_log.clear();
    Serum_SetLogCallback(logLine, nullptr);
    const std::string csv = (directory / (document.romName() + ".PUP.CSV")).string();
    expect(Serum_LoadFromMemoryWithScenes(crom.data(), crom.size(), csv.c_str(), 0) != nullptr,
           "libserum loads the cROM");
    expect(logged("2 rotation scene frames") && logged("not read") &&
               Serum_GetScalingAlgorithm() == document::kScalingLineDoubling,
           "with the project's scene frames and scaling, the csv unread");
    Serum_Dispose();
    Serum_SetLogCallback(nullptr, nullptr);
    fs::remove_all(directory);
}

}  // namespace

void runSceneTests()
{
    testCsvReadsAsLibserum();
    testMarkers();
    testImportAndCrom();
}
