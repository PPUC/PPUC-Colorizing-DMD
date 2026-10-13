#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "render/ProjectGeometry.h"
#include "runtime/RuntimePreview.h"

namespace colorizing::app {

// The state of the canvas's Runtime view: whether it is on, which plane it shows, whether it
// plays, and libserum's output for the frame on the canvas. It asks libserum again only when
// the document, the frame, the plane or the playback changed since the last answer, so it can
// be asked from a paint event.
class RuntimeView {
public:
    // Frames one loaded playback window plays at most, see RuntimePreview::openSequence().
    // Playback past them loads the next window, which keeps libserum's identification state.
    static constexpr std::size_t kSequenceFrames = runtime::RuntimePreview::kSequenceWindowFrames;

    RuntimeView() = default;
    RuntimeView(const RuntimeView&) = delete;
    RuntimeView& operator=(const RuntimeView&) = delete;
    ~RuntimeView();

    [[nodiscard]] bool isEnabled() const;
    // Switching off ends playback and releases what libserum has loaded.
    void setEnabled(bool enabled);

    // The plane shown, by its rows: Rows32 or Rows64.
    [[nodiscard]] runtime::PlaneRequest rows() const;
    void setRows(runtime::PlaneRequest rows);
    // The rows shown for a project: one with a single plane shows that plane.
    [[nodiscard]] runtime::PlaneRequest rowsFor(const render::ProjectGeometry& geometry) const;
    // The stored plane that has the size of the shown rows, for laying out the canvas.
    [[nodiscard]] render::Resolution layoutPlane(const render::ProjectGeometry& geometry) const;

    // Playback feeds the frames to one loaded colorization in the order they are shown, so
    // libserum carries its state from frame to frame and its color rotations can be stepped.
    // frameOrder is the order playback goes through.
    void startPlayback(std::vector<std::size_t> frameOrder);
    void stopPlayback();
    [[nodiscard]] bool isPlaying() const;

    [[nodiscard]] const runtime::RuntimeFrame& frame(const document::ProjectDocument& document,
                                                     std::size_t frameIndex);
    // The shown plane of frame(), or nullptr when libserum has no picture; message() says why.
    [[nodiscard]] const document::Rgb565Frame* image(const document::ProjectDocument& document,
                                                     std::size_t frameIndex);
    [[nodiscard]] std::string message(const document::ProjectDocument& document,
                                      std::size_t frameIndex);
    // The frame a machine identifies the frame's ROM frame as, if that is another frame.
    //
    // While playing: the frame libserum identified, if it is another one than the frame that
    // was fed. Otherwise the view shows the frame rendered on its own, and this is what
    // libserum identifies its ROM frame as when it comes from the frame before it in the
    // project (`after`; the first frame for the first one), again only if that is another
    // frame. That is libserum's search rule (runtime::FrameIdentification) applied to the
    // project, not a render.
    struct OtherFrame {
        std::optional<std::size_t> frame;
        std::optional<std::size_t> after;
    };
    [[nodiscard]] OtherFrame identifiedOtherFrame(const document::ProjectDocument& document,
                                                  std::size_t frameIndex);
    // That as a sentence for the line above the picture, or shortened for the status line
    // ("identified as frame 7", "after frame 11: frame 7"); empty if there is nothing to say.
    [[nodiscard]] std::string identificationHint(const document::ProjectDocument& document,
                                                 std::size_t frameIndex,
                                                 bool shortText = false);
    // While playing: lets libserum step its color rotations on the frame last asked for.
    // Returns the milliseconds until it should be called again, 0 when nothing rotates.
    uint32_t rotate();

    // How often libserum was asked for a frame, for tests.
    [[nodiscard]] std::size_t renderCount() const;

private:
    bool m_enabled = false;
    runtime::PlaneRequest m_rows = runtime::PlaneRequest::Rows32;
    bool m_playing = false;
    std::vector<std::size_t> m_playOrder;
    bool m_hasFrame = false;
    uint64_t m_revision = 0;
    std::size_t m_frameIndex = 0;
    runtime::PlaneRequest m_frameRows = runtime::PlaneRequest::Rows32;
    bool m_framePlaying = false;
    // A window of this playback is loaded: the next one continues it.
    bool m_sequenceOpen = false;
    uint64_t m_sequenceRevision = 0;
    runtime::PlaneRequest m_sequenceRows = runtime::PlaneRequest::Rows32;
    runtime::RuntimeFrame m_frame;
    std::size_t m_renderCount = 0;
    // identificationHint() of the frame last asked about while not playing.
    bool m_hasStillHint = false;
    uint64_t m_stillHintRevision = 0;
    std::size_t m_stillHintFrame = 0;
    OtherFrame m_stillHint;
};

// Why libserum has no picture of a frame on the plane; empty if it has one.
[[nodiscard]] std::string runtimeNoPictureReason(const runtime::RuntimeFrame& frame,
                                                 runtime::PlaneRequest rows);

}  // namespace colorizing::app
