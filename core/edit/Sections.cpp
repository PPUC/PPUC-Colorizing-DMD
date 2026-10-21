#include "edit/Sections.h"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "document/EditorMetadata.h"

namespace colorizing::edit {

namespace {

using document::kMaxSections;
using document::kSectionNameBytes;

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string nameAt(const SectionTable& table, std::size_t index)
{
    return document::fixedName(table.names, index, kSectionNameBytes);
}

void requireSection(const SectionTable& table, std::size_t index)
{
    if (index >= table.count) {
        throw std::out_of_range("section index out of range");
    }
}

// Writes the table, and returns the command for the change.
std::unique_ptr<EditCommand> change(document::ProjectDocument& document,
                                    SectionTable before,
                                    SectionTable after)
{
    if (before.count == after.count && before.firstFrames == after.firstFrames &&
        before.names == after.names) {
        return nullptr;
    }
    auto command = std::make_unique<SectionsCommand>(std::move(before), std::move(after));
    command->apply(document);
    return command;
}

void swapEntries(SectionTable& table, std::size_t a, std::size_t b)
{
    std::swap(table.firstFrames[a], table.firstFrames[b]);
    std::swap_ranges(table.names.begin() + static_cast<std::ptrdiff_t>(a * kSectionNameBytes),
                     table.names.begin() + static_cast<std::ptrdiff_t>((a + 1) * kSectionNameBytes),
                     table.names.begin() + static_cast<std::ptrdiff_t>(b * kSectionNameBytes));
}

}  // namespace

SectionTable sectionTable(const document::ProjectDocument& document)
{
    const document::EditorMetadata& metadata = document.editorMetadata();
    return SectionTable{std::min<uint32_t>(metadata.sectionCount, kMaxSections),
                        metadata.sectionFirstFrames,
                        metadata.sectionNames};
}

std::vector<Section> sections(const document::ProjectDocument& document)
{
    const SectionTable table = sectionTable(document);
    std::vector<Section> result;
    for (std::size_t i = 0; i < table.count; ++i) {
        result.push_back(Section{nameAt(table, i), table.firstFrames[i]});
    }
    return result;
}

std::optional<std::size_t> sectionOfFrame(const document::ProjectDocument& document,
                                          std::size_t frameIndex)
{
    // Read in place: asked for every row of the frame list.
    const document::EditorMetadata& metadata = document.editorMetadata();
    const std::size_t count = std::min<std::size_t>(metadata.sectionCount, kMaxSections);
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t first = metadata.sectionFirstFrames[i];
        if (first <= frameIndex && (!found || first > metadata.sectionFirstFrames[*found])) {
            found = i;
        }
    }
    return found;
}

std::optional<std::size_t> sectionStartingAt(const document::ProjectDocument& document,
                                             std::size_t frameIndex)
{
    const document::EditorMetadata& metadata = document.editorMetadata();
    const std::size_t count = std::min<std::size_t>(metadata.sectionCount, kMaxSections);
    for (std::size_t i = 0; i < count; ++i) {
        if (metadata.sectionFirstFrames[i] == frameIndex) {
            return i;
        }
    }
    return std::nullopt;
}

std::string sectionName(const document::ProjectDocument& document, std::size_t sectionIndex)
{
    const document::EditorMetadata& metadata = document.editorMetadata();
    if (sectionIndex >= std::min<std::size_t>(metadata.sectionCount, kMaxSections)) {
        throw std::out_of_range("section index out of range");
    }
    return document::fixedName(metadata.sectionNames, sectionIndex, kSectionNameBytes);
}

std::string sectionNameProblem(const document::ProjectDocument& document,
                               const std::string& name,
                               std::optional<std::size_t> exceptSection)
{
    if (name.empty()) {
        return "a section needs a name";
    }
    if (name.size() >= kSectionNameBytes) {
        return "a section name has at most " + std::to_string(kSectionNameBytes - 1) +
               " characters";
    }
    if (name == "- None -") {
        return "\"- None -\" stands for no section";
    }
    const SectionTable table = sectionTable(document);
    for (std::size_t i = 0; i < table.count; ++i) {
        if (i != exceptSection && nameAt(table, i) == name) {
            return "section " + std::to_string(i) + " already has that name";
        }
    }
    return {};
}

SectionsCommand::SectionsCommand(SectionTable before, SectionTable after)
    : m_before(std::move(before)),
      m_after(std::move(after))
{
}

namespace {

void writeTable(document::ProjectDocument& document, const SectionTable& table)
{
    document::RecordTables tables = document.recordTables();
    tables.sectionCount = table.count;
    tables.sectionFirstFrames = table.firstFrames;
    tables.sectionNames = table.names;
    document.restoreRecordTables(std::move(tables));
}

}  // namespace

void SectionsCommand::apply(document::ProjectDocument& document) const
{
    writeTable(document, m_after);
}

void SectionsCommand::revert(document::ProjectDocument& document) const
{
    writeTable(document, m_before);
}

EditTarget SectionsCommand::target() const
{
    return EditTarget{EditKind::Sections, 0, render::Resolution::Sd, {}};
}

std::vector<RecordReference> SectionsCommand::recordReferences() const
{
    std::vector<RecordReference> references;
    for (const SectionTable* table : {&m_before, &m_after}) {
        for (std::size_t i = 0; i < table->count; ++i) {
            references.push_back({RecordKind::Frame, table->firstFrames[i]});
        }
    }
    return references;
}

std::unique_ptr<EditCommand> addOrRenameSection(document::ProjectDocument& document,
                                                std::size_t frameIndex,
                                                const std::string& name)
{
    if (frameIndex >= document.frameCount()) {
        throw std::out_of_range("frame index out of range");
    }
    const std::optional<std::size_t> existing = sectionStartingAt(document, frameIndex);
    if (const std::string problem = sectionNameProblem(document, name, existing);
        !problem.empty()) {
        throw std::invalid_argument(problem);
    }
    SectionTable before = sectionTable(document);
    SectionTable after = before;
    std::size_t index = 0;
    if (existing) {
        index = *existing;
    } else {
        if (after.count >= kMaxSections) {
            throw std::length_error("a project holds at most " + std::to_string(kMaxSections) +
                                    " sections");
        }
        index = after.count++;
        after.firstFrames[index] = static_cast<uint32_t>(frameIndex);
    }
    document::setFixedName(after.names, index, kSectionNameBytes, name);
    return change(document, std::move(before), std::move(after));
}

std::unique_ptr<EditCommand> deleteSection(document::ProjectDocument& document,
                                           std::size_t sectionIndex)
{
    SectionTable before = sectionTable(document);
    requireSection(before, sectionIndex);
    SectionTable after = before;
    // As Delete_Section: the entries behind move down by one; the old last entry stays filled
    // behind the count, as the Win32 editor leaves it.
    for (std::size_t i = sectionIndex; i + 1 < before.count; ++i) {
        after.firstFrames[i] = before.firstFrames[i + 1];
        std::copy_n(before.names.begin() + static_cast<std::ptrdiff_t>((i + 1) * kSectionNameBytes),
                    kSectionNameBytes,
                    after.names.begin() + static_cast<std::ptrdiff_t>(i * kSectionNameBytes));
    }
    --after.count;
    return change(document, std::move(before), std::move(after));
}

std::unique_ptr<EditCommand> moveSection(document::ProjectDocument& document,
                                         std::size_t sectionIndex,
                                         int offset)
{
    SectionTable before = sectionTable(document);
    requireSection(before, sectionIndex);
    if (offset != -1 && offset != 1) {
        throw std::invalid_argument("a section moves by one place");
    }
    if ((offset < 0 && sectionIndex == 0) || (offset > 0 && sectionIndex + 1 >= before.count)) {
        return nullptr;
    }
    SectionTable after = before;
    swapEntries(after, sectionIndex, offset < 0 ? sectionIndex - 1 : sectionIndex + 1);
    return change(document, std::move(before), std::move(after));
}

std::unique_ptr<EditCommand> sortSectionsByName(document::ProjectDocument& document)
{
    SectionTable before = sectionTable(document);
    std::vector<std::size_t> order(before.count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&before](std::size_t a, std::size_t b) {
        return lower(nameAt(before, a)) < lower(nameAt(before, b));
    });
    SectionTable after = before;
    for (std::size_t i = 0; i < order.size(); ++i) {
        after.firstFrames[i] = before.firstFrames[order[i]];
        std::copy_n(before.names.begin() +
                        static_cast<std::ptrdiff_t>(order[i] * kSectionNameBytes),
                    kSectionNameBytes,
                    after.names.begin() + static_cast<std::ptrdiff_t>(i * kSectionNameBytes));
    }
    return change(document, std::move(before), std::move(after));
}

}  // namespace colorizing::edit
