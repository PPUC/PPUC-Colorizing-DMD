#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "document/Scene.h"
#include "render/ProjectGeometry.h"

namespace colorizing::runtime {

// Whether libserum plays scenes in a project of this layout: only one with a ROM frame of
// 128x32.
[[nodiscard]] bool supportsScenes(const render::ProjectGeometry& geometry);

// The ROM frame libserum's scene generator makes for a frame of the scene, as libdmdutil's
// scene tool dumps it and libserum identifies it: "PUP SCENE ID", "FRAME GROUP" and
// "FRAME NUMBER" with the numbers, 128x32, in the shades of colorCount colors (4 or 16).
// Throws std::out_of_range for a group or frame the scene does not have.
[[nodiscard]] document::IndexedFrame sceneMarkerFrame(const document::SceneDefinition& scene,
                                                      const document::SceneFrameRef& frame,
                                                      uint32_t colorCount);

// The scenes of a "<rom>.pup.csv", read as libserum's SceneGenerator::parseCSV() reads them:
// lines that are empty, start with '#' or have fewer than three numbers are skipped. Nothing
// if the file cannot be read.
[[nodiscard]] std::optional<std::vector<document::SceneDefinition>> readSceneCsv(
    const std::filesystem::path& path);
// The settings of a "scaling.txt", read as libserum reads them, over defaults.
[[nodiscard]] std::optional<document::ColorizationSettings> readScalingFile(
    const std::filesystem::path& path,
    document::ColorizationSettings defaults = {});

// Links each frame whose original is the marker of a frame of one of the scenes to it
// (ProjectDocument::sceneFrame()), and unlinks the others. Returns the number linked.
std::size_t linkSceneFrames(document::ProjectDocument& document);

struct SidecarImport {
    // The files that were read, by name.
    std::vector<std::string> files;
    std::size_t scenes = 0;
    std::size_t linkedFrames = 0;
    // Frames whose PUP trigger is the ID of an imported scene, which now start it as well
    // (document::linkPupTriggerScenes()).
    std::size_t startingFrames = 0;
};

// Gives a project without colorization settings (one of the Win32 editor) the settings
// libserum would use for it from the files beside its cROM in `directory`: the scenes of
// "<rom>.pup.csv" (frames linked as linkSceneFrames() does) and "scaling.txt", names in any
// case, defaults for what is missing. The files themselves are left as they are. Does
// nothing to a project that has settings.
SidecarImport importColorizationSidecars(document::ProjectDocument& document,
                                         const std::filesystem::path& directory);

// A frame that starts a background scene shows the scene through its background mask without
// a background of its own (ProjectDocument::backgroundScene()). libserum shows a scene only
// where the frame has a background, which it takes for a placeholder while the scene plays:
// every such frame with a background mask gets one in the snapshot given to libserum, a black
// background added for this, with a black HD image where a frame using it has HD (libserum
// takes a frame's HD picture only together with its background's). Returns the number of
// frames that got it.
std::size_t addSceneBackgroundPlaceholders(document::ProjectSnapshot& snapshot);
// Takes the background off every frame that starts a background scene: the placeholder the
// editors before needed for it (see above). Only where libserum never shows it: the scene
// loops, or it keeps its last frame as the background when it has played (no finish mode,
// no timed hold). After a scene that ends black or on the frame before, libserum shows the
// background's image; that one stays. Returns the number of frames.
std::size_t removeSceneBackgroundPlaceholders(document::ProjectDocument& document);

}  // namespace colorizing::runtime
