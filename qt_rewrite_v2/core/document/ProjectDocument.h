#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document/EditorMetadata.h"
#include "render/CanvasTypes.h"
#include "render/ProjectGeometry.h"

namespace colorizing::document {

using render::Resolution;
using render::Size;

struct Rgb565Frame {
    Size size;
    std::vector<uint16_t> pixels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::size_t pixelCount() const;
};

struct IndexedFrame {
    Size size;
    std::vector<uint8_t> pixels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::size_t pixelCount() const;
};

constexpr std::size_t kMaxDynamicSetsPerFrame = 32;
constexpr std::size_t kMaxDynamicSetsPerSprite = 9;
constexpr std::size_t kMaxSpritesPerFrame = 32;
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
    std::vector<uint16_t> colorsSd;
    std::vector<uint16_t> colorsHd;
    std::vector<uint8_t> shadowDirectionsSd;
    std::vector<uint16_t> shadowColorsSd;
    std::vector<uint8_t> shadowDirectionsHd;
    std::vector<uint16_t> shadowColorsHd;
};

struct BackgroundRecord {
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    // HD pixels the Win32 editor left in the file although HD is switched off. They are kept so
    // an unedited project saves back unchanged, and dropped once HD is created or removed here.
    std::optional<Rgb565Frame> inactiveHd;
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
    std::vector<uint16_t> dynamicColorsSd;
    std::vector<uint16_t> dynamicColorsHd;
    uint8_t shapeMode = 0;
};

struct FrameSpriteReference {
    uint8_t spriteId = 255;
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
    uint32_t triggerId = kNoTriggerId;
    uint32_t durationMs = 0;
    std::vector<uint16_t> colorRotationsSd;
    std::vector<uint16_t> colorRotationsHd;
    // See BackgroundRecord::inactiveHd.
    std::optional<Rgb565Frame> inactiveHd;
};

// Which related HD data Create HD upscales besides the frame itself. Dynamic data is not
// among them: the runtime takes it from the original plane, see ProjectDocument::dynamicPlane().
struct FrameHdParts {
    bool backgroundMask = false;
    bool backgroundImage = false;
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

// Missing HD data of an HD-only project. The runtime shows a frame only if the frame, its
// background and every sprite assigned to it have HD data.
struct FrameHdGaps {
    bool frame = false;
    bool background = false;
    // Sprite IDs, ascending and unique.
    std::vector<uint8_t> sprites;

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
};

struct ProjectCreateOptions {
    std::string romName;
    uint32_t frameCount = 0;
    bool createHdFrames = false;
    uint32_t colorCount = 4;
};

struct ProjectSnapshot;

// What the IDs of a subset snapshot stand for: subset index -> document index.
struct SnapshotSubsetMap {
    std::vector<std::size_t> frames;
    std::vector<std::size_t> comparisonMasks;
    std::vector<std::size_t> backgrounds;
    std::vector<std::size_t> sprites;
};

constexpr std::size_t kMaxBackgrounds = 255;
constexpr uint16_t kNoBackgroundId = 0xffff;
constexpr uint8_t kNoComparisonMaskId = 255;
constexpr uint8_t kNoSpriteId = 255;
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
    // What keeps the runtime from showing the frame; nothing for other projects.
    [[nodiscard]] FrameHdGaps frameHdGaps(std::size_t frameIndex) const;
    [[nodiscard]] const EditorMetadata& editorMetadata() const;
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
    // Goes up with every change and is not used by another document's content in this
    // process, so that data derived from a document can tell it is stale.
    [[nodiscard]] uint64_t revision() const;
    // The revision at which what frame identification depends on last changed: the original
    // frames, the comparison masks and each frame's mask and shape mode.
    [[nodiscard]] uint64_t identificationRevision() const;

    [[nodiscard]] bool hasHdFrame(std::size_t frameIndex) const;
    // Upscales the SD frame. In an HD-only project there is nothing to upscale: the HD frame
    // becomes the original in its plain colors (plainOriginalColors).
    void createHdFrame(std::size_t frameIndex);
    // Removing and upscaling from SD throw std::logic_error in an HD-only project.
    void removeHdFrame(std::size_t frameIndex);
    void upscaleBackgroundMaskToHd(std::size_t frameIndex);
    // In an HD-only project: gives the assigned background a black HD image if it has none.
    void upscaleAssignedBackgroundToHd(std::size_t frameIndex);
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
    [[nodiscard]] uint32_t triggerId(std::size_t frameIndex) const;
    void setTriggerId(std::size_t frameIndex, uint32_t triggerId);
    [[nodiscard]] uint32_t frameDurationMs(std::size_t frameIndex) const;
    void setFrameDurationMs(std::size_t frameIndex, uint32_t durationMs);
    [[nodiscard]] const std::vector<uint16_t>& colorRotations(std::size_t frameIndex,
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
    // Upscales the SD sprite. In an HD-only project the HD sprite lines up with the sprite
    // original instead: its mask is the original's shape, its colors the plain original colors.
    void createHdSprite(std::size_t spriteIndex);
    // Throws std::logic_error in an HD-only project.
    void removeHdSprite(std::size_t spriteIndex);
    [[nodiscard]] SpriteHdState spriteHdState(std::size_t spriteIndex) const;
    void setSpriteHdState(std::size_t spriteIndex, SpriteHdState state);
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
    // Where a sprite meant for the slot goes: the slot itself if it holds a sprite, otherwise
    // the first empty slot (which is the slot itself or one before it).
    [[nodiscard]] std::size_t spriteSlotForAssignment(std::size_t frameIndex,
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
    void requireFittingRecordTables(const RecordTables& tables) const;
    void markChanged();

    std::string m_romName;
    std::vector<FrameRecord> m_frames;
    std::vector<IndexedFrame> m_comparisonMasks;
    std::vector<BackgroundRecord> m_backgrounds;
    std::vector<SpriteRecord> m_sprites;
    std::vector<PaletteRecord> m_palettes;
    std::vector<UnsupportedLegacyPayload> m_unsupportedPayloads;
    EditorMetadata m_editorMetadata = makeDefaultEditorMetadata();
    uint32_t m_colorCount = 4;
    uint32_t m_cromRevision = 20;
    render::ProjectGeometry m_geometry;
    bool m_dirty = false;
    uint64_t m_revision = 0;
    uint64_t m_identificationRevision = 0;
    // hasDynamicPixels() per frame: 0 not known, 1 no dynamic pixel, 2 at least one. Filled
    // on demand; a dynamic mask edit updates its frame, adding or removing frames drops it.
    // Like the rest of the document, not for concurrent use from several threads.
    mutable std::vector<uint8_t> m_dynamicPixelStates;
};

// The colors of an uncolorized frame: a ramp over the original color indices, as the Win32
// editor writes it.
[[nodiscard]] std::vector<uint16_t> plainOriginalColors(uint32_t colorCount);

[[nodiscard]] Rgb565Frame makeRgb565Frame(Size size, uint16_t fill = 0);
[[nodiscard]] IndexedFrame makeIndexedFrame(Size size, uint8_t fill = 0);

}  // namespace colorizing::document
