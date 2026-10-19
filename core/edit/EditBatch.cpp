#include "edit/EditBatch.h"

#include <stdexcept>
#include <utility>

#include "document/ProjectDocument.h"
#include "edit/FrameContentCommand.h"
#include "edit/StateCommands.h"
#include "edit/UndoHistories.h"

namespace colorizing::edit {

namespace {

using document::ProjectDocument;
using render::Resolution;

const EditTarget kStructureTarget{EditKind::ProjectStructure, 0, Resolution::Sd, {}};

void checkFrame(const ProjectDocument& document, std::size_t frameIndex)
{
    if (frameIndex >= document.frameCount()) {
        throw std::out_of_range("frame index out of range");
    }
}

// The planes the frame has a colorized picture on.
std::vector<Resolution> colorizedPlanes(const ProjectDocument& document, std::size_t frameIndex)
{
    std::vector<Resolution> planes;
    for (Resolution plane : {Resolution::Sd, Resolution::Hd}) {
        if (!document.geometry().isEditable(plane)) {
            continue;
        }
        try {
            if (document.colorizedFrame(frameIndex, plane).isValid()) {
                planes.push_back(plane);
            }
        } catch (const std::exception&) {
            // No picture on this plane (an SD frame without HD).
        }
    }
    return planes;
}

std::unique_ptr<EditCommand> applied(std::unique_ptr<EditCommand> command,
                                     ProjectDocument& document)
{
    if (command) {
        command->apply(document);
    }
    return command;
}

}  // namespace

EditBatchCommand::EditBatchCommand(std::string name,
                                   std::vector<std::unique_ptr<EditCommand>> steps)
    : m_name(std::move(name))
    , m_steps(std::move(steps))
{
    if (m_steps.empty()) {
        throw std::invalid_argument("an edit batch needs a step");
    }
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        if (!step) {
            throw std::invalid_argument("an edit batch needs a step");
        }
    }
}

void EditBatchCommand::apply(ProjectDocument& document) const
{
    std::size_t applied = 0;
    try {
        for (; applied < m_steps.size(); ++applied) {
            m_steps[applied]->apply(document);
        }
    } catch (...) {
        // Each step leaves the document as it was when it throws; take back the others.
        while (applied > 0) {
            m_steps[--applied]->revert(document);
        }
        throw;
    }
}

void EditBatchCommand::revert(ProjectDocument& document) const
{
    std::size_t remaining = m_steps.size();
    try {
        for (; remaining > 0; --remaining) {
            m_steps[remaining - 1]->revert(document);
        }
    } catch (...) {
        for (; remaining < m_steps.size(); ++remaining) {
            m_steps[remaining]->apply(document);
        }
        throw;
    }
}

EditTarget EditBatchCommand::target() const
{
    return kStructureTarget;
}

std::vector<ReplacedData> EditBatchCommand::replacedData() const
{
    std::vector<ReplacedData> replaced;
    const HistoryKey structure = structureHistoryKey();
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        const std::vector<ReplacedData> more = step->replacedData();
        replaced.insert(replaced.end(), more.begin(), more.end());
        // The history the step would have had on its own, and for frame content also the one
        // of the dynamic zones it may change.
        const HistoryKey key = historyKey(step->target());
        if (!(key == structure)) {
            replaced.push_back(ReplacedData{key.kind, key.index, key.resolution});
        }
        if (const auto* content = dynamic_cast<const FrameContentCommand*>(step.get())) {
            for (Resolution plane : {Resolution::Sd, Resolution::Hd}) {
                replaced.push_back(ReplacedData{EditKind::FramePixels, content->frameIndex(), plane});
                replaced.push_back(ReplacedData{EditKind::DynamicMask, content->frameIndex(), plane});
            }
        }
    }
    return replaced;
}

std::vector<RecordReference> EditBatchCommand::recordReferences() const
{
    // As the structure commands: it is undone in strict order with them, see StructureCommand.
    return {};
}

std::vector<RenumberedRecords> EditBatchCommand::renumberedRecords() const
{
    std::vector<RenumberedRecords> renumbered;
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        const std::vector<RenumberedRecords> more = step->renumberedRecords();
        renumbered.insert(renumbered.end(), more.begin(), more.end());
    }
    return renumbered;
}

std::size_t EditBatchCommand::heldBytes() const
{
    std::size_t bytes = 0;
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        bytes += step->heldBytes();
    }
    return bytes;
}

const std::string& EditBatchCommand::name() const
{
    return m_name;
}

std::size_t EditBatchCommand::stepCount() const
{
    return m_steps.size();
}

bool EditBatchCommand::changesStructure() const
{
    return !renumberedRecords().empty();
}

EditBatch::EditBatch(std::string name)
    : m_name(std::move(name))
{
}

void EditBatch::add(std::unique_ptr<EditCommand> step)
{
    if (step) {
        m_steps.push_back(std::move(step));
    }
}

bool EditBatch::isEmpty() const
{
    return m_steps.empty();
}

std::unique_ptr<EditBatchCommand> EditBatch::finish()
{
    if (m_steps.empty()) {
        return nullptr;
    }
    return std::make_unique<EditBatchCommand>(std::move(m_name), std::move(m_steps));
}

void EditBatch::cancel(ProjectDocument& document)
{
    while (!m_steps.empty()) {
        m_steps.back()->revert(document);
        m_steps.pop_back();
    }
}

std::unique_ptr<EditCommand> resetPixelsToOriginal(ProjectDocument& document,
                                                   std::size_t frameIndex,
                                                   const std::vector<uint8_t>& keep)
{
    checkFrame(document, frameIndex);
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    if (keep.size() != original.pixelCount()) {
        throw std::invalid_argument("the keep mask has not the size of the ROM frame");
    }
    const std::vector<uint16_t> colors = document::plainOriginalColors(document.colorCount());
    const int romWidth = original.size.width;
    const int romHeight = original.size.height;
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (Resolution plane : colorizedPlanes(document, frameIndex)) {
        const render::Size size = document.colorizedFrame(frameIndex, plane).size;
        FrameContentBuilder builder(document, frameIndex, plane);
        for (int y = 0; y < size.height; ++y) {
            const int romY = y * romHeight / size.height;
            for (int x = 0; x < size.width; ++x) {
                const int romX = x * romWidth / size.width;
                const std::size_t romIndex = static_cast<std::size_t>(romY) *
                                                 static_cast<std::size_t>(romWidth) +
                                             static_cast<std::size_t>(romX);
                if (keep[romIndex] != 0) {
                    continue;
                }
                const uint8_t shade = original.pixels[romIndex];
                builder.setPixel(render::Point{x, y},
                                 shade < colors.size() ? colors[shade] : uint16_t{0});
            }
        }
        FrameContentChanges changes = builder.changes();
        if (!changes.isEmpty()) {
            commands.push_back(applied(std::make_unique<FrameContentCommand>(
                                           frameIndex, document.dynamicPlane(), std::move(changes)),
                                       document));
        }
    }
    return groupCommands(std::move(commands));
}

std::unique_ptr<EditCommand> setDynamicZones(ProjectDocument& document,
                                             std::size_t frameIndex,
                                             const std::vector<uint8_t>& sets)
{
    checkFrame(document, frameIndex);
    const Resolution plane = document.dynamicPlane();
    const render::Size size = document.geometry().planeSize(plane);
    if (sets.size() != static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height)) {
        throw std::invalid_argument("the dynamic zones have not the size of the dynamic plane");
    }
    for (uint8_t set : sets) {
        if (set != 255 && set >= document::kMaxDynamicSetsPerFrame) {
            throw std::invalid_argument("a dynamic set out of range");
        }
    }
    if (!document.dynamicData(frameIndex)) {
        throw std::logic_error("the frame has no dynamic data");
    }
    FrameContentBuilder builder(document, frameIndex, plane);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            builder.setDynamicMask(render::Point{x, y},
                                   sets[static_cast<std::size_t>(y) *
                                            static_cast<std::size_t>(size.width) +
                                        static_cast<std::size_t>(x)]);
        }
    }
    FrameContentChanges changes = builder.changes();
    if (changes.isEmpty()) {
        return nullptr;
    }
    return applied(std::make_unique<FrameContentCommand>(frameIndex, plane, std::move(changes),
                                                         EditKind::DynamicMask),
                   document);
}

std::unique_ptr<EditCommand> setFrameBackground(ProjectDocument& document,
                                                std::size_t frameIndex,
                                                uint16_t backgroundId)
{
    checkFrame(document, frameIndex);
    if (backgroundId != document::kNoBackgroundId && backgroundId >= document.backgroundCount()) {
        throw std::out_of_range("background index out of range");
    }
    if (!document.backgroundData(frameIndex)) {
        throw std::logic_error("the frame has no background data");
    }
    const uint32_t before =
        frameAttributeValue(document, frameIndex, FrameAttribute::BackgroundId);
    if (before == backgroundId) {
        return nullptr;
    }
    return applied(std::make_unique<FrameAttributeEditCommand>(
                       frameIndex, FrameAttribute::BackgroundId, before, backgroundId),
                   document);
}

std::unique_ptr<EditCommand> clearColorRotations(ProjectDocument& document,
                                                 std::size_t frameIndex)
{
    checkFrame(document, frameIndex);
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (Resolution plane : colorizedPlanes(document, frameIndex)) {
        document::CompactArray<uint16_t> before = document.colorRotations(frameIndex, plane);
        bool rotates = false;
        for (std::size_t slot = 0; slot < document::kMaxColorRotations; ++slot) {
            rotates = rotates || document.colorRotationLength(frameIndex, plane, slot) > 0;
        }
        if (!rotates) {
            continue;
        }
        for (std::size_t slot = 0; slot < document::kMaxColorRotations; ++slot) {
            document.clearColorRotationSlot(frameIndex, plane, slot);
        }
        commands.push_back(colorRotationsCommand(document, frameIndex, plane, std::move(before)));
    }
    return groupCommands(std::move(commands));
}

}  // namespace colorizing::edit
