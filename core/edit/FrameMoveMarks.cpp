#include "edit/FrameMoveMarks.h"

#include <algorithm>

namespace colorizing::edit {

void FrameMoveMarks::mark(const document::ProjectDocument& document,
                          const std::vector<std::size_t>& frames)
{
    m_ids.clear();
    for (const std::size_t frame : frames) {
        m_ids.insert(document.frameMeta(frame).id);
    }
}

void FrameMoveMarks::clear()
{
    m_ids.clear();
}

std::vector<std::size_t> FrameMoveMarks::frames(const document::ProjectDocument& document) const
{
    std::vector<std::size_t> frames;
    if (m_ids.empty()) {
        return frames;
    }
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        if (m_ids.contains(document.frameMeta(frame).id)) {
            frames.push_back(frame);
        }
    }
    return frames;
}

FrameMovePlace frameMovePlace(std::size_t frame, bool behind)
{
    return behind ? FrameMovePlace{frame + 1, false} : FrameMovePlace{frame, true};
}

std::optional<document::ComponentId> frameLeftBehindByMove(
    const document::ProjectDocument& document, const std::vector<std::size_t>& frames)
{
    if (frames.empty()) {
        return std::nullopt;
    }
    for (std::size_t frame = frames.front() + 1; frame < document.frameCount(); ++frame) {
        if (!std::binary_search(frames.begin(), frames.end(), frame)) {
            return document.frameMeta(frame).id;
        }
    }
    if (frames.front() > 0) {
        return document.frameMeta(frames.front() - 1).id;
    }
    return std::nullopt;
}

}  // namespace colorizing::edit
