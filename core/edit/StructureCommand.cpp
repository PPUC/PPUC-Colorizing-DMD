#include "edit/StructureCommand.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace colorizing::edit {

StructureCommand::StructureCommand(RecordKind kind,
                                   std::vector<std::size_t> indices,
                                   bool addsRecords,
                                   std::optional<document::RecordTables> tablesWithout)
    : m_kind(kind),
      m_indices(std::move(indices)),
      m_addsRecords(addsRecords),
      m_tablesWithout(std::move(tablesWithout)),
      m_present(true)
{
}

void StructureCommand::apply(document::ProjectDocument& document) const
{
    if (m_addsRecords) {
        putRecords(document);
    } else {
        takeRecords(document);
    }
}

void StructureCommand::revert(document::ProjectDocument& document) const
{
    if (m_addsRecords) {
        takeRecords(document);
    } else {
        putRecords(document);
    }
}

void StructureCommand::takeRecords(document::ProjectDocument& document) const
{
    if (!m_present) {
        throw std::logic_error("the records are not in the document");
    }
    m_unassignedFrames = take(document);
    if (m_tablesWithout) {
        document.restoreRecordTables(*m_tablesWithout);
    }
    m_present = false;
}

void StructureCommand::putRecords(document::ProjectDocument& document) const
{
    if (m_present) {
        throw std::logic_error("the records are in the document already");
    }
    put(document);
    m_present = true;
}

EditTarget StructureCommand::target() const
{
    return EditTarget{EditKind::ProjectStructure, 0, render::Resolution::Sd, {}};
}

std::vector<ReplacedData> StructureCommand::replacedData() const
{
    // Frames whose mask or background assignment this command cleared or puts back: an
    // edit of that setting recorded in between no longer describes the frame.
    std::vector<ReplacedData> replaced;
    if (m_kind == RecordKind::ComparisonMask || m_kind == RecordKind::Background) {
        for (std::size_t frameIndex : m_unassignedFrames) {
            replaced.push_back({EditKind::FrameAttributes, frameIndex, render::Resolution::Sd});
        }
    }
    return replaced;
}

std::vector<RecordReference> StructureCommand::recordReferences() const
{
    return {};
}

std::vector<RenumberedRecords> StructureCommand::renumberedRecords() const
{
    return {{m_kind, m_indices.front()}};
}

RecordKind StructureCommand::recordKind() const
{
    return m_kind;
}

bool StructureCommand::addsRecords() const
{
    return m_addsRecords;
}

const std::vector<std::size_t>& StructureCommand::recordIndices() const
{
    return m_indices;
}

bool StructureCommand::recordsPresent() const
{
    return m_present;
}

const std::vector<std::size_t>& StructureCommand::unassignedFrames() const
{
    return m_unassignedFrames;
}

namespace {

class FramesCommand final : public StructureCommand {
public:
    FramesCommand(std::vector<std::size_t> frameIndices,
                  bool addsRecords,
                  std::optional<document::RecordTables> tablesWithout)
        : StructureCommand(RecordKind::Frame, std::move(frameIndices), addsRecords,
                           std::move(tablesWithout))
    {
    }

private:
    std::vector<std::size_t> take(document::ProjectDocument& document) const override
    {
        m_removed = document.removeFrames(recordIndices());
        return {};
    }

    void put(document::ProjectDocument& document) const override
    {
        document.insertFrames(std::move(m_removed));
        m_removed = {};
    }

    std::size_t heldBytes() const override
    {
        std::size_t bytes = 0;
        for (const document::FrameRecord& frame : m_removed.frames) {
            bytes += document::ownedBytes(frame);
        }
        return bytes;
    }

    mutable document::RemovedFrames m_removed;
};

class SpriteCommand final : public StructureCommand {
public:
    SpriteCommand(std::size_t spriteIndex,
                  bool addsRecords,
                  std::optional<document::RecordTables> tablesWithout)
        : StructureCommand(RecordKind::Sprite, {spriteIndex}, addsRecords,
                           std::move(tablesWithout))
    {
    }

private:
    std::vector<std::size_t> take(document::ProjectDocument& document) const override
    {
        m_removed = document.removeSprite(recordIndices().front());
        std::vector<std::size_t> frames;
        for (const document::RemovedSpriteReference& reference : m_removed.references) {
            if (frames.empty() || frames.back() != reference.frameIndex) {
                frames.push_back(reference.frameIndex);
            }
        }
        return frames;
    }

    void put(document::ProjectDocument& document) const override
    {
        document.insertSprite(std::move(m_removed));
        m_removed = {};
    }

    std::size_t heldBytes() const override
    {
        return recordsPresent() ? 0 : document::ownedBytes(m_removed.sprite);
    }

    mutable document::RemovedSprite m_removed;
};

class BackgroundCommand final : public StructureCommand {
public:
    BackgroundCommand(std::size_t backgroundIndex, bool addsRecords)
        : StructureCommand(RecordKind::Background, {backgroundIndex}, addsRecords, std::nullopt)
    {
    }

private:
    std::vector<std::size_t> take(document::ProjectDocument& document) const override
    {
        m_removed = document.removeBackground(recordIndices().front());
        return m_removed.frames;
    }

    void put(document::ProjectDocument& document) const override
    {
        document.insertBackground(std::move(m_removed));
        m_removed = {};
    }

    std::size_t heldBytes() const override
    {
        return recordsPresent() ? 0 : document::ownedBytes(m_removed.background);
    }

    mutable document::RemovedBackground m_removed;
};

class ComparisonMaskCommand final : public StructureCommand {
public:
    ComparisonMaskCommand(std::size_t maskIndex,
                          bool addsRecords,
                          std::optional<document::RecordTables> tablesWithout)
        : StructureCommand(RecordKind::ComparisonMask, {maskIndex}, addsRecords,
                           std::move(tablesWithout))
    {
    }

private:
    std::vector<std::size_t> take(document::ProjectDocument& document) const override
    {
        m_removed = document.removeComparisonMask(recordIndices().front());
        return m_removed.frames;
    }

    void put(document::ProjectDocument& document) const override
    {
        document.insertComparisonMask(std::move(m_removed));
        m_removed = {};
    }

    std::size_t heldBytes() const override
    {
        return recordsPresent() ? 0 : m_removed.mask.pixels.ownedBytes();
    }

    mutable document::RemovedComparisonMask m_removed;
};

// A new mask with its content, assigned to frames that had other masks before.
class NewComparisonMaskCommand final : public StructureCommand {
public:
    NewComparisonMaskCommand(std::size_t maskIndex,
                             document::RecordTables tablesWithout,
                             std::vector<std::pair<std::size_t, uint8_t>> previousMasks)
        : StructureCommand(RecordKind::ComparisonMask, {maskIndex}, true,
                           std::move(tablesWithout)),
          m_previousMasks(std::move(previousMasks))
    {
    }

private:
    std::vector<std::size_t> take(document::ProjectDocument& document) const override
    {
        m_removed = document.removeComparisonMask(recordIndices().front());
        // The new mask is the last one: the masks the frames had keep their numbers.
        for (const auto& [frameIndex, maskId] : m_previousMasks) {
            document.setComparisonMaskId(frameIndex, maskId);
        }
        return m_removed.frames;
    }

    void put(document::ProjectDocument& document) const override
    {
        document.insertComparisonMask(std::move(m_removed));
        m_removed = {};
    }

    std::size_t heldBytes() const override
    {
        return recordsPresent() ? 0 : m_removed.mask.pixels.ownedBytes();
    }

    std::vector<std::pair<std::size_t, uint8_t>> m_previousMasks;
    mutable document::RemovedComparisonMask m_removed;
};

template <typename Command, typename... Arguments>
std::unique_ptr<StructureCommand> applied(document::ProjectDocument& document,
                                          Arguments&&... arguments)
{
    auto command = std::make_unique<Command>(std::forward<Arguments>(arguments)...);
    command->apply(document);
    return command;
}

}  // namespace

std::vector<std::size_t> framesShorterThan(const document::ProjectDocument& document,
                                           const std::vector<std::size_t>& frameIndices,
                                           uint32_t durationMs)
{
    std::vector<std::size_t> frames;
    for (std::size_t frameIndex : frameIndices) {
        if (document.frameDurationMs(frameIndex) < durationMs) {
            frames.push_back(frameIndex);
        }
    }
    return frames;
}

SpriteNameCheck checkSpriteName(const document::ProjectDocument& document,
                                const std::string& name,
                                std::optional<std::size_t> renamedSprite)
{
    if (name.empty()) {
        return SpriteNameCheck::Empty;
    }
    if (name.size() >= document::kSpriteNameBytes) {
        return SpriteNameCheck::TooLong;
    }
    for (std::size_t sprite = 0; sprite < document.spriteCount(); ++sprite) {
        if (sprite != renamedSprite && document.sprite(sprite).name == name) {
            return SpriteNameCheck::Used;
        }
    }
    return SpriteNameCheck::Usable;
}

std::string spriteNameProblem(const document::ProjectDocument& document, const std::string& name)
{
    switch (checkSpriteName(document, name)) {
    case SpriteNameCheck::Usable:
        return {};
    case SpriteNameCheck::Empty:
        return "A sprite needs a name";
    case SpriteNameCheck::TooLong:
        return "A sprite name has at most 31 characters";
    case SpriteNameCheck::Used:
        return "Another sprite has this name already";
    }
    return {};
}

std::optional<std::size_t> frameIndexAfterDeletion(std::size_t frameIndex,
                                                   const std::vector<std::size_t>& deletedFrames)
{
    std::size_t before = 0;
    for (const std::size_t deleted : std::set<std::size_t>(deletedFrames.begin(),
                                                            deletedFrames.end())) {
        if (deleted == frameIndex) {
            return std::nullopt;
        }
        if (deleted < frameIndex) {
            ++before;
        }
    }
    return frameIndex - before;
}

std::unique_ptr<StructureCommand> deleteFrames(document::ProjectDocument& document,
                                               const std::vector<std::size_t>& frameIndices)
{
    std::vector<std::size_t> indices = frameIndices;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.empty()) {
        return nullptr;
    }
    return applied<FramesCommand>(document, std::move(indices), false, std::nullopt);
}

std::unique_ptr<StructureCommand> addFrames(document::ProjectDocument& document,
                                            std::vector<document::FrameRecord> frames)
{
    if (frames.empty()) {
        return nullptr;
    }
    document::RemovedFrames added;
    added.indices.reserve(frames.size());
    for (std::size_t offset = 0; offset < frames.size(); ++offset) {
        added.indices.push_back(document.frameCount() + offset);
    }
    std::vector<std::size_t> indices = added.indices;
    added.frames = std::move(frames);
    // Frames behind the last one start no section and are no sprite's source.
    added.tables = document.recordTables();
    document::RecordTables tablesWithout = added.tables;
    document.insertFrames(std::move(added));
    return std::make_unique<FramesCommand>(std::move(indices), true, std::move(tablesWithout));
}

std::unique_ptr<StructureCommand> addSprite(document::ProjectDocument& document,
                                            const std::string& name,
                                            std::size_t sourceFrameIndex)
{
    const std::string problem = spriteNameProblem(document, name);
    if (!problem.empty()) {
        throw std::invalid_argument(problem);
    }
    document::RecordTables tablesWithout = document.recordTables();
    const std::size_t spriteIndex =
        document.appendSprite(document.makeBlankSprite(name), sourceFrameIndex);
    return std::make_unique<SpriteCommand>(spriteIndex, true, std::move(tablesWithout));
}

std::unique_ptr<StructureCommand> deleteSprite(document::ProjectDocument& document,
                                               std::size_t spriteIndex)
{
    return applied<SpriteCommand>(document, spriteIndex, false, std::nullopt);
}

std::unique_ptr<StructureCommand> addBackgroundFromFrame(document::ProjectDocument& document,
                                                         std::size_t frameIndex)
{
    const std::size_t backgroundIndex =
        document.appendBackground(document.makeBackgroundFromFrame(frameIndex));
    return std::make_unique<BackgroundCommand>(backgroundIndex, true);
}

std::unique_ptr<StructureCommand> deleteBackground(document::ProjectDocument& document,
                                                   std::size_t backgroundIndex)
{
    return applied<BackgroundCommand>(document, backgroundIndex, false);
}

std::unique_ptr<StructureCommand> addComparisonMask(document::ProjectDocument& document)
{
    document::RecordTables tablesWithout = document.recordTables();
    const std::size_t maskIndex = document.appendComparisonMask();
    return std::make_unique<ComparisonMaskCommand>(maskIndex, true, std::move(tablesWithout));
}

std::unique_ptr<StructureCommand> addComparisonMaskForFrames(
    document::ProjectDocument& document,
    const document::IndexedFrame& mask,
    const std::vector<std::size_t>& frameIndices)
{
    const document::Size size = document.originalFrameSize();
    if (mask.size.width != size.width || mask.size.height != size.height ||
        mask.pixels.size() != static_cast<std::size_t>(size.width) * size.height) {
        throw std::invalid_argument("the mask has not the size of the ROM frame");
    }
    std::vector<std::pair<std::size_t, uint8_t>> previousMasks;
    for (std::size_t frameIndex : frameIndices) {
        previousMasks.emplace_back(frameIndex, document.comparisonMaskId(frameIndex));
    }
    document::RecordTables tablesWithout = document.recordTables();
    const std::size_t maskIndex = document.appendComparisonMask();
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const uint8_t value = mask.pixels[static_cast<std::size_t>(y) * size.width + x];
            if (value != 0) {
                document.setComparisonMaskPixel(maskIndex, x, y, 1);
            }
        }
    }
    for (std::size_t frameIndex : frameIndices) {
        document.setComparisonMaskId(frameIndex, static_cast<uint8_t>(maskIndex));
    }
    return std::make_unique<NewComparisonMaskCommand>(maskIndex, std::move(tablesWithout),
                                                      std::move(previousMasks));
}

std::unique_ptr<StructureCommand> deleteComparisonMask(document::ProjectDocument& document,
                                                       std::size_t maskIndex)
{
    return applied<ComparisonMaskCommand>(document, maskIndex, false, std::nullopt);
}

}  // namespace colorizing::edit
