#include "edit/FrameMove.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace colorizing::edit {

namespace {

std::vector<std::size_t> inverse(const std::vector<std::size_t>& order)
{
    std::vector<std::size_t> result(order.size());
    for (std::size_t position = 0; position < order.size(); ++position) {
        result[order[position]] = position;
    }
    return result;
}

}  // namespace

FrameMoveCommand::FrameMoveCommand(std::vector<std::size_t> newOrder,
                                   std::vector<std::size_t> movedFrames,
                                   document::RecordTables tablesBefore,
                                   document::RecordTables tablesAfter)
    : m_newOrder(std::move(newOrder)),
      m_inverseOrder(inverse(m_newOrder)),
      m_movedFrames(std::move(movedFrames)),
      m_tablesBefore(std::move(tablesBefore)),
      m_tablesAfter(std::move(tablesAfter))
{
}

void FrameMoveCommand::apply(document::ProjectDocument& document) const
{
    document.reorderFrames(m_newOrder);
    document.restoreRecordTables(m_tablesAfter);
    m_applied = true;
}

void FrameMoveCommand::revert(document::ProjectDocument& document) const
{
    document.reorderFrames(m_inverseOrder);
    document.restoreRecordTables(m_tablesBefore);
    m_applied = false;
}

EditTarget FrameMoveCommand::target() const
{
    return EditTarget{EditKind::ProjectStructure, 0, render::Resolution::Sd, {}};
}

std::vector<ReplacedData> FrameMoveCommand::replacedData() const
{
    return {};
}

std::vector<RecordReference> FrameMoveCommand::recordReferences() const
{
    return {};
}

std::vector<RenumberedRecords> FrameMoveCommand::renumberedRecords() const
{
    // The first frame whose number changes.
    std::size_t first = 0;
    while (first < m_newOrder.size() && m_newOrder[first] == first) {
        ++first;
    }
    return {{RecordKind::Frame, first}};
}

std::vector<std::size_t> FrameMoveCommand::movedFrames() const
{
    if (!m_applied) {
        return m_movedFrames;
    }
    std::vector<std::size_t> moved;
    moved.reserve(m_movedFrames.size());
    for (std::size_t frame : m_movedFrames) {
        moved.push_back(m_inverseOrder[frame]);
    }
    std::sort(moved.begin(), moved.end());
    return moved;
}

bool FrameMoveCommand::isApplied() const
{
    return m_applied;
}

std::unique_ptr<FrameMoveCommand> moveFrames(document::ProjectDocument& document,
                                             const std::vector<std::size_t>& frameIndices,
                                             std::size_t beforeFrame,
                                             bool joinSection)
{
    const std::size_t count = document.frameCount();
    std::vector<std::size_t> moved = frameIndices;
    std::sort(moved.begin(), moved.end());
    moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
    if (moved.empty()) {
        return nullptr;
    }
    if (moved.back() >= count || beforeFrame > count) {
        throw std::out_of_range("frame index out of range");
    }
    std::vector<bool> isMoved(count, false);
    for (std::size_t frame : moved) {
        isMoved[frame] = true;
    }
    // Before a moved frame: before the next one that stays.
    while (beforeFrame < count && isMoved[beforeFrame]) {
        ++beforeFrame;
    }

    std::vector<std::size_t> newOrder;
    newOrder.reserve(count);
    for (std::size_t frame = 0; frame < count; ++frame) {
        if (frame == beforeFrame) {
            newOrder.insert(newOrder.end(), moved.begin(), moved.end());
        }
        if (!isMoved[frame]) {
            newOrder.push_back(frame);
        }
    }
    if (beforeFrame == count) {
        newOrder.insert(newOrder.end(), moved.begin(), moved.end());
    }
    bool unchanged = true;
    for (std::size_t position = 0; position < count && unchanged; ++position) {
        unchanged = newOrder[position] == position;
    }
    if (unchanged) {
        return nullptr;
    }
    const std::vector<std::size_t> newIndex = inverse(newOrder);

    // The sections, worked out on the numbering before the move.
    const document::RecordTables before = document.recordTables();
    document::RecordTables after = before;
    const std::size_t sectionCount =
        std::min<std::size_t>(before.sectionCount, before.sectionFirstFrames.size());
    const auto sectionEnd = [&](std::size_t first) {
        std::size_t end = count;
        for (std::size_t other = 0; other < sectionCount; ++other) {
            const std::size_t otherFirst = before.sectionFirstFrames[other];
            if (otherFirst > first && otherFirst < end) {
                end = otherFirst;
            }
        }
        return end;
    };
    bool movedBringSection = false;
    for (std::size_t section = 0; section < sectionCount; ++section) {
        const std::size_t first = before.sectionFirstFrames[section];
        if (first >= count) {
            continue;
        }
        std::size_t start = first;
        if (isMoved[first]) {
            const std::size_t end = sectionEnd(first);
            std::size_t staying = first;
            while (staying < end && isMoved[staying]) {
                ++staying;
            }
            if (staying < end) {
                start = staying;
            } else if (first == moved.front()) {
                movedBringSection = true;
            }
        }
        after.sectionFirstFrames[section] = static_cast<uint32_t>(newIndex[start]);
    }
    if (joinSection && beforeFrame < count && !movedBringSection) {
        for (std::size_t section = 0; section < sectionCount; ++section) {
            if (before.sectionFirstFrames[section] == beforeFrame) {
                after.sectionFirstFrames[section] = static_cast<uint32_t>(newIndex[moved.front()]);
            }
        }
    }
    for (std::size_t sprite = 0; sprite < document.spriteCount(); ++sprite) {
        const std::size_t source = before.spriteSourceFrames[sprite];
        if (source < count) {
            after.spriteSourceFrames[sprite] = static_cast<uint32_t>(newIndex[source]);
        }
    }

    auto command =
        std::make_unique<FrameMoveCommand>(newOrder, moved, before, std::move(after));
    command->apply(document);
    return command;
}

}  // namespace colorizing::edit
