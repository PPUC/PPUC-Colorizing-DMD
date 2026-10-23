// Packages (edit/PackageImport.h): frames of a project with what they use, written as a
// package file and imported into another project, with the frames it has already.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/EditorMetadata.h"
#include "document/ProjectDocument.h"
#include "edit/PackageImport.h"
#include "edit/UndoHistories.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"
#include "project/ProjectFile.h"

namespace {

using namespace colorizing;
using document::ProjectDocument;
using document::ProjectSnapshot;
using edit::KnownFrames;
namespace fixtures = runtime_fixtures;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: packages: " << message << '\n';
        std::exit(1);
    }
}

// The full fixture with a mask name, a light effect frame 4 starts, and an effect no package
// frame starts.
ProjectDocument makeSource()
{
    ProjectSnapshot snapshot = fixtures::makeFullProject(render::PlaneLayout::Ordinary);
    document::setFixedName(snapshot.editorMetadata.maskNames, 2, document::kMaskNameBytes,
                           "Score");
    document::LightEffect used;
    used.id = 3;
    used.name = "Used";
    document::LightEffect unused;
    unused.id = 4;
    unused.name = "Unused";
    snapshot.speakerLights.effects = {used, unused};
    snapshot.frames[4].lights[0] = {document::LightActionKind::Effect, 3, 0};
    snapshot.frames[1].tags = {"Attract"};
    snapshot.license.kind = document::LicenseKind::CcBy;
    // Change information comes with an ID only (without one a frame is a new one).
    snapshot.frames[1].meta = {document::ComponentId::generate(), {}, "Kraken", 1000, 0};
    snapshot.frames[4].meta = {document::ComponentId::generate(), {}, "Enthusiast", 2000, 0};
    return ProjectDocument::fromSnapshot(std::move(snapshot));
}

std::string cromOf(const ProjectSnapshot& snapshot)
{
    std::ostringstream stream;
    std::string error;
    expect(legacy::writeLegacyCrom(stream, snapshot, error), "the cROM is written: " + error);
    return stream.str();
}

void testPackageContent()
{
    const ProjectDocument source = makeSource();
    const ProjectSnapshot package = edit::makePackage(source, {1, 4});
    expect(package.frames.size() == 2 && package.comparisonMasks.size() == 1 &&
               package.backgrounds.size() == 2 && package.sprites.size() == 2,
           "the frames with the mask, the backgrounds and the sprites they use");
    expect(document::fixedName(package.editorMetadata.maskNames, 0, document::kMaskNameBytes) ==
               "Score" &&
               package.sprites[0].name == source.sprite(1).name,
           "with the mask's and the sprites' names");
    expect(package.speakerLights.effects.size() == 1 &&
               package.speakerLights.effects[0].name == "Used",
           "only the light effects its frames start");
    expect(package.projectId == source.projectId() &&
               package.license.kind == document::LicenseKind::CcBy &&
               package.frames[0].meta.id == source.frameMeta(1).id &&
               package.frames[0].tags == std::vector<std::string>{"Attract"},
           "the source project's ID and license, the frames' IDs and tags");
}

std::filesystem::path tempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("colorizing_qt_v2_package_" + run + "_" + std::to_string(counter++) + suffix);
}

void testPackageFile()
{
    const ProjectDocument source = makeSource();
    const ProjectSnapshot package = edit::makePackage(source, {1, 4});
    const std::string packagePath = platform::utf8FromPath(tempPath(".cdmdpkg"));
    project::ProjectFileOptions options{"test"};
    options.package = true;
    expect(project::hasPackageExtension(packagePath) &&
               project::saveProjectFile(packagePath, package, options).ok,
           "the package is written");
    const legacy::LegacyLoadResult loaded = project::loadPackageFile(packagePath);
    expect(loaded.ok && cromOf(loaded.snapshot) == cromOf(package) &&
               loaded.snapshot.frames[1].meta == package.frames[1].meta,
           "and read back: " + loaded.error.message);
    const legacy::LegacyLoadResult asProject = project::loadProjectFile(packagePath);
    expect(!asProject.ok && asProject.error.message.find("package") != std::string::npos,
           "a package does not open as a project");

    const std::string projectPath = platform::utf8FromPath(tempPath(".cdmd"));
    expect(project::saveProjectFile(projectPath, source.toSnapshot(), {"test"}).ok,
           "a project is written");
    expect(!project::loadPackageFile(projectPath).ok, "a project is no package");
    std::filesystem::remove(packagePath);
    std::filesystem::remove(projectPath);
}

void testImportIntoAnotherProject()
{
    const ProjectDocument source = makeSource();
    const ProjectSnapshot package = edit::makePackage(source, {1, 4});
    ProjectSnapshot targetSnapshot = fixtures::makeProject(render::PlaneLayout::Ordinary, 3);
    // A sprite of the same name, another picture.
    targetSnapshot.sprites.push_back(fixtures::emptySprite());
    targetSnapshot.sprites.back().name = source.sprite(0).name;
    ProjectDocument target = ProjectDocument::fromSnapshot(std::move(targetSnapshot));
    const ProjectDocument untouched = target;

    const edit::PackageImportPlan plan = edit::planPackageImport(target, package);
    expect(plan.problem.empty() && plan.knownFrameCount() == 0 &&
               plan.authors == std::vector<std::string>{"Enthusiast", "Kraken"},
           "nothing known, and who made the frames: " + plan.problem + " known " +
               std::to_string(plan.knownFrameCount()) + " authors " +
               std::to_string(plan.authors.size()));

    const auto command = edit::importPackage(target, package, KnownFrames::Update);
    expect(command && target.frameCount() == 5 && command->counts().addedFrames == 2 &&
               command->counts().addedMasks == 1 && command->counts().addedBackgrounds == 2 &&
               command->counts().addedSprites == 2 && target.isDirty(),
           "the frames are added behind the last one, with what they use");
    {
        // The source has another light effect and defaults, which the package leaves behind.
        ProjectSnapshot imported = target.toSubsetSnapshot({3, 4});
        ProjectSnapshot original = source.toSubsetSnapshot({1, 4});
        original.speakerLights = imported.speakerLights;
        expect(cromOf(imported) == cromOf(original),
               "they are the same frames as in the source project, for libserum");
    }
    expect(target.frameMeta(3).id == source.frameMeta(1).id &&
               target.frameMeta(3).author == "Kraken",
           "they keep their IDs and who changed them");
    expect(document::fixedName(target.editorMetadata().maskNames, 0, document::kMaskNameBytes) ==
               "Score",
           "the mask keeps its name");
    expect(target.sprite(2).name != source.sprite(0).name &&
               target.sprite(2).name.rfind(source.sprite(1).name, 0) == 0 &&
               (target.sprite(1).name == source.sprite(1).name ||
                target.sprite(2).name == source.sprite(0).name + " 2" ||
                target.sprite(1).name == source.sprite(0).name + " 2"),
           "a sprite whose name is taken gets a number");
    expect(target.speakerLights().effects.size() == 1 &&
               target.frameLights(4)[0].effectId == target.speakerLights().effects[0].id,
           "the light effect comes along");

    command->revert(target);
    expect(target.frameCount() == 3 && target.comparisonMaskCount() == 0 &&
               target.spriteCount() == untouched.spriteCount() &&
               target.speakerLights().effects.empty(),
           "undo takes it all back");
    command->apply(target);
    expect(target.frameCount() == 5 && target.frameMeta(3).id == source.frameMeta(1).id,
           "redo puts it back");
    const std::vector<edit::RenumberedRecords> renumbered = command->renumberedRecords();
    expect(renumbered.size() == 4 && renumbered[0].firstIndex == 3, "it names what it adds");

    // The same package again: every frame is known now, by its ID.
    const edit::PackageImportPlan again = edit::planPackageImport(target, package);
    expect(again.knownFrameCount() == 2 && again.knownFrames[0] == std::size_t{3} &&
               again.maskTargets[0] == std::size_t{0} && again.spriteTargets[1].has_value(),
           "a second import knows the frames and finds the mask and sprites");
    expect(edit::importPackage(target, package, KnownFrames::Skip) == nullptr &&
               target.frameCount() == 5,
           "skipping the known frames leaves nothing to do");
    const auto copies = edit::importPackage(target, package, KnownFrames::Add);
    expect(copies && target.frameCount() == 7 && target.comparisonMaskCount() == 1 &&
               target.frameMeta(5).id != target.frameMeta(3).id,
           "adding them again makes copies with IDs of their own, using what is there");
}

void testUpdateKnownFrames()
{
    ProjectDocument source = makeSource();
    ProjectDocument target = source;
    // The source has a newer frame 1: another duration and another color.
    source.setFrameDurationMs(1, 1234);
    ProjectSnapshot package = edit::makePackage(source, {1});
    package.frames[0].colorizedSd.pixels[5] = 0xf800;
    // A frame of another project, the same ROM frame under the same mask, also counts as
    // known: by what libserum identifies it by.
    package.frames[0].meta.id = document::ComponentId::generate();

    edit::UndoHistories histories;
    const edit::PackageImportPlan plan = edit::planPackageImport(target, package);
    expect(plan.knownFrames.size() == 1 && plan.knownFrames[0] == std::size_t{1},
           "the frame is known by its ROM frame under its mask");
    const document::ComponentId id = target.frameMeta(1).id;
    const auto command = edit::importPackage(target, package, KnownFrames::Update);
    expect(command && command->counts().updatedFrames == 1 && command->counts().addedFrames == 0 &&
               target.frameCount() == source.frameCount() &&
               target.frameDurationMs(1) == 1234 &&
               target.frameSnapshotOf(1).colorizedSd.pixels[5] == 0xf800 &&
               target.frameMeta(1).id == id,
           "the frame is updated in its place and keeps its ID");
    expect(command->counts().addedMasks == 0 && command->counts().addedBackgrounds == 0,
           "with the project's own mask and background");
    bool replacesItsPixels = false;
    for (const edit::ReplacedData& replaced : command->replacedData()) {
        replacesItsPixels = replacesItsPixels ||
                            (replaced.kind == edit::EditKind::FramePixels && replaced.index == 1);
    }
    expect(replacesItsPixels, "the frame's own histories are dropped");
    command->revert(target);
    expect(target.frameDurationMs(1) != 1234, "undo brings the frame back");
}

void testRefusals()
{
    const ProjectDocument source = makeSource();
    const ProjectSnapshot package = edit::makePackage(source, {1});
    ProjectSnapshot other = fixtures::makeProject(render::PlaneLayout::NativeOriginal, 2);
    ProjectDocument wide = ProjectDocument::fromSnapshot(std::move(other));
    expect(!edit::planPackageImport(wide, package).problem.empty(),
           "a package of another frame size is refused");
    bool thrown = false;
    try {
        (void)edit::importPackage(wide, package, KnownFrames::Add);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    expect(thrown && wide.frameCount() == 2, "and nothing changes");

    ProjectSnapshot colors = fixtures::makeProject(render::PlaneLayout::Ordinary, 2);
    colors.colorCount = 16;
    const ProjectDocument sixteenShades = ProjectDocument::fromSnapshot(std::move(colors));
    expect(sixteenShades.colorCount() != source.colorCount() &&
               edit::planPackageImport(sixteenShades, package).problem.find("shades") !=
                   std::string::npos,
           "a package of other shades is refused");
}

}  // namespace

void runPackageTests()
{
    testPackageContent();
    testPackageFile();
    testImportIntoAnotherProject();
    testUpdateKnownFrames();
    testRefusals();
}
