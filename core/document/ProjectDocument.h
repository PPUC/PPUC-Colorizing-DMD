#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "document/ComponentId.h"
#include "document/CompactArray.h"
#include "document/EditorMetadata.h"
#include "document/HelperLine.h"
#include "document/ImageZone.h"
#include "document/ProjectLimits.h"
#include "document/Scene.h"
#include "document/ProjectLicense.h"
#include "document/DofEvents.h"
#include "document/SpeakerLights.h"
#include "document/TeamState.h"
#include "render/CanvasTypes.h"
#include "render/ProjectGeometry.h"

namespace colorizing::document {

using render::Resolution;
using render::Size;

// The pixels are a CompactArray: a plane that is one color throughout takes no memory of its
// own, and copies share the pixels until one of them is written.
struct Rgb565Frame {
    Size size;
    CompactArray<uint16_t> pixels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::size_t pixelCount() const;
};

struct IndexedFrame {
    Size size;
    CompactArray<uint8_t> pixels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::size_t pixelCount() const;
};

constexpr std::size_t kMaxDynamicSetsPerFrame = 32;
constexpr std::size_t kMaxDynamicSetsPerSprite = 9;
constexpr std::size_t kMaxSpriteWidth = 256;
constexpr std::size_t kMaxSpriteHeight = 64;
constexpr std::size_t kMaxSpriteDetectAreas = 4;
constexpr std::size_t kMaxColorRotations = 4;
constexpr std::size_t kMaxColorRotationLength = 64;
constexpr std::size_t kMaxColorRotationColors = kMaxColorRotationLength - 2;
constexpr std::size_t kPaletteCount = 256;
constexpr std::size_t kPaletteColorCount = 64;
// Trigger ID of a frame that triggers nothing, as stored in the cROM.
constexpr uint32_t kNoTriggerId = 0xffffffff;

// The names of the record fields follow the stored planes (render::Resolution): ...Sd is the
// original plane and ...Hd the extra plane, whatever their sizes are in the project's
// render::ProjectGeometry. originalSd is the ROM frame.
struct FrameDynamicData {
    IndexedFrame maskSd;
    IndexedFrame maskHd;
    CompactArray<uint16_t> colorsSd;
    CompactArray<uint16_t> colorsHd;
    CompactArray<uint8_t> shadowDirectionsSd;
    CompactArray<uint16_t> shadowColorsSd;
    CompactArray<uint8_t> shadowDirectionsHd;
    CompactArray<uint16_t> shadowColorsHd;
};

struct BackgroundRecord {
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    // HD pixels the Win32 editor left in the file although HD is switched off. They are kept so
    // an unedited project saves back unchanged, and dropped once HD is created or removed here.
    std::optional<Rgb565Frame> inactiveHd;
    ComponentMeta meta;
};

struct FrameBackgroundData {
    uint16_t backgroundId = 0xffff;
    IndexedFrame maskSd;
    IndexedFrame maskHd;
};

struct SpriteDetectionArea {
    uint16_t x = 0xffff;
    uint16_t y = 0xffff;
    uint16_t width = 0xffff;
    uint16_t height = 0xffff;
};

struct SpriteRecord {
    bool hasHd = false;
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    IndexedFrame maskHd;
    Rgb565Frame colorizedHd;
    std::vector<uint32_t> detectionDwords;
    std::vector<uint16_t> detectionDwordPositions;
    std::vector<SpriteDetectionArea> detectionAreas;
    std::string name;
    // Sprite dynamic content: 255 in the mask means "not dynamic". Empty containers are
    // normalized to that default when a document is built.
    IndexedFrame dynamicMaskSd;
    IndexedFrame dynamicMaskHd;
    CompactArray<uint16_t> dynamicColorsSd;
    CompactArray<uint16_t> dynamicColorsHd;
    uint8_t shapeMode = 0;
    ComponentMeta meta;
};

struct FrameSpriteReference {
    // kNoSpriteId for an empty slot (255 in a cROM of format version 0, read as such).
    uint16_t spriteId = 0xffff;
    uint16_t minX = 0;
    uint16_t minY = 0;
    uint16_t maxX = 0;
    uint16_t maxY = 0;
};

struct FrameRecord {
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    uint8_t shapeComparisonMode = 0;
    uint8_t comparisonMaskId = 0;
    std::optional<FrameDynamicData> dynamicData;
    std::optional<FrameBackgroundData> backgroundData;
    std::vector<FrameSpriteReference> spriteReferences;
    // The frame's triggers, each kNoTriggerId for none (see Triggers.h): the PUP trigger, the
    // scene it starts and the monochrome trigger. One frame can have all three.
    uint32_t pupTriggerId = kNoTriggerId;
    uint32_t sceneTriggerId = kNoTriggerId;
    uint32_t monochromeTriggerId = kNoTriggerId;
    uint32_t durationMs = 0;
    CompactArray<uint16_t> colorRotationsSd;
    CompactArray<uint16_t> colorRotationsHd;
    // See BackgroundRecord::inactiveHd.
    std::optional<Rgb565Frame> inactiveHd;
    // The scene frame this frame is (.cRPX only).
    std::optional<SceneFrameRef> scene;
    // The author's tags of the frame ("Multiball", "Settings menu"), sorted, each once
    // (.cRPX only): groups of frames to filter the frame strip by.
    std::vector<std::string> tags;
    // What the frame does to the speaker light channels when it shows (SpeakerLights.h).
    LightActions lights{};
    // The dynamic sets that color the SD picture only, bit n for set n
    // (ProjectDocument::originalOnlyDynamicSets()).
    uint32_t originalOnlyDynamicSets = 0;
    // What the frame passes to DOF when it shows (DofEvents.h).
    DofEvents dofEvents;
    ComponentMeta meta;
};

// Which related HD data Create HD upscales besides the frame itself. Dynamic data is not
// among them: the runtime takes it from the original plane, see ProjectDocument::dynamicPlane().
// How Create HD makes a picture twice the size (CDMD's filter box: OpenCV's INTER_NEAREST,
// INTER_LINEAR, INTER_CUBIC and INTER_LANCZOS4, and its own Scale2x). The interpolating ones
// follow OpenCV: pixel centres aligned, edges repeated, bicubic with a = -0.75, Lanczos over 8
// pixels; they make colors the picture did not have. Scale2x keeps the colors and smooths
// diagonal edges, as libserum does when it upscales at runtime.
enum class UpscaleFilter {
    Nearest,
    Bilinear,
    Bicubic,
    Lanczos4,
    Scale2x,
};

struct FrameHdParts {
    bool backgroundMask = false;
    bool backgroundImage = false;
    // For the frame's picture and the background image; masks are always doubled.
    UpscaleFilter filter = UpscaleFilter::Nearest;
};

struct BackgroundHdData {
    std::size_t backgroundIndex = 0;
    std::optional<Rgb565Frame> colorizedHd;
    std::optional<Rgb565Frame> inactiveHd;
};

// The HD data Create HD and Remove HD replace, so that it can be put back exactly. The
// optional parts are only filled for the FrameHdParts that were asked for.
struct FrameHdState {
    std::optional<Rgb565Frame> colorizedHd;
    std::optional<Rgb565Frame> inactiveHd;
    std::optional<IndexedFrame> backgroundMask;
    // The background that was assigned when the state was taken.
    std::optional<BackgroundHdData> background;
};

struct SpriteHdState {
    bool hasHd = false;
    IndexedFrame maskHd;
    Rgb565Frame colorizedHd;
};

// The 96x32 pictures of a 192x64 project that were dropped on load (the editor does not keep
// that plane): frames and backgrounds with a picture of extra resolution, sprites with extra
// resolution data. The project's files hold them until it is saved.
struct DiscardedExtraPlane {
    std::size_t frames = 0;
    std::size_t backgrounds = 0;
    std::size_t sprites = 0;

    [[nodiscard]] bool any() const { return frames > 0 || backgrounds > 0 || sprites > 0; }
    DiscardedExtraPlane& operator+=(const DiscardedExtraPlane& other)
    {
        frames += other.frames;
        backgrounds += other.backgrounds;
        sprites += other.sprites;
        return *this;
    }
    bool operator==(const DiscardedExtraPlane&) const = default;
};

// Missing HD data of an HD-only project. The runtime shows a frame only if the frame, its
// background and every sprite assigned to it have HD data.
struct FrameHdGaps {
    bool frame = false;
    bool background = false;
    // Sprite IDs, ascending and unique.
    std::vector<uint16_t> sprites;

    [[nodiscard]] bool any() const { return frame || background || !sprites.empty(); }
};

// What a frame refers to that the runtime cannot follow. Only loaded projects have these; the
// editor's own edits never make them. They are kept as they are and flagged.
struct FrameReferenceProblems {
    // A comparison mask, background or sprite ID that names no record.
    bool comparisonMask = false;
    bool background = false;
    bool sprite = false;
    // The first empty sprite slot that has used slots behind it; libserum looks for the
    // sprites before it only.
    std::optional<std::size_t> spriteSlotGap;

    [[nodiscard]] bool any() const
    {
        return comparisonMask || background || sprite || spriteSlotGap.has_value();
    }
};

// The edit colors and the 64 color sets of the cRP, as one unit for undo (the Win32 editor
// keeps them in two undo records, SA_ACCOLORS and SA_COLSETS).
struct ColorSetTables {
    // kEditColorCount RGB565 colors; the first colorCount() are used.
    std::vector<uint16_t> editColors;
    // kMaxColorSets flags, 0 or 1 (BOOL in the cRP).
    std::vector<uint32_t> activeColorSets;
    // kMaxColorSets * kColorSetColorCount RGB565 colors.
    std::vector<uint16_t> colorSets;
    // kMaxColorSets names of kColorSetNameBytes bytes, NUL terminated.
    std::vector<uint8_t> colorSetNames;

    [[nodiscard]] bool operator==(const ColorSetTables& other) const;
    [[nodiscard]] bool operator!=(const ColorSetTables& other) const;
};

struct UnsupportedLegacyPayload {
    std::string name;
    std::vector<uint8_t> bytes;
};

struct PaletteRecord {
    std::vector<uint16_t> colors;
    std::string name;
    ComponentMeta meta;
};

struct ProjectCreateOptions {
    std::string romName;
    uint32_t frameCount = 0;
    bool createHdFrames = false;
    uint32_t colorCount = 4;
};

struct ProjectSnapshot;
struct ProjectFrameSnapshot;

// What the IDs of a subset snapshot stand for: subset index -> document index.
struct SnapshotSubsetMap {
    std::vector<std::size_t> frames;
    std::vector<std::size_t> comparisonMasks;
    std::vector<std::size_t> backgrounds;
    std::vector<std::size_t> sprites;
};

constexpr uint16_t kNoBackgroundId = 0xffff;
constexpr uint8_t kNoComparisonMaskId = 255;
constexpr uint16_t kNoSpriteId = 0xffff;
// SpriteRect[0] of a sprite whose source rectangle is not known.
constexpr uint16_t kNoSpriteSourceRect = 0xffff;

// The cRP tables that are indexed by a mask, a section or a sprite. A structural edit hands
// back the tables it changes as a whole, so that undoing it restores every byte of them,
// including entries behind the last record, which the Win32 editor leaves filled.
struct RecordTables {
    std::vector<uint8_t> maskNames;
    uint32_t sectionCount = 0;
    std::vector<uint32_t> sectionFirstFrames;
    std::vector<uint8_t> sectionNames;
    std::vector<uint8_t> spriteNames;
    std::vector<uint32_t> spriteSourceFrames;
    std::vector<uint16_t> spriteSourceRects;
    std::vector<uint32_t> spriteSourceMirrors;
};

// What ProjectDocument::removeFrames() took out; insertFrames() puts it back.
struct RemovedFrames {
    // Ascending and unique, as the frames were numbered before the removal.
    std::vector<std::size_t> indices;
    std::vector<FrameRecord> frames;
    // The imported palettes of those of the frames that had one, in the order of indices.
    std::vector<uint8_t> importedPalettes;
    RecordTables tables;
};

struct RemovedSpriteReference {
    std::size_t frameIndex = 0;
    std::size_t slotIndex = 0;
    FrameSpriteReference reference;
};

struct RemovedSprite {
    std::size_t index = 0;
    SpriteRecord sprite;
    // The frame slots that held the sprite, by frame and then by slot.
    std::vector<RemovedSpriteReference> references;
    RecordTables tables;
};

struct RemovedBackground {
    std::size_t index = 0;
    BackgroundRecord background;
    // The frames it was assigned to, ascending.
    std::vector<std::size_t> frames;
};

struct RemovedComparisonMask {
    std::size_t index = 0;
    IndexedFrame mask;
    ComponentMeta meta;
    // The frames it was assigned to, ascending.
    std::vector<std::size_t> frames;
    RecordTables tables;
};

class ProjectDocument {
public:
    [[nodiscard]] static ProjectDocument createEmpty(const ProjectCreateOptions& options);
    // Taken by value so that a snapshot that is no longer needed can be moved in.
    [[nodiscard]] static ProjectDocument fromSnapshot(ProjectSnapshot snapshot);

    [[nodiscard]] const std::string& romName() const;
    void setRomName(std::string name);

    [[nodiscard]] std::size_t frameCount() const;
    [[nodiscard]] uint32_t colorCount() const;
    [[nodiscard]] const render::ProjectGeometry& geometry() const;
    // Size of the ROM frame: of the original frames and the comparison masks.
    [[nodiscard]] Size originalFrameSize() const;
    // A project with 256x64 original frames (PlaneLayout::NativeExtra). The runtime renders
    // its HD plane only, one to one with the original; the SD data of frames, backgrounds and
    // sprites is stored and written back but never shown or edited. Sprite originals,
    // detection areas and frame sprite bounds are original frame pixels in every project.
    [[nodiscard]] bool rendersHdPlaneOnly() const;
    // A project without an editable extra plane (PlaneLayout::NativeOriginal, 192x64 with a
    // stored 96x32 plane): only the original plane is shown and edited. The 96x32 data of a
    // snapshot is discarded by fromSnapshot() and not held here; toSnapshot() returns it as
    // "no extra data" (discardExtraPlane()). No frame, background or sprite has HD, HD data
    // cannot be created, and every access to Resolution::Hd throws std::logic_error.
    [[nodiscard]] bool editsOriginalPlaneOnly() const;
    // What of the 96x32 plane fromSnapshot() or the load before it dropped; forgotten once the
    // project is saved (forgetDiscardedExtraPlane()), when the files no longer hold it.
    [[nodiscard]] const DiscardedExtraPlane& discardedExtraPlane() const
    {
        return m_discardedExtraPlane;
    }
    void forgetDiscardedExtraPlane() { m_discardedExtraPlane = {}; }
    // What keeps the runtime from showing the frame; nothing for other projects.
    [[nodiscard]] FrameHdGaps frameHdGaps(std::size_t frameIndex) const;
    [[nodiscard]] const EditorMetadata& editorMetadata() const;
    // The editor's helper lines (HelperLine.h), in their order.
    [[nodiscard]] const std::vector<HelperLine>& helperLines() const;
    void setHelperLines(std::vector<HelperLine> lines);
    // The Images window's saved zones (ImageZone.h), in their order. A change marks the
    // project changed; it is not an undo step (as in the Win32 editor).
    [[nodiscard]] const std::vector<SavedImageZone>& imageZones() const;
    void setImageZones(std::vector<SavedImageZone> zones);
    // The scenes and scaling settings the cROM carries (Scene.h); nothing for a project whose
    // editor file has none, as one of the Win32 editor (see importColorizationSidecars()).
    [[nodiscard]] const std::optional<ColorizationSettings>& colorizationSettings() const;
    void setColorizationSettings(std::optional<ColorizationSettings> settings);
    // The speaker lights (SpeakerLights.h): the effects, the channels' defaults, and what a
    // frame does to the channels. setSpeakerLights() needs every effect ID unique and every
    // effect a default or a frame names to exist (std::invalid_argument otherwise);
    // setFrameLights() an effect that exists.
    [[nodiscard]] const SpeakerLightSettings& speakerLights() const { return m_speakerLights; }
    void setSpeakerLights(SpeakerLightSettings settings);
    // The license the colorization is published under (ProjectLicense.h).
    [[nodiscard]] const ProjectLicense& license() const { return m_license; }
    void setLicense(ProjectLicense license);
    [[nodiscard]] const LightActions& frameLights(std::size_t frameIndex) const;
    // The frame's dynamic sets that color the SD picture only, bit n for set n. libserum 2.9
    // keeps their zones off the HD picture of a frame with HD content of its own, which then
    // shows there instead of the upscaled set: for pixel art recolored through a set on a frame
    // whose HD picture is an imported one (libserum's SerumOriginalOnlySets). Kept for every
    // project; the cROM carries them for libserum 2.9 only.
    [[nodiscard]] uint32_t originalOnlyDynamicSets(std::size_t frameIndex) const;
    void setOriginalOnlyDynamicSets(std::size_t frameIndex, uint32_t sets);
    // Whether the runtime keeps a set off the frame's HD picture, as libserum decides it: a
    // project for libserum 2.9 with 128x32 frames and an HD plane, a frame with its HD frame
    // and, if it has a background, the background's HD image (HasHdStaticContent()).
    [[nodiscard]] bool originalOnlyDynamicSetsApply(std::size_t frameIndex) const;
    // originalOnlyDynamicSets() where they apply, 0 elsewhere.
    [[nodiscard]] uint32_t effectiveOriginalOnlyDynamicSets(std::size_t frameIndex) const;
    void setFrameLights(std::size_t frameIndex, const LightActions& lights);
    // The frames that start the effect on some channel.
    [[nodiscard]] std::vector<std::size_t> framesUsingLightEffect(uint16_t effectId) const;
    // Whether the frame starts the effect on some channel.
    [[nodiscard]] bool frameUsesLightEffect(std::size_t frameIndex, uint16_t effectId) const;
    // The frame's DOF events (FrameRecord::dofEvents). std::invalid_argument for a type that
    // is not a capital letter.
    [[nodiscard]] const DofEvents& frameDofEvents(std::size_t frameIndex) const;
    void setFrameDofEvents(std::size_t frameIndex, DofEvents events);
    // The frame's tags (FrameRecord::tags). setFrameTags() sorts them, drops empty ones and
    // repeats, and trims blanks.
    [[nodiscard]] const std::vector<std::string>& frameTags(std::size_t frameIndex) const;
    void setFrameTags(std::size_t frameIndex, std::vector<std::string> tags);
    // Every tag of the project's frames, sorted.
    [[nodiscard]] std::vector<std::string> allFrameTags() const;
    // The scene frame the frame is, if it is one.
    [[nodiscard]] const std::optional<SceneFrameRef>& sceneFrame(std::size_t frameIndex) const;
    void setSceneFrame(std::size_t frameIndex, std::optional<SceneFrameRef> scene);
    // A frame of a scene is its picture: libserum looks for no sprites on it, starts nothing by
    // its trigger, and its comparison mask, shape mode and dynamic zones would only act on the
    // scene marker. The setters of those refuse a value for such a frame
    // (std::logic_error); "none" is always taken. stripSceneFrame() takes them off a frame of a
    // scene and returns whether there was any. Its background, color rotations and speaker
    // lights stay: libserum renders them on a scene's frames.
    bool stripSceneFrame(std::size_t frameIndex);
    // The background scene the frame's trigger starts: a scene of colorizationSettings() with
    // kSceneAsBackground, in a project whose ROM frames are 128x32 (the scenes libserum
    // plays). While it plays libserum shows it through the frame's background mask, in place
    // of the frame's background image, which it takes for a placeholder; such a frame needs
    // none (runtime::SnapshotSource gives libserum one). Nothing for another frame.
    [[nodiscard]] const SceneDefinition* backgroundScene(std::size_t frameIndex) const;
    // The project's frame of that scene that shows sceneElapsedMs after the scene started:
    // a frame of group 1, one per durationMs, from the first again after the last (the
    // editor plays it in a loop), or back from the last with kSceneBackAndForth. Nothing
    // without a background scene or that frame.
    [[nodiscard]] std::optional<std::size_t> backgroundSceneFrame(
        std::size_t frameIndex,
        uint32_t sceneElapsedMs = 0) const;
    // What the plane shows through the frame's background mask: the frame of its background
    // scene backgroundSceneFrame() gives (the first one when the editor does not play it),
    // otherwise the assigned background's image. A scene frame without HD is upscaled as
    // libserum upscales it (the project's scaling algorithm), a background without HD shows
    // nothing on HD. nullptr for none.
    [[nodiscard]] const Rgb565Frame* backgroundPicture(std::size_t frameIndex,
                                                       Resolution resolution,
                                                       uint32_t sceneElapsedMs = 0) const;
    // Whether anything shows through the frame's background mask: an assigned background or a
    // background scene.
    [[nodiscard]] bool hasBackgroundSource(std::size_t frameIndex) const;
    // Replaces the frame's original (the ROM frame it is identified by), e.g. with a scene
    // marker. Throws std::invalid_argument for one that has not the ROM frame's size.
    void setOriginalFrame(std::size_t frameIndex, IndexedFrame original);
    [[nodiscard]] ColorSetTables colorSetTables() const;
    // Replaces the edit colors and color sets. Throws std::invalid_argument, without changing
    // anything, for tables of the wrong sizes. Tables equal to the current ones change
    // nothing.
    void setColorSetTables(ColorSetTables tables);
    [[nodiscard]] uint16_t editColor(std::size_t slot) const;
    // Drawing settings of the cRP that the Win32 editor writes as they are when the project is
    // saved, without counting them as a change: neither marks the document dirty nor changes
    // its revision.
    void setFirstDisplayedColorSet(uint8_t setIndex);
    void setDrawColorMode(uint32_t mode);
    [[nodiscard]] bool isDirty() const;
    void markClean();
    // For a document that is in no file yet, e.g. a new project.
    void markUnsaved();

    // ---- Storage (see CompactArray): nothing here changes the content or the revision.
    // Makes blocks with the same values share one buffer; returns the bytes no longer held.
    // fromSnapshot() does it.
    std::size_t shareIdenticalBlocks();
    struct PackProgress {
        std::size_t bytesFreed = 0;
        // A pass over the whole document has ended.
        bool done = false;
    };
    // Packs blocks (LZ4) for about the budget, going on where the last call stopped, and leaves
    // the blocks of keepFrames unpacked. A packed block is unpacked again when it is read. A
    // block is packed only when every array that holds it is part of the document, so a
    // snapshot, a copy or an undo step that holds it keeps it as it is. No other thread may
    // read the document itself meanwhile (threads that read a snapshot may).
    PackProgress packBlocks(const std::vector<std::size_t>& keepFrames,
                            std::chrono::steady_clock::duration budget);
    // The bytes of the document's blocks, each buffer once: unpacked values and packed bytes.
    [[nodiscard]] std::size_t heldBlockBytes() const;

    // Goes up with every change and is not used by another document's content in this
    // process, so that data derived from a document can tell it is stale.
    [[nodiscard]] uint64_t revision() const;
    // The revision at which what frame identification depends on last changed: the original
    // frames, the comparison masks and each frame's mask and shape mode.
    [[nodiscard]] uint64_t identificationRevision() const;

    [[nodiscard]] bool hasHdFrame(std::size_t frameIndex) const;
    // Upscales the SD frame. In an HD-only project there is nothing to upscale: the HD frame
    // becomes the original in its plain colors (plainOriginalColors).
    // filter: how the SD picture becomes the HD one (see Upscale.h); an HD-only project's HD
    // frame is the original in its plain colors whatever the filter.
    void createHdFrame(std::size_t frameIndex, UpscaleFilter filter = UpscaleFilter::Nearest);
    // Removing and upscaling from SD throw std::logic_error in an HD-only project.
    void removeHdFrame(std::size_t frameIndex);
    void upscaleBackgroundMaskToHd(std::size_t frameIndex);
    // In an HD-only project: gives the assigned background a black HD image if it has none.
    void upscaleAssignedBackgroundToHd(std::size_t frameIndex,
                                       UpscaleFilter filter = UpscaleFilter::Nearest);
    // Throws when a requested part does not exist for the frame.
    [[nodiscard]] FrameHdState frameHdState(std::size_t frameIndex, FrameHdParts parts) const;
    // Writes back everything the state holds; a state that does not fit the frame is rejected
    // without changing anything.
    void setFrameHdState(std::size_t frameIndex, const FrameHdState& state);

    [[nodiscard]] const IndexedFrame& originalFrame(std::size_t frameIndex) const;
    [[nodiscard]] const Rgb565Frame& colorizedFrame(std::size_t frameIndex,
                                                    Resolution resolution) const;
    [[nodiscard]] uint8_t shapeComparisonMode(std::size_t frameIndex) const;
    void setShapeComparisonMode(std::size_t frameIndex, uint8_t mode);
    // Puts back a mode a file was loaded with, including the unknown legacy values that
    // setShapeComparisonMode rejects.
    void restoreShapeComparisonMode(std::size_t frameIndex, uint8_t mode);
    [[nodiscard]] uint8_t comparisonMaskId(std::size_t frameIndex) const;
    // kNoComparisonMaskId or an existing mask; throws std::out_of_range for another ID.
    void setComparisonMaskId(std::size_t frameIndex, uint8_t maskId);
    // Puts back an ID the frame had, also one of a mask that does not exist, which a loaded
    // project can have (the Win32 editor writes what it holds). For undo only.
    void restoreComparisonMaskId(std::size_t frameIndex, uint8_t maskId);
    // The frame's triggers (FrameRecord). Setting one on a frame of a scene throws
    // std::logic_error.
    [[nodiscard]] uint32_t pupTriggerId(std::size_t frameIndex) const;
    void setPupTriggerId(std::size_t frameIndex, uint32_t triggerId);
    [[nodiscard]] uint32_t sceneTriggerId(std::size_t frameIndex) const;
    void setSceneTriggerId(std::size_t frameIndex, uint32_t triggerId);
    [[nodiscard]] uint32_t monochromeTriggerId(std::size_t frameIndex) const;
    void setMonochromeTriggerId(std::size_t frameIndex, uint32_t triggerId);
    // Whether the frame has any of them.
    [[nodiscard]] bool hasTrigger(std::size_t frameIndex) const;
    [[nodiscard]] uint32_t frameDurationMs(std::size_t frameIndex) const;
    void setFrameDurationMs(std::size_t frameIndex, uint32_t durationMs);
    [[nodiscard]] const CompactArray<uint16_t>& colorRotations(std::size_t frameIndex,
                                                               Resolution resolution) const;
    [[nodiscard]] uint16_t colorRotationLength(std::size_t frameIndex,
                                               Resolution resolution,
                                               std::size_t slotIndex) const;
    [[nodiscard]] uint16_t colorRotationDelayMs(std::size_t frameIndex,
                                                Resolution resolution,
                                                std::size_t slotIndex) const;
    [[nodiscard]] uint16_t colorRotationColor(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t slotIndex,
                                              std::size_t colorIndex) const;
    void setColorRotationLength(std::size_t frameIndex,
                                Resolution resolution,
                                std::size_t slotIndex,
                                uint16_t length);
    void setColorRotationDelayMs(std::size_t frameIndex,
                                 Resolution resolution,
                                 std::size_t slotIndex,
                                 uint16_t delayMs);
    void setColorRotationColor(std::size_t frameIndex,
                               Resolution resolution,
                               std::size_t slotIndex,
                               std::size_t colorIndex,
                               uint16_t rgb565);
    void clearColorRotationSlot(std::size_t frameIndex,
                                Resolution resolution,
                                std::size_t slotIndex);
    // Puts a frame's color rotations back as they were before an edit. For undo only.
    void restoreColorRotations(std::size_t frameIndex,
                               Resolution resolution,
                               CompactArray<uint16_t> rotations);
    [[nodiscard]] std::size_t comparisonMaskCount() const;
    [[nodiscard]] const IndexedFrame& comparisonMask(std::size_t maskIndex) const;
    [[nodiscard]] uint8_t comparisonMaskPixel(std::size_t maskIndex, int x, int y) const;
    void setComparisonMaskPixel(std::size_t maskIndex, int x, int y, uint8_t value);
    [[nodiscard]] const std::optional<FrameDynamicData>& dynamicData(std::size_t frameIndex) const;
    // The plane whose dynamic masks, colors and shadows the runtime renders from and the only
    // one they can be changed on: the plane with the ROM frame's size. In an ordinary project
    // that is the 128x32 plane also for the 256x64 picture, which libserum composes from the
    // 128x32 dynamic zones, upscaled (Colorize_Framev2: layer mode forces frameHasDynamicExtra
    // false). The 256x64 dynamic data such a project stores is loaded, readable and written
    // back unchanged; the setters below throw std::logic_error for it.
    [[nodiscard]] Resolution dynamicPlane() const;
    // Clears the 256x64 dynamic zones, their colors and the 256x64 shadow tables an ordinary
    // project stores for the frame (mask 255, colors, directions 0), which libserum never reads
    // there (see dynamicPlane()). Returns whether the frame had any; a frame without is left
    // as it is. Throws std::logic_error in another layout.
    bool clearUnusedHdDynamicZones(std::size_t frameIndex);
    // Whether the frame has a dynamic zone the runtime renders: a pixel of its dynamic mask on
    // dynamicPlane() that is not 255. Loaded frames always carry dynamic data, mostly all 255,
    // so dynamicData() alone does not tell. Cached per frame.
    [[nodiscard]] bool hasDynamicPixels(std::size_t frameIndex) const;
    [[nodiscard]] uint8_t dynamicMaskPixel(std::size_t frameIndex,
                                           Resolution resolution,
                                           int x,
                                           int y) const;
    void setDynamicMaskPixel(std::size_t frameIndex,
                             Resolution resolution,
                             int x,
                             int y,
                             uint8_t value);
    [[nodiscard]] uint16_t dynamicColor(std::size_t frameIndex,
                                        Resolution resolution,
                                        std::size_t setIndex,
                                        std::size_t originalColorIndex) const;
    void setDynamicColor(std::size_t frameIndex,
                         Resolution resolution,
                         std::size_t setIndex,
                         std::size_t originalColorIndex,
                         uint16_t rgb565);
    [[nodiscard]] uint8_t dynamicShadowDirection(std::size_t frameIndex,
                                                 Resolution resolution,
                                                 std::size_t setIndex) const;
    [[nodiscard]] uint16_t dynamicShadowColor(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t setIndex) const;
    void setDynamicShadowDirection(std::size_t frameIndex,
                                   Resolution resolution,
                                   std::size_t setIndex,
                                   uint8_t direction);
    void setDynamicShadowColor(std::size_t frameIndex,
                               Resolution resolution,
                               std::size_t setIndex,
                               uint16_t rgb565);
    [[nodiscard]] std::size_t backgroundCount() const;
    [[nodiscard]] const BackgroundRecord& background(std::size_t backgroundIndex) const;
    [[nodiscard]] const Rgb565Frame& backgroundFrame(std::size_t backgroundIndex,
                                                     Resolution resolution) const;
    void setBackgroundFrame(std::size_t backgroundIndex,
                            Resolution resolution,
                            Rgb565Frame frame);
    void removeBackgroundHdFrame(std::size_t backgroundIndex);
    [[nodiscard]] uint16_t backgroundPixel(std::size_t backgroundIndex,
                                           Resolution resolution,
                                           int x,
                                           int y) const;
    void setBackgroundPixel(std::size_t backgroundIndex,
                            Resolution resolution,
                            int x,
                            int y,
                            uint16_t rgb565);
    [[nodiscard]] const std::optional<FrameBackgroundData>& backgroundData(
        std::size_t frameIndex) const;
    [[nodiscard]] uint16_t backgroundId(std::size_t frameIndex) const;
    // kNoBackgroundId or an existing background; throws std::out_of_range for another ID.
    void setBackgroundId(std::size_t frameIndex, uint16_t backgroundId);
    // As restoreComparisonMaskId(), for undo only.
    void restoreBackgroundId(std::size_t frameIndex, uint16_t backgroundId);
    [[nodiscard]] uint8_t backgroundMaskPixel(std::size_t frameIndex,
                                              Resolution resolution,
                                              int x,
                                              int y) const;
    void setBackgroundMaskPixel(std::size_t frameIndex,
                                Resolution resolution,
                                int x,
                                int y,
                                uint8_t value);

    [[nodiscard]] std::size_t spriteCount() const;
    [[nodiscard]] const SpriteRecord& sprite(std::size_t spriteIndex) const;
    void setSpriteName(std::size_t spriteIndex, std::string name);
    [[nodiscard]] std::size_t paletteCount() const;
    [[nodiscard]] const PaletteRecord& palette(std::size_t paletteIndex) const;
    [[nodiscard]] uint16_t paletteColor(std::size_t paletteIndex,
                                        std::size_t colorIndex) const;
    void setPaletteColor(std::size_t paletteIndex, std::size_t colorIndex, uint16_t rgb565);
    void setPaletteName(std::size_t paletteIndex, std::string name);
    [[nodiscard]] bool hasHdSprite(std::size_t spriteIndex) const;
    // Upscales the SD sprite: its colors with the filter, its mask doubled. In an HD-only
    // project the HD sprite lines up with the sprite original instead: its mask is the
    // original's shape, its colors the plain original colors.
    void createHdSprite(std::size_t spriteIndex, UpscaleFilter filter = UpscaleFilter::Nearest);
    // Throws std::logic_error in an HD-only project.
    void removeHdSprite(std::size_t spriteIndex);
    [[nodiscard]] SpriteHdState spriteHdState(std::size_t spriteIndex) const;
    void setSpriteHdState(std::size_t spriteIndex, SpriteHdState state);
    // Puts a sprite back as it was before an edit. For undo only.
    void restoreSprite(std::size_t spriteIndex, SpriteRecord sprite);
    // The frame the cRP names as the one a sprite was taken from (Sprite_Col_From_Frame).
    [[nodiscard]] uint32_t spriteSourceFrame(std::size_t spriteIndex) const;
    void setSpriteSourceFrame(std::size_t spriteIndex, uint32_t frameIndex);
    [[nodiscard]] uint8_t spriteOriginalPixel(std::size_t spriteIndex,
                                              int x,
                                              int y) const;
    // Both captures take a frame's original and a colorized frame of the same size. In an
    // HD-only project that is the HD frame: the captured colors become the HD sprite, its mask
    // the captured shape, and the unused SD sprite data is reset (colors 0, not dynamic).
    void replaceSpriteFromFrame(std::size_t spriteIndex,
                                const IndexedFrame& originalFrame,
                                const Rgb565Frame& colorizedFrame,
                                int sourceX,
                                int sourceY,
                                int width,
                                int height);
    void replaceSpriteFromFrameSelection(std::size_t spriteIndex,
                                         const IndexedFrame& originalFrame,
                                         const Rgb565Frame& colorizedFrame,
                                         const std::vector<render::Point>& sourcePixels);
    // Replaces the sprite's picture on one plane with sprite-sized (256x64) shades (255 for
    // transparent) and colors, as a capture does: on the SD plane of a project with two
    // planes the HD picture of a sprite that has one is made again from it; on its HD plane
    // only the HD picture changes (the sprite needs HD data); in an HD-only project the plane
    // is the HD one and the shades are also the sprite's original. Throws std::logic_error
    // for a plane the sprite cannot have, std::invalid_argument for frames of another size.
    void replaceSpritePlane(std::size_t spriteIndex,
                            Resolution plane,
                            IndexedFrame shades,
                            Rgb565Frame colors);
    // Sprite shape mode: libserum compares the sprite's shape (lit or not) instead of its
    // shades when it looks for it.
    void setSpriteShapeMode(std::size_t spriteIndex, bool shapeMode);
    // The sprite's dynamic content lives on dynamicPlane(), as the frames' does. Pixels the
    // sprite does not cover (transparent on that plane) are left alone; 255 is "not dynamic".
    // Returns how many pixels changed.
    std::size_t setSpriteDynamicMaskPixels(std::size_t spriteIndex,
                                           const std::vector<render::Point>& points,
                                           uint8_t value);
    [[nodiscard]] uint16_t spriteDynamicColor(std::size_t spriteIndex,
                                              std::size_t setIndex,
                                              std::size_t originalColorIndex) const;
    void setSpriteDynamicColor(std::size_t spriteIndex,
                               std::size_t setIndex,
                               std::size_t originalColorIndex,
                               uint16_t rgb565);
    void setSpriteOriginalPixel(std::size_t spriteIndex,
                                int x,
                                int y,
                                uint8_t value);
    [[nodiscard]] uint16_t spriteColorizedPixel(std::size_t spriteIndex,
                                                Resolution resolution,
                                                int x,
                                                int y) const;
    void setSpriteColorizedPixel(std::size_t spriteIndex,
                                 Resolution resolution,
                                 int x,
                                 int y,
                                 uint16_t rgb565);
    void setSpriteDetectionArea(std::size_t spriteIndex,
                                std::size_t areaIndex,
                                SpriteDetectionArea area);
    [[nodiscard]] const std::vector<FrameSpriteReference>& frameSpriteReferences(
        std::size_t frameIndex) const;
    // A frame's sprite list ends, for the runtime, at its first empty slot (libserum stops
    // there), so no edit may leave an empty slot before a used one. Putting a sprite into an
    // empty slot behind the first empty one throws std::invalid_argument (see
    // spriteSlotForAssignment()); kNoSpriteId for a used slot removes it as
    // clearFrameSpriteReference() does. Changing the sprite or the bounds of a used slot and
    // the bounds of an empty one is always possible.
    void setFrameSpriteReference(std::size_t frameIndex,
                                 std::size_t slotIndex,
                                 FrameSpriteReference reference);
    // Removes the sprite of the slot: the slots behind it move up with their bounds and the
    // last slot becomes empty, keeping its bounds (IDC_DELSPRITE2 of the Win32 editor). An
    // empty slot is left as it is.
    void clearFrameSpriteReference(std::size_t frameIndex, std::size_t slotIndex);
    // Swaps the sprites of two used slots, each with its bounds: the order in which the runtime
    // draws them, a later slot over an earlier one. False, changing nothing, when one of them
    // is empty.
    bool swapFrameSpriteReferences(std::size_t frameIndex, std::size_t slotA, std::size_t slotB);
    // Puts a frame's sprite slots back as they were before an edit, gaps included. For undo
    // only.
    void restoreFrameSpriteReferences(std::size_t frameIndex,
                                      std::vector<FrameSpriteReference> references);
    // Where a sprite meant for the slot goes: the slot itself if it holds a sprite, otherwise
    // the first empty slot (which is the slot itself or one before it).
    [[nodiscard]] std::size_t spriteSlotForAssignment(std::size_t frameIndex,
                                                      std::size_t slotIndex) const;
    // Whether a sprite meant for the slot would be the frame's first sprite past
    // kSpritesPerFrameWarning: the step the author is warned about (see ProjectLimits.h).
    [[nodiscard]] bool assignmentPassesSpriteWarning(std::size_t frameIndex,
                                                     std::size_t slotIndex) const;
    // The first empty slot of a frame that has a used slot behind it: the runtime ignores the
    // sprites from there on. Only a loaded project can have one; nothing here makes one.
    [[nodiscard]] std::optional<std::size_t> firstSpriteSlotGap(std::size_t frameIndex) const;
    // References of the frame the runtime cannot follow.
    [[nodiscard]] FrameReferenceProblems frameReferenceProblems(std::size_t frameIndex) const;

    void setColorizedPixel(std::size_t frameIndex,
                           Resolution resolution,
                           int x,
                           int y,
                           uint16_t rgb565);
    [[nodiscard]] uint16_t colorizedPixel(std::size_t frameIndex,
                                          Resolution resolution,
                                          int x,
                                          int y) const;

    // Structural edits: records are removed or added, and every ID that refers to a record
    // of that kind follows. Each remove...() returns what it took out, moved and not copied,
    // and the matching insert...() puts that back where it was, with the references and
    // the cRP tables as they were. An insert...() is only meant for the state its
    // remove...() left behind, apart from edits that neither add nor remove records;
    // edit::StructureCommand guarantees that by keeping all of them in one undo history.
    // All of them throw, without changing anything, when the request does not fit the
    // document.

    [[nodiscard]] RecordTables recordTables() const;
    void restoreRecordTables(RecordTables tables);

    // Any frames, in any order; none changes nothing. At least one frame has to remain.
    // What follows a frame in the cRP goes with it: its imported palette, and by the Win32
    // editor's rules (Delete_Frame, Del_Section_Frame)
    // - a section starts at the first remaining frame at or behind its first frame, and is
    //   removed when another section starts between the two or no frame remains there;
    // - a sprite whose source frame is removed loses its source rectangle
    //   (kNoSpriteSourceRect), and source frame numbers behind a removed frame move down.
    [[nodiscard]] RemovedFrames removeFrames(const std::vector<std::size_t>& frameIndices);
    // Checks every frame before anything changes: each block must have the size of its plane
    // in this document (original, colorized and extra-plane frames, dynamic masks, colors and
    // shadows, background masks, color rotations, sprite slots), and there must be no
    // extra-plane data in a project that discards that plane. Throws std::invalid_argument
    // naming the frame and the block otherwise.
    void insertFrames(RemovedFrames removed);
    // A frame record of the snapshot's content, for insertFrames(); its color rotations are
    // normalized as a load does.
    [[nodiscard]] static FrameRecord frameRecordFrom(ProjectFrameSnapshot frame);
    // The frame's content as a snapshot frame, with its change information.
    [[nodiscard]] ProjectFrameSnapshot frameSnapshotOf(std::size_t frameIndex) const;
    // Puts the record's content in place of the frame's (a newer version of the frame from
    // a package): pictures, masks, settings, references. The frame keeps its ID. Checks the
    // record as insertFrames() does; std::out_of_range for a frame that does not exist.
    void replaceFrame(std::size_t frameIndex, FrameRecord record);
    // The same for a comparison mask (a picture of the ROM frame's size) and a background
    // (of the planes' sizes, without inactive HD pixels): it keeps its ID and its frames.
    void replaceComparisonMask(std::size_t maskIndex, IndexedFrame mask);
    void replaceBackground(std::size_t backgroundIndex, BackgroundRecord background);
    // Makes the project the one in state, as a whole: for undo steps that keep whole states
    // (edit::PackageImportCommand), which share every block with it. It is a change of the
    // project, also of what identifies frames.
    void restoreState(const ProjectDocument& state);
    // Puts the frames in a new order: the frame numbered newOrder[i] becomes frame i. The
    // source frames of sprites and the first frames of sections follow their frames, and so
    // do imported palettes. Throws std::invalid_argument when newOrder is no order of all
    // frames, and std::logic_error when the frames that have an imported palette would no
    // longer be the first ones (the cRP stores them for the first frames only).
    void reorderFrames(const std::vector<std::size_t>& newOrder);
    // A frame for insertFrames() that nobody has worked on, see
    // makeUncolorizedFrameSnapshot(). Throws std::invalid_argument for an original that has
    // not the size of the ROM frame or has a shade the project has no color for.
    [[nodiscard]] FrameRecord makeUncolorizedFrame(IndexedFrame original,
                                                   uint32_t durationMs) const;

    // A sprite without any pixel, as the Win32 editor creates it. In a project that renders
    // its extra plane only it has that plane's data, which the runtime needs.
    [[nodiscard]] SpriteRecord makeBlankSprite(std::string name) const;
    [[nodiscard]] bool isSpriteNameUsed(const std::string& name) const;
    // Returns the new sprite's index. sourceFrameIndex is the frame the cRP records the
    // sprite as taken from.
    [[nodiscard]] std::size_t appendSprite(SpriteRecord sprite, std::size_t sourceFrameIndex);
    // The sprite leaves every frame's slot list, which closes up as the runtime needs it
    // (a list ends at its first empty slot), and higher sprite IDs move down.
    [[nodiscard]] RemovedSprite removeSprite(std::size_t spriteIndex);
    void insertSprite(RemovedSprite removed);

    // The colorized frame as a background, as the Win32 editor's AddBackground takes it:
    // without extra-plane data in an ordinary project. A project that renders its extra
    // plane only takes that plane too and needs the frame to have it.
    [[nodiscard]] BackgroundRecord makeBackgroundFromFrame(std::size_t frameIndex) const;
    [[nodiscard]] std::size_t appendBackground(BackgroundRecord background);
    // Frames that used it get kNoBackgroundId, higher background IDs move down.
    [[nodiscard]] RemovedBackground removeBackground(std::size_t backgroundIndex);
    void insertBackground(RemovedBackground removed);

    // An empty mask (nothing masked) behind the last one.
    [[nodiscard]] std::size_t appendComparisonMask();
    // Frames that used it get kNoComparisonMaskId, higher mask IDs and mask names move down.
    [[nodiscard]] RemovedComparisonMask removeComparisonMask(std::size_t maskIndex);
    void insertComparisonMask(RemovedComparisonMask removed);
    // How many frames a comparison mask, background or sprite is assigned to.
    [[nodiscard]] std::size_t comparisonMaskUseCount(std::size_t maskIndex) const;
    [[nodiscard]] std::size_t backgroundUseCount(std::size_t backgroundIndex) const;
    [[nodiscard]] std::size_t spriteUseCount(std::size_t spriteIndex) const;

    void addUnsupportedPayload(UnsupportedLegacyPayload payload);
    [[nodiscard]] const std::vector<UnsupportedLegacyPayload>& unsupportedPayloads() const;
    [[nodiscard]] ProjectSnapshot toSnapshot() const;

    // The identity and change information of each component (ComponentId.h). Every component
    // has an ID, unique within its kind: the document gives one to a component that comes
    // without one, and a new one to a component that comes with the ID of another (a copy).
    // A component keeps its ID when it is removed and put back (undo).
    [[nodiscard]] const ComponentMeta& frameMeta(std::size_t frameIndex) const;
    [[nodiscard]] const ComponentMeta& comparisonMaskMeta(std::size_t maskIndex) const;
    [[nodiscard]] const ComponentMeta& backgroundMeta(std::size_t backgroundIndex) const;
    [[nodiscard]] const ComponentMeta& spriteMeta(std::size_t spriteIndex) const;
    [[nodiscard]] const ComponentMeta& paletteMeta(std::size_t paletteIndex) const;
    [[nodiscard]] std::optional<std::size_t> frameIndexOf(const ComponentId& id) const;
    // Takes over the change information a save gave components (project::saveProjectFile()),
    // found by their IDs; a component removed since is skipped. Not an edit: the project
    // stays as it was, also unchanged when it was.
    void adoptComponentMeta(const std::vector<StampedMeta>& stamped);
    // What this copy of a team project keeps about the team (TeamState.h); nothing for a solo
    // project. Setting it is no edit of the project, but it is saved with it.
    [[nodiscard]] const std::optional<TeamState>& team() const { return m_team; }
    void setTeam(std::optional<TeamState> team);
    // The project's own ID, kept by every copy of the project file.
    [[nodiscard]] const ComponentId& projectId() const { return m_projectId; }
    // The given frames alone, in that order, with the comparison masks, backgrounds and sprites
    // they reference and nothing else. The IDs in the frames are renumbered to match, in order
    // of first use; an ID that points at nothing becomes "none". Palettes, editor metadata and
    // unsupported payloads are left out. Costs what the listed frames and their items cost,
    // not what the project costs. Throws std::out_of_range for a frame that does not exist.
    [[nodiscard]] ProjectSnapshot toSubsetSnapshot(const std::vector<std::size_t>& frameIndices,
                                                   SnapshotSubsetMap* map = nullptr) const;

private:
    void replaceCapturedHdOnlySprite(SpriteRecord& sprite,
                                     IndexedFrame original,
                                     Rgb565Frame colorized);
    void requireEditablePlane(Resolution resolution) const;
    void requireEditableExtraPlane() const;
    void requireDynamicPlane(Resolution resolution) const;
    // Throws std::logic_error for a frame of a scene, see stripSceneFrame().
    static void requireGameFrame(const FrameRecord& frame, const char* what);
    void requireFittingRecordTables(const RecordTables& tables) const;
    void markChanged();
    // One of the frame's triggers (FrameRecord::pupTriggerId and the others).
    void setFrameTrigger(std::size_t frameIndex,
                         uint32_t FrameRecord::*trigger,
                         uint32_t triggerId);
    // Gives a new ID to each component that has none or the ID of one before it.
    void ensureComponentIds();
    // The records packBlocks() goes through: the frames, then the comparison masks, the
    // backgrounds and the sprites; visit is called with each CompactArray of the record.
    [[nodiscard]] std::size_t storageItemCount() const;
    template <typename Visit>
    void visitItemBlocks(std::size_t item, Visit&& visit);

    std::string m_romName;
    ComponentId m_projectId;
    std::optional<TeamState> m_team;
    std::vector<FrameRecord> m_frames;
    std::vector<IndexedFrame> m_comparisonMasks;
    // Parallel to m_comparisonMasks.
    std::vector<ComponentMeta> m_comparisonMaskMeta;
    std::vector<BackgroundRecord> m_backgrounds;
    std::vector<SpriteRecord> m_sprites;
    std::vector<PaletteRecord> m_palettes;
    std::vector<UnsupportedLegacyPayload> m_unsupportedPayloads;
    EditorMetadata m_editorMetadata = makeDefaultEditorMetadata();
    std::vector<HelperLine> m_helperLines;
    std::vector<SavedImageZone> m_imageZones;
    std::optional<ColorizationSettings> m_colorizationSettings;
    SpeakerLightSettings m_speakerLights;
    ProjectLicense m_license;
    DiscardedExtraPlane m_discardedExtraPlane;
    uint32_t m_colorCount = 4;
    render::ProjectGeometry m_geometry;
    bool m_dirty = false;
    uint64_t m_revision = 0;
    uint64_t m_identificationRevision = 0;
    // hasDynamicPixels() per frame: 0 not known, 1 no dynamic pixel, 2 at least one. Filled
    // on demand; a dynamic mask edit updates its frame, adding or removing frames drops it.
    // Like the rest of the document, not for concurrent use from several threads.
    mutable std::vector<uint8_t> m_dynamicPixelStates;
    // backgroundSceneFrame(): the frames of group 1 of each scene, by scene ID << 16 | frame
    // index, and backgroundPicture(): the HD pictures upscaled from those without, by frame;
    // of the revision m_sceneRevision.
    mutable uint64_t m_sceneRevision = 0;
    mutable bool m_sceneCacheValid = false;
    mutable std::unordered_map<uint32_t, std::size_t> m_sceneFrames;
    mutable std::unordered_map<std::size_t, Rgb565Frame> m_upscaledSceneFrames;
    // packBlocks(): where it goes on, the revision its counts are of, and how many arrays of the
    // document hold each buffer.
    std::size_t m_packCursor = 0;
    uint64_t m_packRevision = 0;
    std::unordered_map<const void*, long> m_packHolders;
};

// The colors of an uncolorized frame: a ramp over the original color indices, as the Win32
// editor writes it.
[[nodiscard]] std::vector<uint16_t> plainOriginalColors(uint32_t colorCount);

[[nodiscard]] Rgb565Frame makeRgb565Frame(Size size, uint16_t fill = 0);

// ProjectDocument::clearUnusedHdDynamicZones() for every frame of an ordinary project, nothing
// in another layout. Returns the number of frames that had such zones or shadows.
std::size_t clearUnusedHdDynamicZones(ProjectDocument& document);
// ProjectDocument::stripSceneFrame() for every frame. Returns the number of frames that had
// something a frame of a scene cannot have.
std::size_t stripSceneFrames(ProjectDocument& document);

// The memory of the blocks only this record holds: uniform blocks and blocks shared with a
// copy (see CompactArray) count 0. An estimate for limits such as the undo memory, not exact.
[[nodiscard]] std::size_t ownedBytes(const FrameRecord& frame);
[[nodiscard]] std::size_t ownedBytes(const SpriteRecord& sprite);
[[nodiscard]] std::size_t ownedBytes(const BackgroundRecord& background);
[[nodiscard]] IndexedFrame makeIndexedFrame(Size size, uint8_t fill = 0);

}  // namespace colorizing::document
