#include "runtime/CromcExport.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"
#include "runtime/RuntimePreview.h"
#include "serum-decode.h"

namespace {

using namespace colorizing;
namespace fs = std::filesystem;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: cROMc export: " << message << '\n';
        std::exit(1);
    }
}

fs::path uniqueDirectory(const std::string& name)
{
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    // A character outside code page 1252, as a user name in the temp path can have.
    return fs::temp_directory_path() /
           platform::pathFromUtf8("colorizing_qt_v2_cromc_\xce\xa9_" + run + "_" + name);
}

void testExportLoadsInLibserum()
{
    runtime::RuntimePreview::instance().close();
    const document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3);
    const fs::path base = uniqueDirectory("export");
    const fs::path sidecars = base / "project";
    fs::create_directories(sidecars);
    // A sidecar in another case is found as libserum finds it.
    std::ofstream(sidecars / (snapshot.romName + ".PUP.csv")).put('\n');
    const fs::path target = base / "out.cROMc";

    legacy::LegacyIoProgress progress;
    const runtime::CromcExportResult result =
        runtime::exportCromc(snapshot, base / "work", sidecars, target, &progress);
    expect(result.ok, "the export works: " + result.error);
    expect(fs::is_regular_file(target) && fs::file_size(target) > 0, "the cROMc is there");
    expect(!fs::exists(base / "work"), "the work folder is removed");
    expect(result.sidecars == std::vector<std::string>{snapshot.romName + ".PUP.csv"},
           "the PUP csv went in");
    expect(progress.stage.load() == static_cast<int>(legacy::LegacyIoStage::Generating),
           "the export reports libserum's part; no cROM is written");

    // libserum loads it as a machine does: from <altcolor>/<rom>/<rom>.cROMc, no cROM beside.
    // libserum's paths are ANSI on Windows: this folder has no character outside it.
    const fs::path altcolor =
        fs::temp_directory_path() /
        ("colorizing_qt_v2_cromc_altcolor_" +
         std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    fs::create_directories(altcolor / snapshot.romName);
    fs::copy_file(target, altcolor / snapshot.romName / (snapshot.romName + ".cROMc"));
    Serum_SetGenerateCRomC(false);
    const Serum_Frame_Struc* loaded =
        Serum_Load(platform::utf8FromPath(altcolor).c_str(), snapshot.romName.c_str(), 0);
    expect(loaded != nullptr, "libserum loads the cROMc");
    std::vector<uint8_t> original(snapshot.frames[0].originalSd.pixels.begin(),
                                  snapshot.frames[0].originalSd.pixels.end());
    expect(Serum_Colorize(original.data()) != IDENTIFY_NO_FRAME,
           "and identifies the project's first frame");
    Serum_Dispose();
    Serum_SetGenerateCRomC(true);
    fs::remove_all(altcolor);
    fs::remove_all(base);
}

// A project with colorization settings brings them in its cROM: no sidecar is taken, and the
// cROMc keeps them against a scaling.txt beside it.
void testSettingsTakeNoSidecars()
{
    runtime::RuntimePreview::instance().close();
    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2);
    document::ColorizationSettings settings;
    settings.scalingAlgorithm = document::kScalingLineDoubling;
    snapshot.colorizationSettings = settings;
    const fs::path base = uniqueDirectory("settings");
    const fs::path sidecars = base / "project";
    fs::create_directories(sidecars);
    std::ofstream(sidecars / (snapshot.romName + ".pup.csv")) << "9,2,40\n";
    std::ofstream(sidecars / "scaling.txt") << "scale2x\n";
    const fs::path target = base / "out.cROMc";
    const runtime::CromcExportResult result =
        runtime::exportCromc(snapshot, base / "work", sidecars, target);
    expect(result.ok && result.sidecars.empty(), "the export takes no sidecar");

    const fs::path altcolor =
        fs::temp_directory_path() /
        ("colorizing_qt_v2_cromc_settings_" +
         std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    fs::create_directories(altcolor / snapshot.romName);
    fs::copy_file(target, altcolor / snapshot.romName / (snapshot.romName + ".cROMc"));
    std::ofstream(altcolor / snapshot.romName / "scaling.txt") << "scale2x\n";
    Serum_SetGenerateCRomC(false);
    expect(Serum_Load(platform::utf8FromPath(altcolor).c_str(), snapshot.romName.c_str(), 0) !=
                   nullptr &&
               Serum_GetScalingAlgorithm() == document::kScalingLineDoubling,
           "the cROMc keeps the project's scaling, whatever scaling.txt says");
    Serum_Dispose();
    Serum_SetGenerateCRomC(true);
    fs::remove_all(altcolor);
    fs::remove_all(base);
}

// The export writes no cROM: libserum builds the cROMc from the project. It is the cROMc
// libserum generates itself from the project's cROM, byte for byte.
void testExportIsLibserumsOwnCromc()
{
    for (const render::PlaneLayout layout : {render::PlaneLayout::Ordinary,
                                             render::PlaneLayout::NativeExtra,
                                             render::PlaneLayout::NativeOriginal}) {
        runtime::RuntimePreview::instance().close();
        document::ProjectSnapshot snapshot = runtime_fixtures::makeProject(layout, 4);
        if (layout == render::PlaneLayout::Ordinary) {
            snapshot.colorizationSettings = document::ColorizationSettings{};
            snapshot.colorizationSettings->scalingAlgorithm = document::kScalingLineDoubling;
        }
        const fs::path base = uniqueDirectory("same");
        const fs::path target = base / "exported.cROMc";
        const runtime::CromcExportResult result =
            runtime::exportCromc(snapshot, base / "work", {}, target);
        expect(result.ok, "the export works: " + result.error);

        // libserum's own generation, from the cROM in an altcolor folder (ANSI on Windows).
        const fs::path altcolor =
            fs::temp_directory_path() /
            ("colorizing_qt_v2_cromc_same_" +
             std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        const fs::path folder = altcolor / snapshot.romName;
        fs::create_directories(folder);
        {
            std::ofstream crom(folder / (snapshot.romName + ".cROM"), std::ios::binary);
            std::string error;
            expect(legacy::writeLegacyCrom(crom, snapshot, error), "the cROM is written");
        }
        Serum_SetGenerateCRomC(true);
        expect(Serum_Load(platform::utf8FromPath(altcolor).c_str(), snapshot.romName.c_str(),
                          0) != nullptr,
               "libserum loads the cROM");
        Serum_Dispose();
        const auto bytes = [](const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(in), {});
        };
        const std::vector<char> generated = bytes(folder / (snapshot.romName + ".cROMc"));
        expect(!generated.empty() && generated == bytes(target),
               "the exported cROMc is libserum's own");
        fs::remove_all(altcolor);
        fs::remove_all(base);
    }
}

void testFailureLeavesTarget()
{
    runtime::RuntimePreview::instance().close();
    const document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2);
    const fs::path base = uniqueDirectory("failure");
    // The target's folder does not exist: nothing is written, no partial file.
    const fs::path target = base / "missing" / "out.cROMc";
    const runtime::CromcExportResult result =
        runtime::exportCromc(snapshot, base / "work", {}, target);
    expect(!result.ok && !result.error.empty(), "a target that cannot be written fails");
    expect(!fs::exists(target) && !fs::exists(fs::path(target).concat(".saving")),
           "and leaves nothing behind");

    legacy::LegacyIoProgress cancelled;
    cancelled.cancel = true;
    const runtime::CromcExportResult stopped =
        runtime::exportCromc(snapshot, base / "work", {}, base / "out.cROMc", &cancelled);
    expect(stopped.cancelled && !fs::exists(base / "out.cROMc"), "a cancel writes nothing");
    fs::remove_all(base);
}

}  // namespace

void runCromcExportTests()
{
    testExportLoadsInLibserum();
    testSettingsTakeNoSidecars();
    testExportIsLibserumsOwnCromc();
    testFailureLeavesTarget();
}
