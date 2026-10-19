#pragma once

#include <cstdint>
#include <optional>
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
constexpr uint8_t kSceneKnownOptions = 63;
// The IDs a scene made in the editor gets: the trigger range kept for scenes (see
// Triggers.h). Scenes of an imported pup.csv keep theirs.
constexpr uint16_t kFirstSceneId = 50000;
constexpr uint16_t kLastSceneId = 61999;

// A scene of the colorization: libserum plays its frames when the PUP trigger sceneId fires.
// It is what one line of a "<rom>.pup.csv" says, with that file's columns:
// sceneId, frameCount, durationMs, interruptable, immediateStart, repeat, frameGroups,
// random, autoStartSeconds, options.
struct SceneDefinition {
    uint16_t sceneId = 0;
    // Frames per group.
    uint16_t frameCount = 1;
    // How long each frame shows (33 ms: about 30 frames a second).
    uint16_t durationMs = 33;
    bool interruptable = false;
    bool immediateStart = false;
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

    [[nodiscard]] uint32_t totalFrames() const
    {
        return static_cast<uint32_t>(frameCount) * (frameGroups == 0 ? 1u : frameGroups);
    }

    friend bool operator==(const SceneDefinition& a, const SceneDefinition& b)
    {
        return a.sceneId == b.sceneId && a.frameCount == b.frameCount &&
               a.durationMs == b.durationMs && a.interruptable == b.interruptable &&
               a.immediateStart == b.immediateStart && a.repeat == b.repeat &&
               a.frameGroups == b.frameGroups && a.random == b.random &&
               a.autoStartSeconds == b.autoStartSeconds && a.options == b.options;
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

    [[nodiscard]] const SceneDefinition* scene(uint16_t sceneId) const
    {
        for (const SceneDefinition& definition : scenes) {
            if (definition.sceneId == sceneId) {
                return &definition;
            }
        }
        return nullptr;
    }

    friend bool operator==(const ColorizationSettings& a, const ColorizationSettings& b)
    {
        return a.scenes == b.scenes && a.scalingAlgorithm == b.scalingAlgorithm &&
               a.shadowOffset == b.shadowOffset;
    }
    friend bool operator!=(const ColorizationSettings& a, const ColorizationSettings& b)
    {
        return !(a == b);
    }
};

}  // namespace colorizing::document
