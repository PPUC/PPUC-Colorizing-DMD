#pragma once

#include <cstddef>
#include <cstdint>
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
    // Frames of one loaded playback sequence. Playback past them loads the next ones, which
    // starts libserum's state afresh.
    static constexpr std::size_t kSequenceFrames = 48;

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
    uint64_t m_sequenceRevision = 0;
    runtime::PlaneRequest m_sequenceRows = runtime::PlaneRequest::Rows32;
    runtime::RuntimeFrame m_frame;
    std::size_t m_renderCount = 0;
};

}  // namespace colorizing::app
