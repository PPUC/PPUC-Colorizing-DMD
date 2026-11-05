#include "edit/FrameMove.h"

#include <algorithm>
#include <optional>
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

// The frames to move, ascending and unique, and where they go: the frame they go before,
// past moved frames, or the frame count for behind the last one.
struct MovePlan {
    std::vector<std::size_t> moved;
    std::vector<bool> isMoved;
    std::size_t beforeFrame = 0;
    std::vector<std::size_t> newOrder;
};

// Nothing when no frame is given or the order would not change.
std::optional<MovePlan> planMove(std::size_t count,
                                 const std::vector<std::size_t>& frameIndices,
                                 std::size_t beforeFrame)
{
    MovePlan plan;
    plan.moved = frameIndices;
    std::vector<std::size_t>& moved = plan.moved;
    std::sort(moved.begin(), moved.end());
    moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
    if (moved.empty()) {
        return std::nullopt;
    }
    if (moved.back() >= count || beforeFrame > count) {
        throw std::out_of_range("frame index out of range");
    }
    plan.isMoved.assign(count, false);
    for (std::size_t frame : moved) {
        plan.isMoved[frame] = true;
    }
    // Before a moved frame: before the next one that stays.
    while (beforeFrame < count && plan.isMoved[beforeFrame]) {
        ++beforeFrame;
    }
    plan.beforeFrame = beforeFrame;

    std::vector<std::size_t>& newOrder = plan.newOrder;
    newOrder.reserve(count);
    for (std::size_t frame = 0; frame < count; ++frame) {
        if (frame == beforeFrame) {
            newOrder.insert(newOrder.end(), moved.begin(), moved.end());
        }
        if (!plan.isMoved[frame]) {
            newOrder.push_back(frame);
        }
    }
    if (beforeFrame == count) {
        newOrder.insert(newOrder.end(), moved.begin(), moved.end());
    }
    for (std::size_t position = 0; position < count; ++position) {
        if (newOrder[position] != position) {
            return plan;
        }
    }
    return std::nullopt;
}

// The signing notice stays the last frame: it does not move, and frames moved behind the last
// frame go before it.
std::optional<MovePlan> planMoveIn(const document::ProjectDocument& document,
                                   std::vector<std::size_t> frameIndices,
                                   std::size_t beforeFrame)
{
    const std::size_t count = document.frameCount();
    if (count > 0 && document.isSigningNoticeFrame(count - 1)) {
        frameIndices.erase(std::remove(frameIndices.begin(), frameIndices.end(), count - 1),
                           frameIndices.end());
        if (beforeFrame == count) {
            beforeFrame = count - 1;
        }
    }
    return planMove(count, frameIndices, beforeFrame);
}

// The frame behind the last frame of the section that starts at `first`: the next first frame
// of another section, or the frame count.
std::size_t sectionEnd(const document::RecordTables& tables,
                       std::size_t sectionCount,
                       std::size_t count,
                       std::size_t first)
{
    std::size_t end = count;
    for (std::size_t other = 0; other < sectionCount; ++other) {
        const std::size_t otherFirst = tables.sectionFirstFrames[other];
        if (otherFirst > first && otherFirst < end) {
            end = otherFirst;
        }
    }
    return end;
}

// The first frame of the section that stays, or the end of the section when all move.
std::size_t firstStaying(const MovePlan& plan, std::size_t first, std::size_t end)
{
    std::size_t staying = first;
    while (staying < end && plan.isMoved[staying]) {
        ++staying;
    }
    return staying;
}

}  // namespace

std::vector<std::size_t> sectionsSplitByMove(const document::ProjectDocument& document,
                                             const std::vector<std::size_t>& frameIndices,
                                             std::size_t beforeFrame)
{
    const std::size_t count = document.frameCount();
    const std::optional<MovePlan> plan = planMoveIn(document, frameIndices, beforeFrame);
    if (!plan) {
        return {};
    }
    const document::RecordTables tables = document.recordTables();
    const std::size_t sectionCount =
        std::min<std::size_t>(tables.sectionCount, tables.sectionFirstFrames.size());
    std::vector<std::size_t> split;
    for (std::size_t section = 0; section < sectionCount; ++section) {
        const std::size_t first = tables.sectionFirstFrames[section];
        if (first < count && plan->isMoved[first]) {
            const std::size_t end = sectionEnd(tables, sectionCount, count, first);
            if (firstStaying(*plan, first, end) < end) {
                split.push_back(section);
            }
        }
    }
    return split;
}

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
                                             bool joinSection,
                                             SplitSectionStart splitSections)
{
    const std::size_t count = document.frameCount();
    std::optional<MovePlan> plan = planMoveIn(document, frameIndices, beforeFrame);
    if (!plan) {
        return nullptr;
    }
    const std::vector<std::size_t>& moved = plan->moved;
    beforeFrame = plan->beforeFrame;
    const std::vector<std::size_t> newIndex = inverse(plan->newOrder);

    // The sections, worked out on the numbering before the move.
    const document::RecordTables before = document.recordTables();
    document::RecordTables after = before;
    const std::size_t sectionCount =
        std::min<std::size_t>(before.sectionCount, before.sectionFirstFrames.size());
    bool movedBringSection = false;
    for (std::size_t section = 0; section < sectionCount; ++section) {
        const std::size_t first = before.sectionFirstFrames[section];
        if (first >= count) {
            continue;
        }
        std::size_t start = first;
        if (plan->isMoved[first]) {
            const std::size_t end = sectionEnd(before, sectionCount, count, first);
            const std::size_t staying = firstStaying(*plan, first, end);
            const bool goes =
                staying == end || splitSections == SplitSectionStart::GoesWithMovedFrames;
            if (!goes) {
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

    auto command = std::make_unique<FrameMoveCommand>(std::move(plan->newOrder), moved, before,
                                                      std::move(after));
    command->apply(document);
    return command;
}

}  // namespace colorizing::edit
