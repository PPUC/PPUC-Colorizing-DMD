#pragma once

#include <vector>

#include "document/HelperLine.h"
#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Any change of the helper lines (adding, editing, deleting, showing or hiding one), in one
// history for the project ({HelperLines, 0}). They name no record.
class HelperLinesCommand final : public EditCommand {
public:
    HelperLinesCommand(std::vector<document::HelperLine> before,
                       std::vector<document::HelperLine> after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    std::vector<document::HelperLine> m_before;
    std::vector<document::HelperLine> m_after;
};

// Sets the lines and returns the command, already applied; nullptr when they are the same.
[[nodiscard]] std::unique_ptr<EditCommand> setHelperLines(document::ProjectDocument& document,
                                                          std::vector<document::HelperLine> lines);

}  // namespace colorizing::edit
