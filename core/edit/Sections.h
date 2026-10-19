#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Sections name places in the frame list: each has a name and the frame it starts at. They are
// editor data of the .cRP (the runtime does not use them), kept in a list of their own order,
// which need not be the order of their first frames. As in the Win32 editor (Which_Section,
// is_Section_First, IDC_ADDSECTION, IDC_DELSECTION, the section organizer).

struct Section {
    std::string name;
    std::size_t firstFrame = 0;
};

[[nodiscard]] std::vector<Section> sections(const document::ProjectDocument& document);
// The section a frame belongs to: the one with the latest first frame at or before it (the
// earlier one in the list when two start at the same frame); none before the first section.
[[nodiscard]] std::optional<std::size_t> sectionOfFrame(const document::ProjectDocument& document,
                                                        std::size_t frameIndex);
// The section that starts at the frame, if one does (the earlier one in the list).
[[nodiscard]] std::optional<std::size_t> sectionStartingAt(
    const document::ProjectDocument& document,
    std::size_t frameIndex);

[[nodiscard]] std::string sectionName(const document::ProjectDocument& document,
                                      std::size_t sectionIndex);

// Why the name cannot be given to a section; empty if it can. A name has 1 to 31 bytes, is not
// "- None -" (the Win32 editor's entry for no section) and is not the name of another section
// (exceptSection may keep its own).
[[nodiscard]] std::string sectionNameProblem(const document::ProjectDocument& document,
                                             const std::string& name,
                                             std::optional<std::size_t> exceptSection = {});

// The section part of the cRP tables.
struct SectionTable {
    uint32_t count = 0;
    std::vector<uint32_t> firstFrames;
    std::vector<uint8_t> names;
};

[[nodiscard]] SectionTable sectionTable(const document::ProjectDocument& document);

// Any change of the sections, in one history for the project ({Sections, 0}). It names the
// first frames as records, so a frame deletion, which moves them, drops it.
class SectionsCommand final : public EditCommand {
public:
    SectionsCommand(SectionTable before, SectionTable after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    SectionTable m_before;
    SectionTable m_after;
};

// Each runs the operation and returns its command, already applied, or nullptr when nothing
// changed. On failure the document is left as it was and the exception passed on:
// std::invalid_argument for a name sectionNameProblem() objects to, std::length_error at 512
// sections, std::out_of_range for a section or frame that does not exist.
//
// Names the section that starts at the frame, or adds one there behind the others.
[[nodiscard]] std::unique_ptr<EditCommand> addOrRenameSection(document::ProjectDocument& document,
                                                              std::size_t frameIndex,
                                                              const std::string& name);
[[nodiscard]] std::unique_ptr<EditCommand> deleteSection(document::ProjectDocument& document,
                                                         std::size_t sectionIndex);
// Swaps the section with its neighbour before (offset -1) or behind (+1) in the list.
[[nodiscard]] std::unique_ptr<EditCommand> moveSection(document::ProjectDocument& document,
                                                       std::size_t sectionIndex,
                                                       int offset);
// Sorts the list by name, ignoring case.
[[nodiscard]] std::unique_ptr<EditCommand> sortSectionsByName(document::ProjectDocument& document);

// The frames that have a trigger ID, sorted by trigger ID and then frame, as the Win32
// editor's trigger list.
struct TriggerEntry {
    std::size_t frameIndex = 0;
    uint32_t triggerId = 0;
};

[[nodiscard]] std::vector<TriggerEntry> triggerList(const document::ProjectDocument& document);

}  // namespace colorizing::edit
