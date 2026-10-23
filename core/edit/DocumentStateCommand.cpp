#include "edit/DocumentStateCommand.h"

#include <utility>

namespace colorizing::edit {

DocumentStateCommand::DocumentStateCommand(document::ProjectDocument before,
                                           document::ProjectDocument after,
                                           std::vector<std::size_t> replacedFrames)
    : m_before(std::move(before)),
      m_after(std::move(after)),
      m_replacedFrames(std::move(replacedFrames))
{
}

void DocumentStateCommand::apply(document::ProjectDocument& document) const
{
    document.restoreState(m_after);
}

void DocumentStateCommand::revert(document::ProjectDocument& document) const
{
    document.restoreState(m_before);
}

EditTarget DocumentStateCommand::target() const
{
    EditTarget target{EditKind::ProjectStructure, 0, render::Resolution::Sd, m_replacedFrames};
    for (std::size_t frame = m_before.frameCount(); frame < m_after.frameCount(); ++frame) {
        target.frameIndices.push_back(frame);
    }
    return target;
}

std::vector<ReplacedData> DocumentStateCommand::replacedData() const
{
    std::vector<ReplacedData> replaced;
    for (const std::size_t frame : m_replacedFrames) {
        for (const EditKind kind : {EditKind::FramePixels, EditKind::DynamicMask,
                                    EditKind::DynamicColor, EditKind::DynamicShadow,
                                    EditKind::BackgroundMask, EditKind::FrameAttributes}) {
            for (const render::Resolution resolution :
                 {render::Resolution::Sd, render::Resolution::Hd}) {
                replaced.push_back({kind, frame, resolution});
            }
        }
    }
    return replaced;
}

std::vector<RecordReference> DocumentStateCommand::recordReferences() const
{
    return {};
}

std::vector<RenumberedRecords> DocumentStateCommand::renumberedRecords() const
{
    const auto first = [](std::size_t before, std::size_t after) {
        return std::min(before, after);
    };
    return {{RecordKind::Frame, first(m_before.frameCount(), m_after.frameCount())},
            {RecordKind::ComparisonMask,
             first(m_before.comparisonMaskCount(), m_after.comparisonMaskCount())},
            {RecordKind::Background, first(m_before.backgroundCount(), m_after.backgroundCount())},
            {RecordKind::Sprite, first(m_before.spriteCount(), m_after.spriteCount())}};
}

}  // namespace colorizing::edit
