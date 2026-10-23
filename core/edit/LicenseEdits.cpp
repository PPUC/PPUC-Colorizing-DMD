#include "edit/LicenseEdits.h"

#include <utility>

namespace colorizing::edit {

LicenseCommand::LicenseCommand(document::ProjectLicense before, document::ProjectLicense after)
    : m_before(std::move(before)),
      m_after(std::move(after))
{
}

void LicenseCommand::apply(document::ProjectDocument& document) const
{
    document.setLicense(m_after);
}

void LicenseCommand::revert(document::ProjectDocument& document) const
{
    document.setLicense(m_before);
}

EditTarget LicenseCommand::target() const
{
    return EditTarget{EditKind::ProjectStructure, 0, render::Resolution::Sd, {}};
}

std::vector<RecordReference> LicenseCommand::recordReferences() const
{
    return {};
}

std::unique_ptr<LicenseCommand> setLicense(document::ProjectDocument& document,
                                           document::ProjectLicense license)
{
    if (license == document.license()) {
        return nullptr;
    }
    auto command = std::make_unique<LicenseCommand>(document.license(), std::move(license));
    command->apply(document);
    return command;
}

}  // namespace colorizing::edit
