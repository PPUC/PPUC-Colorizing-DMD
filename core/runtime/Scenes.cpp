#include "runtime/Scenes.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "document/Triggers.h"
#include "runtime/CromcExport.h"
// libserum's headers, without the warnings of this project's settings.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "SceneGenerator.h"
#include "serum.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace colorizing::runtime {

namespace {

static_assert(document::kSceneBlackWhenFinished == FLAG_SCENE_BLACK_WHEN_FINISHED);
static_assert(document::kSceneShowPreviousFrameWhenFinished ==
              FLAG_SCENE_SHOW_PREVIOUS_FRAME_WHEN_FINISHED);
static_assert(document::kSceneFinishModeMask == FLAG_SCENE_FINISH_MODE_MASK);
static_assert(document::kSceneAsBackground == FLAG_SCENE_AS_BACKGROUND);
static_assert(document::kSceneOnlyDynamicContent == FLAG_SCENE_ONLY_DYNAMIC_CONTENT);
static_assert(document::kSceneResumeIfRetriggered == FLAG_SCENE_RESUME_IF_RETRIGGERED);
static_assert(document::kSceneReplaceDynamicBlack == FLAG_SCENE_REPLACE_DYNAMIC_BLACK);
static_assert(document::kScalingLineDoubling == SERUM_SCALING_LINE_DOUBLING);
static_assert(document::kScalingScale2x == SERUM_SCALING_SCALE2X);
static_assert(document::kShadowOffsetNative == SERUM_SHADOW_OFFSET_NATIVE);
static_assert(document::kShadowOffsetProportional == SERUM_SHADOW_OFFSET_PROPORTIONAL);

constexpr int kMarkerWidth = 128;
constexpr int kMarkerHeight = 32;

SceneData sceneData(const document::SceneDefinition& scene)
{
    SceneData data;
    data.sceneId = scene.sceneId;
    data.frameCount = scene.frameCount;
    data.durationPerFrame = scene.durationMs;
    data.interruptable = scene.interruptable;
    data.immediateStart = scene.immediateStart;
    data.repeat = scene.repeat;
    data.frameGroups = scene.frameGroups == 0 ? 1 : scene.frameGroups;
    data.random = scene.random;
    data.autoStart = scene.autoStartSeconds;
    data.sceneOptions = scene.options;
    return data;
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// scaling.txt's algorithm names (libserum's ParseScalingAlgorithm()).
std::optional<uint8_t> scalingAlgorithm(const std::string& value)
{
    if (value == "line-doubling" || value == "linedoubling" || value == "linedouble") {
        return document::kScalingLineDoubling;
    }
    if (value == "scale2x") {
        return document::kScalingScale2x;
    }
    return std::nullopt;
}

// The frame's original as the marker bytes, when it has the marker's size.
std::optional<std::string> markerKey(const document::IndexedFrame& frame)
{
    if (frame.size.width != kMarkerWidth || frame.size.height != kMarkerHeight) {
        return std::nullopt;
    }
    std::string key(frame.pixels.size(), '\0');
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<char>(frame.pixels[index]);
    }
    return key;
}

}  // namespace

bool supportsScenes(const render::ProjectGeometry& geometry)
{
    const render::Size rom = geometry.romFrameSize();
    return rom.width == kMarkerWidth && rom.height == kMarkerHeight;
}

document::IndexedFrame sceneMarkerFrame(const document::SceneDefinition& scene,
                                        const document::SceneFrameRef& frame,
                                        uint32_t colorCount)
{
    const SceneData data = sceneData(scene);
    if (frame.sceneId != scene.sceneId || frame.group < 1 || frame.group > data.frameGroups ||
        frame.frameIndex >= scene.frameCount) {
        throw std::out_of_range("the scene has no such frame");
    }
    SceneGenerator generator;
    generator.setSceneData({data});
    generator.setDepth(colorCount == 16 ? 4 : 2);
    std::vector<uint8_t> pixels(static_cast<std::size_t>(kMarkerWidth) * kMarkerHeight, 0);
    // The generator takes the group at a scene's first frame and keeps it for the others, as
    // libserum generates a group's frames in order.
    if (frame.frameIndex > 0) {
        (void)generator.generateFrame(frame.sceneId, 0, pixels.data(), frame.group, true);
    }
    if (generator.generateFrame(frame.sceneId, frame.frameIndex, pixels.data(), frame.group,
                                true) != 0xffff) {
        throw std::out_of_range("the scene has no such frame");
    }
    document::IndexedFrame marker;
    marker.size = {kMarkerWidth, kMarkerHeight};
    marker.pixels = document::CompactArray<uint8_t>(std::move(pixels));
    return marker;
}

std::optional<std::vector<document::SceneDefinition>> readSceneCsv(
    const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<document::SceneDefinition> scenes;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> row;
        std::istringstream cells(line);
        std::string cell;
        while (std::getline(cells, cell, ',')) {
            row.push_back(cell);
        }
        if (row.size() < 3) {
            continue;
        }
        try {
            document::SceneDefinition scene;
            scene.sceneId = static_cast<uint16_t>(std::stoi(row[0]));
            scene.frameCount = static_cast<uint16_t>(std::stoi(row[1]));
            scene.durationMs = static_cast<uint16_t>(std::stoi(row[2]));
            if (row.size() >= 4) scene.interruptable = std::stoi(row[3]) == 1;
            if (row.size() >= 5) scene.immediateStart = std::stoi(row[4]) == 1;
            if (row.size() >= 6) scene.repeat = static_cast<uint8_t>(std::stoi(row[5]));
            if (row.size() >= 7) {
                const uint8_t groups = static_cast<uint8_t>(std::stoi(row[6]));
                scene.frameGroups = groups == 0 ? 1 : groups;
            }
            if (row.size() >= 8) scene.random = std::stoi(row[7]) == 1;
            if (row.size() >= 9) {
                const int autoStart = std::stoi(row[8]);
                scene.autoStartSeconds =
                    static_cast<uint8_t>(std::clamp(autoStart, 0, 255));
            }
            if (row.size() >= 10) scene.options = static_cast<uint8_t>(std::stoi(row[9]));
            scenes.push_back(scene);
        } catch (const std::exception&) {
            continue;
        }
    }
    return scenes;
}

std::optional<document::ColorizationSettings> readScalingFile(
    const std::filesystem::path& path,
    document::ColorizationSettings settings)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        const std::string text = trim(line);
        if (text.empty()) {
            continue;
        }
        const std::size_t colon = text.find(':');
        if (colon == std::string::npos) {
            if (const auto algorithm = scalingAlgorithm(lower(text))) {
                settings.scalingAlgorithm = *algorithm;
            }
            continue;
        }
        const std::string key = lower(trim(text.substr(0, colon)));
        const std::string value = lower(trim(text.substr(colon + 1)));
        if (key == "shadow-offset" || key == "shadowoffset") {
            if (value == "native" || value == "1") {
                settings.shadowOffset = document::kShadowOffsetNative;
            } else if (value == "proportional" || value == "2") {
                settings.shadowOffset = document::kShadowOffsetProportional;
            }
        } else if (key == "scaling" || key == "algorithm") {
            if (const auto algorithm = scalingAlgorithm(value)) {
                settings.scalingAlgorithm = *algorithm;
            }
        }
    }
    return settings;
}

std::size_t linkSceneFrames(document::ProjectDocument& document)
{
    std::map<std::string, document::SceneFrameRef> markers;
    const auto& settings = document.colorizationSettings();
    if (settings && supportsScenes(document.geometry())) {
        for (const document::SceneDefinition& scene : settings->scenes) {
            const uint8_t groups = scene.frameGroups == 0 ? 1 : scene.frameGroups;
            for (uint8_t group = 1; group <= groups; ++group) {
                for (uint16_t index = 0; index < scene.frameCount; ++index) {
                    const document::SceneFrameRef frame{scene.sceneId, group, index};
                    const auto key =
                        markerKey(sceneMarkerFrame(scene, frame, document.colorCount()));
                    // The first scene of an ID is the one libserum plays.
                    if (key) {
                        markers.emplace(*key, frame);
                    }
                }
            }
        }
    }
    std::size_t linked = 0;
    for (std::size_t frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
        std::optional<document::SceneFrameRef> scene;
        if (const auto key = markerKey(document.originalFrame(frameIndex))) {
            const auto found = markers.find(*key);
            if (found != markers.end()) {
                scene = found->second;
                ++linked;
            }
        }
        document.setSceneFrame(frameIndex, scene);
    }
    return linked;
}

SidecarImport importColorizationSidecars(document::ProjectDocument& document,
                                         const std::filesystem::path& directory)
{
    SidecarImport result;
    if (document.colorizationSettings()) {
        return result;
    }
    document::ColorizationSettings settings;
    if (const auto csv = findCaseInsensitive(directory, document.romName() + ".pup.csv")) {
        if (auto scenes = readSceneCsv(*csv)) {
            settings.scenes = std::move(*scenes);
            result.files.push_back(csv->filename().string());
        }
    }
    if (const auto scaling = findCaseInsensitive(directory, "scaling.txt")) {
        if (auto read = readScalingFile(*scaling, settings)) {
            settings = std::move(*read);
            result.files.push_back(scaling->filename().string());
        }
    }
    result.scenes = settings.scenes.size();
    document.setColorizationSettings(std::move(settings));
    result.linkedFrames = linkSceneFrames(document);
    // The frames came with one trigger ID each: one that is a scene's ID starts it, as before.
    result.startingFrames = document::linkPupTriggerScenes(document);
    return result;
}

std::size_t addSceneBackgroundPlaceholders(document::ProjectSnapshot& snapshot)
{
    if (!snapshot.colorizationSettings || !supportsScenes(snapshot.geometry) ||
        snapshot.backgrounds.size() >= document::kMaxBackgrounds) {
        return 0;
    }
    const auto hasMask = [](const document::IndexedFrame& mask) {
        return std::any_of(mask.pixels.begin(), mask.pixels.end(),
                           [](uint8_t value) { return value != 0; });
    };
    const auto placeholder = static_cast<uint16_t>(snapshot.backgrounds.size());
    std::size_t given = 0;
    bool withHd = false;
    for (document::ProjectFrameSnapshot& frame : snapshot.frames) {
        if (!frame.backgroundData ||
            frame.backgroundData->backgroundId != document::kNoBackgroundId ||
            frame.sceneTriggerId > 0xffff) {
            continue;
        }
        const document::SceneDefinition* scene =
            snapshot.colorizationSettings->scene(static_cast<uint16_t>(frame.sceneTriggerId));
        if (!scene || (scene->options & document::kSceneAsBackground) == 0 ||
            (!hasMask(frame.backgroundData->maskSd) && !hasMask(frame.backgroundData->maskHd))) {
            continue;
        }
        frame.backgroundData->backgroundId = placeholder;
        withHd = withHd || frame.colorizedHd.has_value();
        ++given;
    }
    if (given > 0) {
        document::BackgroundRecord background;
        background.colorizedSd = document::makeRgb565Frame(
            snapshot.geometry.planeSize(render::Resolution::Sd));
        if (withHd) {
            background.colorizedHd = document::makeRgb565Frame(
                snapshot.geometry.planeSize(render::Resolution::Hd));
        }
        snapshot.backgrounds.push_back(std::move(background));
    }
    return given;
}

std::size_t removeSceneBackgroundPlaceholders(document::ProjectDocument& document)
{
    std::size_t removed = 0;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        const std::optional<document::FrameBackgroundData>& data = document.backgroundData(frame);
        const document::SceneDefinition* scene = document.backgroundScene(frame);
        if (!scene || !data || data->backgroundId == document::kNoBackgroundId) {
            continue;
        }
        const bool loops = scene->repeat == 1;
        const bool keepsLastFrame = (scene->options & document::kSceneFinishModeMask) == 0 &&
                                    !(scene->autoStartSeconds > 0 && !scene->interruptable);
        if (loops || keepsLastFrame) {
            document.setBackgroundId(frame, document::kNoBackgroundId);
            ++removed;
        }
    }
    return removed;
}

}  // namespace colorizing::runtime
