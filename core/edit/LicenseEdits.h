#pragma once

#include <memory>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectLicense.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// The project's license (ProjectDocument::license()). One step in the structure history.
class LicenseCommand final : public EditCommand {
public:
    LicenseCommand(document::ProjectLicense before, document::ProjectLicense after);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;

private:
    document::ProjectLicense m_before;
    document::ProjectLicense m_after;
};

// Sets the license and returns the command, already applied; nullptr when nothing changes.
// std::invalid_argument for a kind that does not exist, the document left as it was.
[[nodiscard]] std::unique_ptr<LicenseCommand> setLicense(document::ProjectDocument& document,
                                                         document::ProjectLicense license);

}  // namespace colorizing::edit
