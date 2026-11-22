#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace colorizing::document {

// What libserum does when a scene has played (SceneDefinition::options, the two lowest bits:
// at most one of the two).
constexpr uint8_t kSceneBlackWhenFinished = 1;
constexpr uint8_t kSceneShowPreviousFrameWhenFinished = 2;
constexpr uint8_t kSceneFinishModeMask = 3;
// The other options.
constexpr uint8_t kSceneAsBackground = 4;
constexpr uint8_t kSceneOnlyDynamicContent = 8;
constexpr uint8_t kSceneResumeIfRetriggered = 16;
constexpr uint8_t kSceneReplaceDynamicBlack = 32;
// Forward to the last frame and back to the second; the next run starts at the first, the last
// run ends on it (libserum 2.99.7).
constexpr uint8_t kSceneBackAndForth = 64;
constexpr uint8_t kSceneKnownOptions = 127;
// When libserum starts a scene whose trigger a frame fires (SceneDefinition::startMode).
// After the first frame: the triggering frame shows for the scene's duration first.
constexpr uint8_t kSceneStartAfterFirstFrame = 0;
// Immediately: the scene's first frame replaces the triggering frame.
constexpr uint8_t kSceneStartImmediately = 1;
// With the next frame: the triggering frame shows until the game sends its next frame, however
// long that takes, and the scene's first frame replaces that one (libserum 2.99.21). A scene
// that can be interrupted starts only on a frame that is not a frame of the project; such a
// frame drops it. Repeats of the triggering frame keep it waiting.
constexpr uint8_t kSceneStartWithNextFrame = 2;
// The IDs a scene made in the editor gets: the trigger range kept for scenes (see
// Triggers.h). Scenes of an imported pup.csv keep theirs.
constexpr uint16_t kFirstSceneId = 50000;
constexpr uint16_t kLastSceneId = 61999;
// The speed of a scene variant in percent of its base's (SceneDefinition::speedPercent): 100
// plays as fast as the base, 200 twice as fast, 50 at half the speed.
constexpr uint16_t kSceneSpeedPercentDefault = 100;
constexpr uint16_t kMinSceneSpeedPercent = 10;
constexpr uint16_t kMaxSceneSpeedPercent = 1000;

// How long a frame that shows durationMs in the base shows at speedPercent, as libserum gets
// it: at least 1 ms, at most what a scene duration can hold.
[[nodiscard]] inline uint16_t sceneDurationAtSpeed(uint32_t durationMs, uint16_t speedPercent)
{
    const uint32_t speed = std::max<uint32_t>(speedPercent, 1);
    const uint64_t scaled = (static_cast<uint64_t>(durationMs) * 100 + speed / 2) / speed;
    return static_cast<uint16_t>(std::clamp<uint64_t>(scaled, 1, UINT16_MAX));
}

// The speed that makes a scene of baseDurationMs play at durationMs per frame, within
// kMinSceneSpeedPercent and kMaxSceneSpeedPercent: what a variant of a project from before
// speeds gets for its fixed duration.
[[nodiscard]] inline uint16_t sceneSpeedForDuration(uint32_t baseDurationMs, uint32_t durationMs)
{
    const uint64_t duration = std::max<uint32_t>(durationMs, 1);
    const uint64_t speed = (static_cast<uint64_t>(baseDurationMs) * 100 + duration / 2) / duration;
    return static_cast<uint16_t>(
        std::clamp<uint64_t>(speed, kMinSceneSpeedPercent, kMaxSceneSpeedPercent));
}

// A scene of the colorization: libserum plays its frames when the PUP trigger sceneId fires.
// It is what one line of a "<rom>.pup.csv" says, with that file's columns:
// sceneId, frameCount, durationMs, interruptable, startMode, repeat, frameGroups,
// random, autoStartSeconds, options.
struct SceneDefinition {
    uint16_t sceneId = 0;
    // Frames per group.
    uint16_t frameCount = 1;
    // How long each frame shows (33 ms: about 30 frames a second), unless the frame has a
    // duration of its own: a frame of the scene whose duration (FrameRecord::durationMs)
    // differs from it shows for that (libserum 2.99.20). A new frame of the scene gets it. For
    // a variant it follows from the base's and the speed (normalizeSceneVariants()).
    uint16_t durationMs = 33;
    bool interruptable = false;
    // kSceneStart...
    uint8_t startMode = kSceneStartAfterFirstFrame;
    // 0 plays once, 1 loops, 2 and more play that many times.
    uint8_t repeat = 0;
    // Sets of frameCount frames; libserum plays one of them each time (at least 1).
    uint8_t frameGroups = 1;
    // Pick the group at random instead of in turn.
    bool random = false;
    // Start the scene after this many seconds without a new frame. For a scene that cannot
    // be interrupted and keeps its last frame when it has played (no finish mode), it is how
    // long that last frame stays instead.
    uint8_t autoStartSeconds = 0;
    // kScene... flags.
    uint8_t options = 0;
    // The author's name for the scene (the project only; a pup.csv has none).
    std::string name;
    // A variant plays the frames of the scene baseSceneId with its own settings (libserum
    // 2.99.10); it has no frames of its own, and its frameCount and frameGroups are the base's.
    // The base is a scene that is no variant.
    std::optional<uint16_t> baseSceneId;
    // A variant plays its base's frames this much faster (kSceneSpeedPercentDefault is the
    // base's speed): each frame, its own duration included, shows for its time in the base
    // divided by the speed (sceneDurationAtSpeed()). Only a variant has one.
    uint16_t speedPercent = kSceneSpeedPercentDefault;
    // The scene that starts when this one has played, in place of its finish mode (libserum
    // 2.99.18): its last frame shows for its duration, then the other scene starts. Another
    // scene of the project; a chain may come back to a scene it started from.
    std::optional<uint16_t> nextSceneId;

    [[nodiscard]] bool isVariant() const { return baseSceneId.has_value(); }

    [[nodiscard]] uint32_t totalFrames() const
    {
        return static_cast<uint32_t>(frameCount) * (frameGroups == 0 ? 1u : frameGroups);
    }

    friend bool operator==(const SceneDefinition& a, const SceneDefinition& b)
    {
        return a.sceneId == b.sceneId && a.frameCount == b.frameCount &&
               a.durationMs == b.durationMs && a.interruptable == b.interruptable &&
               a.startMode == b.startMode && a.repeat == b.repeat &&
               a.frameGroups == b.frameGroups && a.random == b.random &&
               a.autoStartSeconds == b.autoStartSeconds && a.options == b.options &&
               a.name == b.name && a.baseSceneId == b.baseSceneId &&
               a.speedPercent == b.speedPercent && a.nextSceneId == b.nextSceneId;
    }
    friend bool operator!=(const SceneDefinition& a, const SceneDefinition& b)
    {
        return !(a == b);
    }
};

// Which frame of a scene a project frame is: its original is the scene marker libserum
// generates for it. group counts from 1, frameIndex from 0.
struct SceneFrameRef {
    uint16_t sceneId = 0;
    uint8_t group = 1;
    uint16_t frameIndex = 0;

    friend bool operator==(const SceneFrameRef& a, const SceneFrameRef& b)
    {
        return a.sceneId == b.sceneId && a.group == b.group && a.frameIndex == b.frameIndex;
    }
    friend bool operator!=(const SceneFrameRef& a, const SceneFrameRef& b) { return !(a == b); }
};

// libserum's upscaling algorithms (SERUM_SCALING_...) and shadow offsets
// (SERUM_SHADOW_OFFSET_...), with its values.
constexpr uint8_t kScalingLineDoubling = 1;
constexpr uint8_t kScalingScale2x = 2;
constexpr uint8_t kShadowOffsetNative = 0;
constexpr uint8_t kShadowOffsetProportional = 1;

// What a colorization carries for libserum besides its frames: its scenes and its scaling
// settings, written into the cROM's settings block. A project that has them needs no
// "<rom>.pup.csv" or "scaling.txt", and libserum ignores those beside it.
struct ColorizationSettings {
    std::vector<SceneDefinition> scenes;
    uint8_t scalingAlgorithm = kScalingScale2x;
    uint8_t shadowOffset = kShadowOffsetNative;
    // Whether the editor signs the exported cROMc with the author's key (app/Signing.h). On
    // unless the author turns it off for the project; a project without colorization settings
    // is signed as well.
    bool signCromc = true;

    [[nodiscard]] const SceneDefinition* scene(uint16_t sceneId) const
    {
        for (const SceneDefinition& definition : scenes) {
            if (definition.sceneId == sceneId) {
                return &definition;
            }
        }
        return nullptr;
    }
    // The scene whose frames sceneId plays: its base for a variant, else itself.
    [[nodiscard]] uint16_t framesSceneId(uint16_t sceneId) const
    {
        const SceneDefinition* definition = scene(sceneId);
        return definition && definition->baseSceneId ? *definition->baseSceneId : sceneId;
    }
    // The variants of the scene, in the order of scenes.
    [[nodiscard]] std::vector<uint16_t> variantsOf(uint16_t sceneId) const
    {
        std::vector<uint16_t> variants;
        for (const SceneDefinition& definition : scenes) {
            if (definition.baseSceneId == sceneId) {
                variants.push_back(definition.sceneId);
            }
        }
        return variants;
    }

    friend bool operator==(const ColorizationSettings& a, const ColorizationSettings& b)
    {
        return a.scenes == b.scenes && a.scalingAlgorithm == b.scalingAlgorithm &&
               a.shadowOffset == b.shadowOffset && a.signCromc == b.signCromc;
    }
    friend bool operator!=(const ColorizationSettings& a, const ColorizationSettings& b)
    {
        return !(a == b);
    }
};

// Gives each variant its base's frame count and groups, and the duration of its base at its
// speed (kept within kMinSceneSpeedPercent and kMaxSceneSpeedPercent); a variant whose base is
// missing, is itself or is a variant plays frames of its own instead. A scene that is no
// variant has no speed of its own. A scene that is to follow a scene that is missing or itself
// follows none.
inline void normalizeSceneVariants(ColorizationSettings& settings)
{
    for (SceneDefinition& scene : settings.scenes) {
        if (scene.nextSceneId &&
            (*scene.nextSceneId == scene.sceneId || !settings.scene(*scene.nextSceneId))) {
            scene.nextSceneId.reset();
        }
    }
    for (SceneDefinition& scene : settings.scenes) {
        if (!scene.baseSceneId) {
            continue;
        }
        const SceneDefinition* base = settings.scene(*scene.baseSceneId);
        if (!base || base->sceneId == scene.sceneId || base->baseSceneId) {
            scene.baseSceneId.reset();
            continue;
        }
        scene.frameCount = base->frameCount;
        scene.frameGroups = base->frameGroups;
        scene.speedPercent =
            std::clamp(scene.speedPercent, kMinSceneSpeedPercent, kMaxSceneSpeedPercent);
        scene.durationMs = sceneDurationAtSpeed(base->durationMs, scene.speedPercent);
    }
    for (SceneDefinition& scene : settings.scenes) {
        if (!scene.baseSceneId) {
            scene.speedPercent = kSceneSpeedPercentDefault;
        }
    }
}

}  // namespace colorizing::document
