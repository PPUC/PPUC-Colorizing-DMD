// Team projects (team/TeamChanges.h, team/TeamMerge.h): two copies of one project, shared and
// merged both ways, with conflicts, deletions, added frames and the common state moving on.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "document/ProjectTime.h"
#include "edit/FrameMove.h"
#include "platform/Utf8Path.h"
#include "project/ProjectFile.h"
#include "team/Claims.h"
#include "team/TeamChanges.h"
#include "team/TeamMerge.h"

namespace {

using namespace colorizing;
using document::ComponentKind;
using document::ProjectDocument;
using team::MergeAction;
using team::MergeChoice;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: team: " << message << '\n';
        std::exit(1);
    }
}

std::string tempPath(const std::string& name)
{
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return platform::utf8FromPath(std::filesystem::temp_directory_path() /
                                  ("colorizing_qt_v2_team_" + run + "_" + name));
}

// Saves as the app does: revisions for what changed, taken over by the document.
void save(ProjectDocument& document, const std::string& path, const std::string& author,
          int64_t time)
{
    project::ProjectFileOptions options{"test"};
    options.stamp = project::RevisionStamp{author, time};
    std::vector<document::StampedMeta> stamped;
    const legacy::LegacySaveResult result =
        project::saveProjectFile(path, document.toSnapshot(), options, nullptr, &stamped);
    expect(result.ok, "saved: " + result.error.message);
    document.adoptComponentMeta(stamped);
    document.markClean();
}

ProjectDocument load(const std::string& path)
{
    legacy::LegacyDocumentLoadResult loaded = project::loadProjectDocument(path);
    expect(loaded.ok, "loaded: " + loaded.error.message);
    return std::move(*loaded.document);
}

team::ChangePackage share(ProjectDocument& document, const std::string& path,
                          const std::string& author, int64_t time, const std::string& name)
{
    save(document, path, author, time);
    const team::ChangePackage made = team::makeChangePackage(document, author, time);
    const std::string packagePath = tempPath(name + ".cdmdchanges");
    expect(team::writeChangePackage(packagePath, made, "test").ok, "the package is written");
    document::TeamState state = *document.team();
    state.sent.push_back(team::sentPackageOf(made));
    document.setTeam(state);
    save(document, path, author, time);
    team::ChangePackageLoad read = team::readChangePackage(packagePath);
    expect(read.package.has_value(), "the package is read: " + read.error);
    std::filesystem::remove(platform::pathFromUtf8(packagePath));
    return std::move(*read.package);
}

const team::MergeItem* itemOf(const team::MergePlan& plan, ComponentKind kind,
                              const document::ComponentId& id)
{
    for (const team::MergeItem& item : plan.items) {
        if (item.kind == kind && item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

struct Team {
    std::string ownerPath = tempPath("kraken.cdmd");
    std::string partnerPath = tempPath("enthusiast.cdmd");
    ProjectDocument owner;
    ProjectDocument partner;
};

Team startTeam()
{
    Team team{tempPath("kraken.cdmd"), tempPath("enthusiast.cdmd"),
              ProjectDocument::fromSnapshot(
                  runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary)),
              {}};
    team.owner.setFrameDurationMs(0, 120);
    bool refused = false;
    try {
        (void)team::startTeam(team.owner, "serum-team/afm", "kraken", {"enthusiast"});
    } catch (const std::logic_error&) {
        refused = true;
    }
    expect(refused, "a team does not start from unsaved changes");
    save(team.owner, team.ownerPath, "kraken", 1000);
    team.owner.setTeam(team::startTeam(team.owner, "serum-team/afm", "kraken", {"enthusiast"}));
    save(team.owner, team.ownerPath, "kraken", 1000);
    // The partner starts from the snapshot: a copy of the file.
    std::filesystem::copy_file(platform::pathFromUtf8(team.ownerPath),
                               platform::pathFromUtf8(team.partnerPath),
                               std::filesystem::copy_options::overwrite_existing);
    team.partner = load(team.partnerPath);
    return team;
}

void testStart()
{
    Team team = startTeam();
    expect(team.partner.team() && *team.partner.team() == *team.owner.team() &&
               team.owner.team()->members ==
                   std::vector<std::string>{"kraken", "enthusiast"},
           "the team state goes through the file, the owner a member");
    expect(team::changesSinceCommon(team.owner).empty() &&
               team::changesSinceCommon(team.partner).empty(),
           "nothing changed since the snapshot");
}

void testTakeAndAcknowledge()
{
    Team team = startTeam();
    team.owner.setFrameDurationMs(2, 777);
    const uint16_t oldColor = team.owner.frameSnapshotOf(2).colorizedSd.pixels[0];
    team.owner.setColorizedPixel(2, render::Resolution::Sd, 0, 0, 0xf81f);
    team.owner.setComparisonMaskPixel(1, 3, 3, 1);
    save(team.owner, team.ownerPath, "kraken", 2000);
    const team::ChangeSet changes = team::changesSinceCommon(team.owner);
    expect(changes.changed[0].size() == 1 && changes.changed[1].size() == 1 &&
               !changes.orderChanged && !changes.settingsChanged,
           "a changed frame and mask");

    // The common version of the changed frame stays in the file, for a conflict.
    const document::ComponentId frame2 = team.owner.frameMeta(2).id;
    const document::ComponentId common2 = team.owner.team()->common[0].at(frame2);
    const auto commonPictures = project::readCommonFramePictures(team.ownerPath, frame2, common2);
    const auto ownPictures =
        project::readCommonFramePictures(team.ownerPath, frame2, team.owner.frameMeta(2).revision);
    expect(commonPictures && commonPictures->colorizedSd.pixels[0] == oldColor && ownPictures &&
               ownPictures->colorizedSd.pixels[0] == 0xf81f,
           "the file keeps the frame's common version beside its own");
    expect(!project::readCommonFramePictures(team.ownerPath, frame2,
                                             document::ComponentId::generate()),
           "no other version");

    const team::ChangePackage package = share(team.owner, team.ownerPath, "kraken", 2000, "a1");
    expect(package.carries(ComponentKind::Frame, frame2) &&
               package.carries(ComponentKind::ComparisonMask,
                               team.owner.comparisonMaskMeta(1).id) &&
               !package.carries(ComponentKind::Frame, team.owner.frameMeta(3).id),
           "the package carries what changed");

    const team::MergePlan plan = team::planMerge(team.partner, package);
    expect(plan.problem.empty() && plan.conflictCount() == 0 &&
               itemOf(plan, ComponentKind::Frame, frame2)->action == MergeAction::Take,
           "the partner takes the frame: " + plan.problem);
    const auto merge = team::mergeChangePackage(team.partner, package, {}, "enthusiast");
    expect(team.partner.frameDurationMs(2) == 777 &&
               team.partner.comparisonMaskPixel(1, 3, 3) == 1 &&
               team.partner.frameMeta(2) == team.owner.frameMeta(2),
           "with its content and its revision, author and time");
    save(team.partner, team.partnerPath, "enthusiast", 3000);
    expect(team.partner.frameMeta(2).author == "kraken" &&
               team::changesSinceCommon(team.partner).empty(),
           "a save there gives it no revision of its own: nothing to share");
    expect(!team::planMerge(team.partner, package).problem.empty(),
           "a package is merged once");

    // The partner's next package says it merged the owner's; the owner's change is common
    // then, as every member has it.
    team.partner.setFrameDurationMs(4, 40);
    const team::ChangePackage reply =
        share(team.partner, team.partnerPath, "enthusiast", 4000, "b1");
    expect(reply.merged.size() == 1 && reply.merged[0] == package.packageId,
           "the reply names the merged package");
    expect(!team::changesSinceCommon(team.owner).changed[0].empty(), "not common yet");
    (void)team::mergeChangePackage(team.owner, reply, {}, "kraken");
    expect(team.owner.frameDurationMs(4) == 40 && team.owner.team()->sent.empty() &&
               team::changesSinceCommon(team.owner).changed[0].count(frame2) == 0,
           "the owner's change is common, its package done");

    merge->revert(team.partner);
}

void testConflicts()
{
    for (const MergeChoice choice : {MergeChoice::Mine, MergeChoice::Theirs}) {
        Team team = startTeam();
        team.owner.setFrameDurationMs(3, 300);
        team.partner.setFrameDurationMs(3, 333);
        save(team.partner, team.partnerPath, "enthusiast", 2500);
        const team::ChangePackage package =
            share(team.owner, team.ownerPath, "kraken", 2000, "c1");
        const document::ComponentId frame3 = team.owner.frameMeta(3).id;
        const team::MergePlan plan = team::planMerge(team.partner, package);
        expect(plan.conflictCount() == 1 &&
                   itemOf(plan, ComponentKind::Frame, frame3)->action == MergeAction::Conflict,
               "both changed the frame: a conflict");
        team::MergeChoices choices;
        choices.components[frame3] = choice;
        const auto merge = team::mergeChangePackage(team.partner, package, choices, "enthusiast");
        expect(merge->counts().conflicts == 1 &&
                   team.partner.frameDurationMs(3) ==
                       (choice == MergeChoice::Theirs ? 300u : 333u),
               "the conflict keeps the side chosen");
        save(team.partner, team.partnerPath, "enthusiast", 5000);
        const bool shared = team::changesSinceCommon(team.partner).changed[0].count(frame3) > 0;
        expect(shared == (choice == MergeChoice::Mine),
               "kept, mine is shared next; taken, nothing is");
        if (choice == MergeChoice::Mine) {
            // The owner takes it without a conflict: it was based on the owner's version.
            const team::ChangePackage reply =
                share(team.partner, team.partnerPath, "enthusiast", 6000, "c2");
            const team::MergePlan back = team::planMerge(team.owner, reply);
            expect(itemOf(back, ComponentKind::Frame, frame3)->action == MergeAction::Take,
                   "the kept version goes back without a conflict");
        }
    }
}

void testAddDeleteSettingsAndUndo()
{
    Team team = startTeam();
    const std::size_t frames = team.owner.frameCount();
    const document::ComponentId deleted = team.owner.frameMeta(5).id;
    std::vector<std::size_t> remove = {5};
    (void)team.owner.removeFrames(remove);
    document::RemovedFrames added;
    added.indices = {2};
    added.frames.push_back(team.owner.makeUncolorizedFrame(
        document::makeIndexedFrame(team.owner.originalFrameSize(), 1), 50));
    added.tables = team.owner.recordTables();
    team.owner.insertFrames(std::move(added));
    const document::ComponentId newFrame = team.owner.frameMeta(2).id;
    document::ProjectLicense license;
    license.kind = document::LicenseKind::Cc0;
    team.owner.setLicense(license);
    const team::ChangePackage package = share(team.owner, team.ownerPath, "kraken", 2000, "d1");
    expect(package.orderChanged && package.settingsChanged &&
               package.deleted[0].count(deleted) == 1,
           "the order, the settings and the deletion are in the package");

    const ProjectDocument before = team.partner;
    const team::MergePlan plan = team::planMerge(team.partner, package);
    expect(plan.takeOrder && plan.takeSettings &&
               itemOf(plan, ComponentKind::Frame, deleted)->action == MergeAction::Delete &&
               itemOf(plan, ComponentKind::Frame, newFrame)->action == MergeAction::Add,
           "the partner takes the order and the settings, deletes and adds");
    const auto merge = team::mergeChangePackage(team.partner, package, {}, "enthusiast");
    expect(team.partner.frameCount() == frames && team.partner.frameMeta(2).id == newFrame &&
               !team.partner.frameIndexOf(deleted) &&
               team.partner.license().kind == document::LicenseKind::Cc0,
           "the frame is added in its place, the other deleted, the license taken");
    save(team.partner, team.partnerPath, "enthusiast", 3000);
    const team::ChangeSet after = team::changesSinceCommon(team.partner);
    expect(after.empty(), "the partner has nothing of its own to share afterwards");

    merge->revert(team.partner);
    expect(team.partner.frameCount() == before.frameCount() &&
               team.partner.frameIndexOf(deleted) && *team.partner.team() == *before.team(),
           "undo brings the frames and the team state back");
}

void testRefusals()
{
    Team team = startTeam();
    team.owner.setFrameDurationMs(1, 11);
    team::ChangePackage package = share(team.owner, team.ownerPath, "kraken", 2000, "e1");
    package.snapshotId = document::ComponentId::generate();
    expect(team::planMerge(team.partner, package).problem.find("snapshot") != std::string::npos,
           "a package of another snapshot is refused");
    ProjectDocument solo = ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    expect(!team::planMerge(solo, package).problem.empty(), "a solo project merges nothing");
    expect(!team::readChangePackage(team.ownerPath).package,
           "a project file is no change package");
}

void testClaims()
{
    Team team = startTeam();
    const document::ComponentId frame1 = team.owner.frameMeta(1).id;
    const document::ComponentId frame6 = team.owner.frameMeta(6).id;
    team::Claims claims;
    claims.claims.push_back({ComponentKind::Frame, frame6, "enthusiast", 500, false});
    team.owner.setFrameDurationMs(1, 10);
    save(team.owner, team.ownerPath, "kraken", 2000);
    const team::Claims mine = team::withMyClaims(
        claims, "kraken", team::changesSinceCommon(team.owner),
        {{ComponentKind::Background, team.owner.backgroundMeta(0).id}}, 2000);
    expect(mine.claims.size() == 3 && !mine.order && !mine.settings,
           "the partner's claim, the changed frame and the reservation");
    expect(team::partnerClaim(mine, ComponentKind::Frame, frame6, "kraken") &&
               team::partnerClaim(mine, ComponentKind::Frame, frame6, "kraken")->login ==
                   "enthusiast" &&
               !team::partnerClaim(mine, ComponentKind::Frame, frame1, "kraken") &&
               team::partnerClaim(mine, ComponentKind::Frame, frame1, "enthusiast"),
           "a claim is another member's or this one's");
    const team::Claims later =
        team::withMyClaims(mine, "kraken", team::changesSinceCommon(team.owner), {}, 9000);
    const auto claimOf = [&](const team::Claims& list, const document::ComponentId& id) {
        for (const team::Claim& claim : list.claims) {
            if (claim.id == id) {
                return claim;
            }
        }
        return team::Claim{};
    };
    expect(later.claims.size() == 2 && claimOf(later, frame1).sinceMs == 2000,
           "a claim keeps its time; a reservation given up is gone");
    expect(claimOf(later, frame1).revision == team.owner.frameMeta(1).revision,
           "a claim names the claimer's revision");

    team::Claims full = later;
    full.order = team::WholeClaim{"enthusiast", 700, 0x0123456789abcdefULL};
    full.lockBreak = team::LockBreak{"kraken", 800, document::ComponentId::generate()};
    std::string error;
    const std::optional<team::Claims> read = team::parseClaims(team::writeClaims(full), error);
    expect(read && *read == full, "the claims file reads back: " + error);
    expect(team::parseClaims("", error) == team::Claims{} &&
               !team::parseClaims("{\"claims\": [{\"kind\": \"frame\"}]}", error),
           "an empty file has no claims, a damaged one is refused");
}

}  // namespace

// Merged, a partner's change no longer keeps this copy off: the claim names the version this
// copy has as the common one now. A new version, or an edit not saved yet, does again.
void testMergedClaimsRelease()
{
    Team team = startTeam();
    const document::ComponentId frame2 = team.owner.frameMeta(2).id;
    team.owner.setFrameDurationMs(2, 222);
    (void)edit::moveFrames(team.owner, {6}, 0);
    save(team.owner, team.ownerPath, "kraken", 2000);
    const auto ownerClaims = [](const team::ChangeSet& changes) {
        return team::withMyClaims({}, "kraken", changes, {}, 2000);
    };
    const auto blocked = [&team, frame2](const team::Claims& claims) {
        const document::TeamState& common = *team.partner.team();
        const auto found = common.common[0].find(frame2);
        return team::blockingClaim(claims, ComponentKind::Frame, frame2, "enthusiast",
                                   found != common.common[0].end() ? found->second
                                                                   : document::ComponentId{}) !=
               nullptr;
    };
    const auto orderBlocked = [&team](const team::Claims& claims) {
        return team::wholeClaimBlocks(claims.order, "enthusiast",
                                      team::orderHash(team.partner.team()->commonOrder));
    };
    team::Claims claims = ownerClaims(team::changesSinceCommon(team.owner));
    expect(blocked(claims) && orderBlocked(claims),
           "before the merge the owner's frame and frame order are claimed");

    const team::ChangePackage package = share(team.owner, team.ownerPath, "kraken", 2000, "c1");
    (void)team::mergeChangePackage(team.partner, package, {}, "enthusiast");
    save(team.partner, team.partnerPath, "enthusiast", 3000);
    expect(team.partner.frameDurationMs(*team.partner.frameIndexOf(frame2)) == 222 &&
               team::partnerClaim(claims, ComponentKind::Frame, frame2, "enthusiast"),
           "the owner still claims the frame (the partner has not acknowledged yet)");
    expect(!blocked(claims) && !orderBlocked(claims),
           "but merged, it keeps the partner off neither the frame nor the frame order");

    // The owner goes on with the frame: unsaved first, then saved.
    team::ChangeSet unsaved = team::changesSinceCommon(team.owner);
    unsaved.changed[0].insert_or_assign(frame2, document::ComponentId{});
    unsaved.orderHash = 0;
    claims = ownerClaims(unsaved);
    expect(blocked(claims) && orderBlocked(claims), "an edit not saved yet keeps the partner off");
    team.owner.setFrameDurationMs(team.owner.frameIndexOf(frame2).value(), 333);
    save(team.owner, team.ownerPath, "kraken", 4000);
    claims = ownerClaims(team::changesSinceCommon(team.owner));
    expect(blocked(claims), "a new version does");
}

// Each author's time: the session counts for the author, a partner's larger time is taken and
// the total grows by it; the times go through the project file and a shared change.
void testAuthorTimes()
{
    using Times = std::map<std::string, uint64_t>;
    document::ProjectTime time;
    time.open(10000, {{"kraken", 3000}});
    expect(time.totalMs(500) == 10500 &&
               time.authorsMs("enthusiast", 500) == Times{{"kraken", 3000}, {"enthusiast", 500}},
           "the session counts for its author and in the total");
    expect(time.authorsMs("", 500) == Times{{"kraken", 3000}}, "without a name for nobody");
    time.merge({{"kraken", 5000}, {"enthusiast", 400}}, "enthusiast", 500);
    expect(time.totalMs(500) == 12500 &&
               time.authorsMs("enthusiast", 500) == Times{{"kraken", 5000}, {"enthusiast", 500}},
           "the partner's larger time is taken, an older one of mine is not");
    time.merge({{"enthusiast", 500}}, "enthusiast", 800);
    expect(time.totalMs(800) == 12800, "my own time coming back adds nothing");
    time.merge({{"enthusiast", 2000}}, "enthusiast", 800);
    expect(time.totalMs(800) == 14000 &&
               time.authorsMs("enthusiast", 800).at("enthusiast") == 2000,
           "my time from another copy of mine is taken too");
    time.open(100, {{"kraken", 3000}});
    expect(time.totalMs(0) == 3000, "a total below the authors' sum counts their sum");

    // Through the project file.
    Team team = startTeam();
    document::ProjectSnapshot snapshot = team.owner.toSnapshot();
    snapshot.editorMetadata.authorTimeMs = {{"kraken", 7200000}, {"Ünal", 60000}};
    const std::string path = tempPath("times.cdmd");
    const legacy::LegacySaveResult saved =
        project::saveProjectFile(path, snapshot, project::ProjectFileOptions{"test"});
    expect(saved.ok, "saved: " + saved.error.message);
    const auto loaded = project::loadProjectDocument(path);
    expect(loaded.document &&
               loaded.document->editorMetadata().authorTimeMs ==
                   snapshot.editorMetadata.authorTimeMs,
           "the authors' times go through the project file");
    std::filesystem::remove(platform::pathFromUtf8(path));

    // Through a shared change.
    team.owner.setFrameDurationMs(2, 222);
    save(team.owner, team.ownerPath, "kraken", 2000);
    team::ChangePackage made = team::makeChangePackage(team.owner, "kraken", 2000);
    made.authorTimes = {{"kraken", 7200000}};
    const std::string packagePath = tempPath("times.cdmdchanges");
    expect(team::writeChangePackage(packagePath, made, "test").ok, "the package is written");
    const team::ChangePackageLoad read = team::readChangePackage(packagePath);
    expect(read.package && read.package->authorTimes == made.authorTimes,
           "and through a shared change");
    std::filesystem::remove(platform::pathFromUtf8(packagePath));
}

void runTeamTests()
{
    testStart();
    testTakeAndAcknowledge();
    testConflicts();
    testAddDeleteSettingsAndUndo();
    testRefusals();
    testClaims();
    testMergedClaimsRelease();
    testAuthorTimes();
}
