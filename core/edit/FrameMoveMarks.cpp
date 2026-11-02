#include "edit/FrameMoveMarks.h"

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

}  // namespace colorizing::edit
