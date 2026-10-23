// The team controller's guard (team/TeamController.h): an edit of what a partner claimed is
// refused, others are not, and looking at a deletion leaves it as it was.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <QWidget>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "edit/FrameMove.h"
#include "edit/PackageImport.h"
#include "edit/StructureCommand.h"
#include "project/ProjectFile.h"
#include "team/TeamChanges.h"
#include "team/TeamController.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: team controller: " << message << '\n';
        std::exit(1);
    }
}

void testRefusals()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    // Revisions for every component, as a save gives them.
    project::ProjectFileOptions options;
    options.stamp = project::RevisionStamp{"kraken", 1000};
    std::vector<document::StampedMeta> stamped;
    project::forEachProjectEntry(document.toSnapshot(), options,
                                 [](const std::string&, const std::string&) {}, &stamped);
    document.adoptComponentMeta(stamped);
    document.markClean();
    document.setTeam(team::startTeam(document, "o/r", "kraken", {"enthusiast"}));

    QWidget window;
    app::TeamController::Host host;
    host.document = [&document]() { return &document; };
    host.projectPath = []() { return QString(); };
    host.save = []() { return false; };
    host.record = [](std::unique_ptr<edit::EditCommand>) {};
    host.status = [](const QString&, int) {};
    host.selectedFrames = []() { return std::vector<std::size_t>{}; };
    app::TeamController controller(&window, host);

    team::Claims claims;
    claims.claims.push_back(
        {document::ComponentKind::Frame, document.frameMeta(2).id, "enthusiast", 1000, false});
    claims.claims.push_back(
        {document::ComponentKind::Frame, document.frameMeta(3).id, "kraken", 1000, false});
    controller.setClaims(claims, QStringLiteral("kraken"));

    const edit::FrameAttributeEditCommand claimed(2, edit::FrameAttribute::DurationMs, 0, 50);
    const edit::FrameAttributeEditCommand mine(3, edit::FrameAttribute::DurationMs, 0, 50);
    const edit::FrameAttributeEditCommand free(4, edit::FrameAttribute::DurationMs, 0, 50);
    const std::optional<QString> refused = controller.refusal(claimed, document);
    expect(refused && refused->contains(QStringLiteral("enthusiast")),
           "a partner's frame is refused, naming the partner");
    expect(!controller.refusal(mine, document) && !controller.refusal(free, document),
           "an own claim and a free frame are not");
    expect(controller.frameClaimText(2).contains(QStringLiteral("enthusiast")) &&
               controller.frameClaimText(3).isEmpty(),
           "the frame's claim for the status line");

    // Deleting the partner's frame: refused; the deletion itself stays applied.
    const std::size_t frames = document.frameCount();
    const auto deletion = edit::deleteFrames(document, {2});
    expect(controller.refusal(*deletion, document).has_value() &&
               document.frameCount() == frames - 1,
           "a deletion of a partner's frame is refused, and stays as it was meanwhile");
    deletion->revert(document);
    const auto otherDeletion = edit::deleteFrames(document, {5});
    expect(!controller.refusal(*otherDeletion, document), "a free frame may be deleted");
    otherDeletion->revert(document);

    // The frame order, claimed by a partner.
    claims.order = team::WholeClaim{"enthusiast", 2000};
    controller.setClaims(claims, QStringLiteral("kraken"));
    const auto move = edit::moveFrames(document, {6}, 0);
    expect(move && controller.refusal(*move, document).has_value(),
           "moving frames while a partner moves frames is refused");
    move->revert(document);

    // A package import is not an edit of components (it adds them).
    const document::ProjectSnapshot package = edit::makePackage(document, {2});
    const auto import = edit::importPackage(document, package, edit::KnownFrames::Add);
    expect(import && !controller.refusal(*import, document), "an import is allowed");

    controller.setClaims({}, QStringLiteral("kraken"));
    expect(!controller.refusal(claimed, document), "without claims everything is allowed");
}

}  // namespace

void runTeamControllerWidgetTests()
{
    testRefusals();
}
