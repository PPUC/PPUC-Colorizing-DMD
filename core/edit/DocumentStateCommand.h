#pragma once

#include <cstddef>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// A step that keeps the project as it was before and after and puts one or the other back
// (a package import, a team merge): for changes too wide for a list of single edits. The two
// copies share every block that did not change, so this costs little. One step in the
// structure history. Whenever it runs it drops the histories of the frames it replaced, and
// the records from the counts before on count as renumbered.
class DocumentStateCommand : public EditCommand {
public:
    DocumentStateCommand(document::ProjectDocument before,
                         document::ProjectDocument after,
                         std::vector<std::size_t> replacedFrames);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<RenumberedRecords> renumberedRecords() const override;

    // The frames whose content the step replaced (numbered as before it), ascending.
    [[nodiscard]] const std::vector<std::size_t>& replacedFrames() const
    {
        return m_replacedFrames;
    }

private:
    document::ProjectDocument m_before;
    document::ProjectDocument m_after;
    std::vector<std::size_t> m_replacedFrames;
};

}  // namespace colorizing::edit
