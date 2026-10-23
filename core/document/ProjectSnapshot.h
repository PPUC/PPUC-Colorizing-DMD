#pragma once

#include <optional>
#include <string>
#include <vector>

#include "document/HelperLine.h"
#include "document/ImageZone.h"
#include "document/LibserumTarget.h"
#include "document/ProjectDocument.h"
#include "document/TeamState.h"
#include "render/ProjectGeometry.h"

namespace colorizing::document {

constexpr uint32_t kCurrentCromRevision = 20;
constexpr uint32_t kOldestWritableCromRevision = 19;

struct ProjectFrameSnapshot {
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    uint8_t shapeComparisonMode = 0;
    uint8_t comparisonMaskId = 0;
    std::optional<FrameDynamicData> dynamicData;
    std::optional<FrameBackgroundData> backgroundData;
    std::vector<FrameSpriteReference> spriteReferences;
    uint32_t triggerId = 0;
    uint32_t durationMs = 0;
    CompactArray<uint16_t> colorRotationsSd;
    CompactArray<uint16_t> colorRotationsHd;
    std::optional<Rgb565Frame> inactiveHd;
    // .cRPX only.
    std::optional<SceneFrameRef> scene;
    std::vector<std::string> tags;
    LightActions lights{};
    uint32_t originalOnlyDynamicSets = 0;
    ComponentMeta meta;
};

struct ProjectSnapshot {
    std::string romName;
    std::vector<ProjectFrameSnapshot> frames;
    std::vector<IndexedFrame> comparisonMasks;
    std::vector<BackgroundRecord> backgrounds;
    std::vector<SpriteRecord> sprites;
    std::vector<PaletteRecord> palettes;
    std::vector<UnsupportedLegacyPayload> unsupportedPayloads;
    // Number of colors of the original ROM frames (4 or 16).
    uint32_t colorCount = 4;
    // cROM header revision (header length in 32-bit words) the project was loaded with, so a
    // save does not silently move it to a revision an older libserum cannot read.
    uint32_t cromRevision = kCurrentCromRevision;
    // The libserum the project is made for (.cRPX; a cROM of format version 1 is for 2.9).
    // Every frame's sprite slots are that target's spritesPerFrame.
    LibserumTarget libserumTarget = LibserumTarget::Libserum28;
    // Sizes of the stored planes and which of them is the ROM frame's. Everything named ...Sd
    // has the size of the original plane and everything named ...Hd that of the extra plane;
    // original frames and comparison masks have the size of the ROM frame. A carried plane
    // is written back unchanged; a discarded one holds "no extra data", see
    // discardExtraPlane().
    render::ProjectGeometry geometry;
    EditorMetadata editorMetadata = makeDefaultEditorMetadata();
    // The editor's helper lines (.cRPX only).
    std::vector<HelperLine> helperLines;
    // The Images window's saved zones (.cRPX only).
    std::vector<SavedImageZone> imageZones;
    // The cROM's settings block (.cRPX, and the cROM written from it); nothing for a project
    // of the Win32 editor.
    std::optional<ColorizationSettings> colorizationSettings;
    // The speaker lights (.cRPX, and the cROM's settings block written from them).
    SpeakerLightSettings speakerLights;
    // The license the colorization is published under (.cRPX).
    ProjectLicense license;
    // What discardExtraPlane() dropped (not written to the files).
    DiscardedExtraPlane discardedExtraPlane;
    // What this copy of a team project keeps about the team; nothing for a solo project.
    std::optional<TeamState> team;
    // The project's own ID (ComponentId.h), given once; null when not known (a project of the
    // legacy files), then the document gives it one.
    ComponentId projectId;
    // The change information of the comparison masks, in their order (a mask is a picture
    // only); shorter or empty when not known, then the masks get new ids.
    std::vector<ComponentMeta> comparisonMaskMeta;
};

[[nodiscard]] Size originalFrameSize(const ProjectSnapshot& snapshot);

// Fills in the optional per-sprite dynamic data with its "no dynamic content" defaults.
void normalizeSpriteRecord(SpriteRecord& sprite, uint32_t colorCount);

// Makes the extra plane "no extra data": no frame, background or sprite has it, and every
// extra-plane array holds what the Win32 editor gives an item without extra resolution
// (colors, dynamic colors, shadows and rotations 0, dynamic and sprite masks 255, background
// masks 1). Sizes and the header stay as the geometry says. Adds the pictures it drops
// (extraPlanePictures()) to discardedExtraPlane.
void discardExtraPlane(ProjectSnapshot& snapshot);
// The frames and backgrounds with a picture on the extra plane and the sprites with extra
// resolution data.
[[nodiscard]] DiscardedExtraPlane extraPlanePictures(const ProjectSnapshot& snapshot);

// A frame nobody has worked on, as the Win32 editor makes it from a frame of a dump
// (CopyTXTFrames2Frame): its picture on the native plane is the original in the plain
// original colors, it has no comparison mask, no shape mode, no dynamic zone, no background
// (with the background masks set), no sprite (each slot's bounds are the whole frame), no
// trigger and no color rotation. An ordinary project gets no extra-plane picture; a
// project with 256x64 originals gets the 256x64 picture the runtime needs and, on its
// unused 128x32 plane, every second pixel of it. Throws std::invalid_argument for an
// original that has not the size of the ROM frame.
[[nodiscard]] ProjectFrameSnapshot makeUncolorizedFrameSnapshot(
    const render::ProjectGeometry& geometry,
    uint32_t colorCount,
    IndexedFrame original,
    uint32_t durationMs,
    std::size_t spritesPerFrame = kLegacySpritesPerFrame);

[[nodiscard]] bool isValidProjectSnapshot(const ProjectSnapshot& snapshot,
                                          std::string* errorMessage = nullptr);

}  // namespace colorizing::document
