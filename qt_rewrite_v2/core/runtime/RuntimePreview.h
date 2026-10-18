#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "render/ProjectGeometry.h"
#include "runtime/FrameIdentification.h"

namespace colorizing::runtime {

// Which output plane is asked of libserum, by its number of rows: what a display of that
// height requests. Rows64 on an ordinary project makes libserum upscale frames without HD.
enum class PlaneRequest {
    Rows32,
    Rows64,
    Both,
};

// The requests that are different pictures for the layout. An ordinary project has both
// planes; the two native layouts have their native plane alone, which has 64 rows.
[[nodiscard]] std::vector<PlaneRequest> planeRequestsFor(const render::ProjectGeometry& geometry);
// The request sent to libserum for the layout: `request` if the layout has that plane,
// otherwise the native plane.
[[nodiscard]] PlaneRequest effectivePlaneRequest(const render::ProjectGeometry& geometry,
                                                 PlaneRequest request);

struct RuntimeRotationSlot {
    uint16_t length = 0;
    uint16_t delayMs = 0;
};

struct RuntimePlane {
    document::Rgb565Frame image;
    // The colour rotations libserum set up for this plane; a slot with length or delay 0 is
    // unused.
    std::array<RuntimeRotationSlot, document::kMaxColorRotations> rotations;

    [[nodiscard]] bool rotates() const;
};

// Wall-clock cost of the last call, in milliseconds.
struct RuntimeCost {
    double subsetMs = 0.0;
    double serializeMs = 0.0;
    double loadMs = 0.0;
    double colorizeMs = 0.0;
    std::size_t cromBytes = 0;

    [[nodiscard]] double totalMs() const
    {
        return subsetMs + serializeMs + loadMs + colorizeMs;
    }
};

// What libserum returned for one ROM frame.
struct RuntimeFrame {
    // Not empty when the call itself failed: nothing below is valid then.
    std::string error;
    // libserum recognised the ROM frame as a frame of the colorization.
    bool identified = false;
    // That frame, as a document frame index. In a sequence opened from a document it is
    // also known where libserum does not report it (see unchanged), from the search rule
    // libserum follows (FrameIdentification).
    std::optional<std::size_t> frameIndex;
    // Sequences opened from a document: the frame FrameIdentification expects libserum to
    // identify. Tests compare it with what libserum reports.
    std::optional<std::size_t> expectedFrameIndex;
    // libserum recognised the frame and renders nothing for it: an inactive frame without a
    // background and without dynamic content. A machine keeps the picture it shows.
    bool skipped = false;
    // libserum reported the picture already on display; the planes are the ones it left.
    // It does not say which frame it identified then.
    bool unchanged = false;
    // The planes libserum reported as filled for this call.
    std::optional<RuntimePlane> rows32;
    std::optional<RuntimePlane> rows64;
    // At least one loaded frame has extra-plane content (FLAG_RETURNED_EXTRA_AVAILABLE).
    bool extraPlaneAvailable = false;
    // The trigger libserum reported, or document::kNoTriggerId. libserum does not repeat a
    // trigger it reported less than 500 ms ago, in this process, whatever was loaded since.
    uint32_t triggerId = document::kNoTriggerId;
    // Milliseconds until libserum wants rotate() called; 0 when nothing rotates.
    uint32_t nextRotationMs = 0;
    RuntimeCost cost;

    [[nodiscard]] bool ok() const { return error.empty(); }
    [[nodiscard]] const RuntimePlane* plane(PlaneRequest rows) const;
};

// In RuntimeSubset::map.frames: a frame that stands for no document frame.
constexpr std::size_t kNoDocumentFrame = static_cast<std::size_t>(-1);

// A minimal cROM for some frames of a document, see ProjectDocument::toSubsetSnapshot(). In a
// project with 256x64 original frames it can start with frames of its own, which no ROM frame
// matches (kNoDocumentFrame in map.frames); the requested frames follow in their order. Those
// frames have a shape mode value no frame of the project has, which makes them a
// comparison bucket of their own that never matches: they do not change the order in which
// libserum's search meets the buckets of the other frames.
struct RuntimeSubset {
    std::vector<uint8_t> crom;
    document::SnapshotSubsetMap map;
    // The ROM frames of map.frames, in that order.
    std::vector<document::IndexedFrame> originals;
    render::ProjectGeometry geometry;
    double subsetMs = 0.0;
    double serializeMs = 0.0;
};

// Throws std::runtime_error if the subset cannot be written, std::out_of_range for a frame
// that does not exist.
[[nodiscard]] RuntimeSubset buildRuntimeSubset(const document::ProjectDocument& document,
                                               const std::vector<std::size_t>& frameIndices);

// Renders frames with libserum itself.
//
// libserum keeps the loaded colorization, its identification state and its timers in process
// globals, so there is one instance and every call is serialized. At most one colorization
// is loaded at a time, and only between openSequence()/openCrom() and close(); renderFrame()
// loads and releases its own.
class RuntimePreview {
public:
    [[nodiscard]] static RuntimePreview& instance();

    RuntimePreview(const RuntimePreview&) = delete;
    RuntimePreview& operator=(const RuntimePreview&) = delete;

    // One frame, as a still: a cROM of that frame alone is loaded, its ROM frame colorized
    // once, and the colorization released. The result depends on the document only: nothing
    // was identified before, no rotation has stepped, no plane is kept from an earlier frame.
    // An open sequence is closed first.
    [[nodiscard]] RuntimeFrame renderFrame(const document::ProjectDocument& document,
                                           std::size_t frameIndex,
                                           PlaneRequest request);

    // Plays frames of the document through libserum with frame identification behaving as
    // with the whole project loaded.
    //
    // openSequence() starts as libserum does after loading the project: its search starts at
    // the first frame. It loads a window: the first frames of frameIndices (at most
    // kSequenceWindowFrames, fewer if they need many other frames) together with every other
    // frame of the project that takes part in identifying them, see
    // FrameIdentification::candidates(). sequenceHasFrame() tells which frames the window
    // plays. For the frames after them continueSequence() loads the next window and carries
    // libserum's identification state over: the new colorization starts with the frame
    // identified last (its frames are rotated, which keeps their cyclic order, the one thing
    // the search depends on), and the ROM frame fed last is fed again, unseen, which puts
    // back "identified last", the "first match" flag and the CRC of the last frame. So
    // identification is the same as with one colorization of the whole project, with the
    // memory of a window. What else libserum keeps from frames before the window (a
    // monochrome switch, the phase of running color rotations) starts again with it.
    static constexpr std::size_t kSequenceWindowFrames = 48;
    // A window stops growing when it holds this many frames, played or not.
    static constexpr std::size_t kSequenceLoadedFrames = 160;
    [[nodiscard]] bool openSequence(const document::ProjectDocument& document,
                                    const std::vector<std::size_t>& frameIndices,
                                    PlaneRequest request,
                                    std::string* errorMessage = nullptr,
                                    std::size_t windowFrames = kSequenceWindowFrames);
    // Without an open sequence this is openSequence().
    [[nodiscard]] bool continueSequence(const document::ProjectDocument& document,
                                        const std::vector<std::size_t>& frameIndices,
                                        PlaneRequest request,
                                        std::string* errorMessage = nullptr,
                                        std::size_t windowFrames = kSequenceWindowFrames);
    // Frames the loaded colorization holds, the played ones and the others, and the size of
    // the cROM it was loaded from.
    [[nodiscard]] std::size_t sequenceLoadedFrameCount() const;
    [[nodiscard]] std::size_t sequenceCromBytes() const;
    // libserum's search rule for the document, built once per
    // ProjectDocument::identificationRevision() and kept.
    [[nodiscard]] std::shared_ptr<const FrameIdentification> identification(
        const document::ProjectDocument& document);
    // Loads a complete cROM as it is. Frame indices of the results are its own.
    [[nodiscard]] bool openCrom(const std::vector<uint8_t>& crom,
                                const render::ProjectGeometry& geometry,
                                std::size_t frameCount,
                                PlaneRequest request,
                                std::string* errorMessage = nullptr);
    [[nodiscard]] bool isOpen() const;
    // Whether the loaded window plays the document frame.
    [[nodiscard]] bool sequenceHasFrame(std::size_t frameIndex) const;
    // Feeds the ROM frame the sequence was opened with for that document frame.
    [[nodiscard]] RuntimeFrame renderSequenceFrame(std::size_t frameIndex);
    // Feeds any ROM frame to the loaded colorization.
    [[nodiscard]] RuntimeFrame colorize(const document::IndexedFrame& original);
    // One call of libserum's rotation step. libserum steps a slot when its delay has passed
    // on its own clock since the slot last stepped, so the caller decides when this is called
    // (nextRotationMs after the previous result) and libserum which colours move.
    [[nodiscard]] RuntimeFrame rotate();
    void close();
    // The project's <rom>.pup.csv as libserum takes the path (libserumPath()), or empty for
    // none. Every load reads its scenes, as libserum reads a pup.csv beside a cROM on a
    // machine (Serum_LoadFromMemoryWithScenes), so background scenes show. A change closes
    // what is loaded; the next open loads with it.
    void setSceneFile(const std::string& path);

private:
    RuntimePreview();
    ~RuntimePreview();

    struct Impl;
    mutable std::mutex m_mutex;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace colorizing::runtime
