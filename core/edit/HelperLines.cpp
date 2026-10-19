#include "edit/HelperLines.h"

namespace colorizing::edit {

HelperLinesCommand::HelperLinesCommand(std::vector<document::HelperLine> before,
                                       std::vector<document::HelperLine> after)
    : m_before(std::move(before)),
      m_after(std::move(after))
{
}

void HelperLinesCommand::apply(document::ProjectDocument& document) const
{
    document.setHelperLines(m_after);
}

void HelperLinesCommand::revert(document::ProjectDocument& document) const
{
    document.setHelperLines(m_before);
}

EditTarget HelperLinesCommand::target() const
{
    return EditTarget{EditKind::HelperLines, 0, render::Resolution::Sd, {}};
}

std::vector<RecordReference> HelperLinesCommand::recordReferences() const
{
    return {};
}

std::unique_ptr<EditCommand> setHelperLines(document::ProjectDocument& document,
                                            std::vector<document::HelperLine> lines)
{
    if (lines == document.helperLines()) {
        return nullptr;
    }
    auto command = std::make_unique<HelperLinesCommand>(document.helperLines(), std::move(lines));
    command->apply(document);
    return command;
}

}  // namespace colorizing::edit
