#include "RuntimeView.h"

#include <algorithm>

namespace colorizing::app {

using runtime::PlaneRequest;
using runtime::RuntimePreview;

RuntimeView::~RuntimeView()
{
    stopPlayback();
}

bool RuntimeView::isEnabled() const
{
    return m_enabled;
}

void RuntimeView::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    m_hasFrame = false;
    if (!enabled) {
        stopPlayback();
    }
}

PlaneRequest RuntimeView::rows() const
{
    return m_rows;
}

void RuntimeView::setRows(PlaneRequest rows)
{
    m_rows = rows == PlaneRequest::Rows64 ? PlaneRequest::Rows64 : PlaneRequest::Rows32;
}

PlaneRequest RuntimeView::rowsFor(const render::ProjectGeometry& geometry) const
{
    return runtime::effectivePlaneRequest(geometry, m_rows);
}

render::Resolution RuntimeView::layoutPlane(const render::ProjectGeometry& geometry) const
{
    if (geometry.layout() != render::PlaneLayout::Ordinary) {
        return geometry.nativePlane();
    }
    return m_rows == PlaneRequest::Rows64 ? render::Resolution::Hd : render::Resolution::Sd;
}

void RuntimeView::startPlayback(std::vector<std::size_t> frameOrder)
{
    m_playOrder = std::move(frameOrder);
    m_playing = true;
    m_hasFrame = false;
}

void RuntimeView::stopPlayback()
{
    if (!m_playing) {
        return;
    }
    m_playing = false;
    m_playOrder.clear();
    m_hasFrame = false;
    RuntimePreview::instance().close();
}

bool RuntimeView::isPlaying() const
{
    return m_playing;
}

const runtime::RuntimeFrame& RuntimeView::frame(const document::ProjectDocument& document,
                                                std::size_t frameIndex)
{
    const PlaneRequest rows = rowsFor(document.geometry());
    if (m_hasFrame && m_revision == document.revision() && m_frameIndex == frameIndex &&
        m_frameRows == rows && m_framePlaying == m_playing) {
        return m_frame;
    }
    m_hasFrame = true;
    m_revision = document.revision();
    m_frameIndex = frameIndex;
    m_frameRows = rows;
    m_framePlaying = m_playing;
    ++m_renderCount;

    RuntimePreview& preview = RuntimePreview::instance();
    if (!m_playing) {
        m_frame = preview.renderFrame(document, frameIndex, rows);
        return m_frame;
    }
    if (!preview.sequenceHasFrame(frameIndex) || m_sequenceRevision != document.revision() ||
        m_sequenceRows != rows) {
        std::vector<std::size_t> frames;
        const auto position = std::find(m_playOrder.begin(), m_playOrder.end(), frameIndex);
        for (auto next = position; next != m_playOrder.end() && frames.size() < kSequenceFrames;
             ++next) {
            if (*next < document.frameCount()) {
                frames.push_back(*next);
            }
        }
        if (frames.empty()) {
            frames.push_back(frameIndex);
        }
        std::string errorMessage;
        if (!preview.openSequence(document, frames, rows, &errorMessage)) {
            m_frame = runtime::RuntimeFrame{};
            m_frame.error = errorMessage;
            return m_frame;
        }
        m_sequenceRevision = document.revision();
        m_sequenceRows = rows;
    }
    m_frame = preview.renderSequenceFrame(frameIndex);
    return m_frame;
}

const document::Rgb565Frame* RuntimeView::image(const document::ProjectDocument& document,
                                                std::size_t frameIndex)
{
    const runtime::RuntimeFrame& shown = frame(document, frameIndex);
    if (!shown.ok()) {
        return nullptr;
    }
    const runtime::RuntimePlane* plane = shown.plane(rowsFor(document.geometry()));
    return plane ? &plane->image : nullptr;
}

std::string RuntimeView::message(const document::ProjectDocument& document,
                                 std::size_t frameIndex)
{
    const runtime::RuntimeFrame& shown = frame(document, frameIndex);
    if (!shown.ok()) {
        return "libserum: " + shown.error;
    }
    if (shown.plane(rowsFor(document.geometry()))) {
        return {};
    }
    if (!shown.identified) {
        return "libserum does not identify this frame.";
    }
    return "libserum identifies this frame and shows no picture for it.";
}

uint32_t RuntimeView::rotate()
{
    if (!m_playing || !m_hasFrame || !m_frame.ok()) {
        return 0;
    }
    runtime::RuntimeFrame rotated = RuntimePreview::instance().rotate();
    if (!rotated.ok()) {
        return 0;
    }
    const bool identified = m_frame.identified;
    const std::optional<std::size_t> identifiedFrame = m_frame.frameIndex;
    m_frame = std::move(rotated);
    m_frame.identified = identified;
    m_frame.frameIndex = identifiedFrame;
    return m_frame.nextRotationMs;
}

std::size_t RuntimeView::renderCount() const
{
    return m_renderCount;
}

}  // namespace colorizing::app
