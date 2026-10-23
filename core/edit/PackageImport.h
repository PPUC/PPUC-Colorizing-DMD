#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// Packages: frames of one project with what they use, for another project (attract mode
// frames of a vendor, fonts as sprites, test frames). A package is written as a project file
// with the kind "package" (project::ProjectFileOptions::package).

// The frames, in the order given, with the comparison masks (and their names), backgrounds,
// sprites (and their names) and light effects they use, renumbered as
// ProjectDocument::toSubsetSnapshot() does; the project's ID and license come along. Scenes,
// sections, palettes and the editor's other data stay behind: the frames of a scene come as
// ordinary frames. Throws std::out_of_range for a frame that does not exist.
[[nodiscard]] document::ProjectSnapshot makePackage(const document::ProjectDocument& document,
                                                    const std::vector<std::size_t>& frames);

// What an import of the package into the project would do.
struct PackageImportPlan {
    // Why the package cannot go into the project (another frame layout, other colors, too
    // many records); empty when it can.
    std::string problem;
    // Per frame of the package: the frame of the project that is the same, if any. The same
    // is the frame with its ID, or else the first frame libserum would identify by the same
    // ROM frame: the same original under a comparison mask of the same pixels (or none) in
    // the same shape mode.
    std::vector<std::optional<std::size_t>> knownFrames;
    // Per comparison mask, background and sprite of the package: the one of the project with
    // the same content, used instead of adding it again.
    std::vector<std::optional<std::size_t>> maskTargets;
    std::vector<std::optional<std::size_t>> backgroundTargets;
    std::vector<std::optional<std::size_t>> spriteTargets;
    // The names of who changed the package's frames, sorted, each once.
    std::vector<std::string> authors;

    [[nodiscard]] std::size_t knownFrameCount() const;
};

[[nodiscard]] PackageImportPlan planPackageImport(const document::ProjectDocument& document,
                                                  const document::ProjectSnapshot& package);

// What happens to the frames of the package the project has already.
enum class KnownFrames {
    // Their content replaces the project's frame, which keeps its place and ID.
    Update,
    // They are left out.
    Skip,
    // They are added as new frames like the others.
    Add,
};

// The import as one step in the structure history. It keeps the project as it was before
// and after (the two share every block that did not change, so this costs little) and puts
// one or the other back. Whenever it runs it drops the histories of the frames it replaced.
class PackageImportCommand final : public EditCommand {
public:
    struct Counts {
        std::size_t addedFrames = 0;
        std::size_t updatedFrames = 0;
        std::size_t addedMasks = 0;
        std::size_t addedBackgrounds = 0;
        std::size_t addedSprites = 0;
    };

    PackageImportCommand(document::ProjectDocument before,
                         document::ProjectDocument after,
                         std::vector<std::size_t> updatedFrames,
                         Counts counts);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<RenumberedRecords> renumberedRecords() const override;

    [[nodiscard]] const Counts& counts() const { return m_counts; }
    // The frames whose content the import replaced, ascending.
    [[nodiscard]] const std::vector<std::size_t>& updatedFrames() const
    {
        return m_updatedFrames;
    }

private:
    document::ProjectDocument m_before;
    document::ProjectDocument m_after;
    std::vector<std::size_t> m_updatedFrames;
    Counts m_counts;
};

// Imports the package as planPackageImport() says, new frames behind the last one, and
// returns the command, already applied; nullptr when nothing changes (every frame known and
// skipped). Comparison masks, backgrounds and sprites the project has are used; the others
// are added (a sprite whose name is taken gets a number), and so are light effects the
// project has no equal of. Components keep their IDs unless the project has one of them
// already. Throws std::invalid_argument with the plan's problem, the document left as it was.
[[nodiscard]] std::unique_ptr<PackageImportCommand> importPackage(
    document::ProjectDocument& document,
    const document::ProjectSnapshot& package,
    KnownFrames knownFrames);

}  // namespace colorizing::edit
