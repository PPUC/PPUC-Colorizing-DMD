// The team controller's guard (team/TeamController.h): an edit of what a partner claimed is
// refused, others are not, and looking at a deletion leaves it as it was. The Team section
// lists the partners' shared changes, and its header shows a bell while some wait.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <QApplication>
#include <QBuffer>
#include <QFormLayout>
#include <QLabel>
#include <QStyle>
#include <QStyleFactory>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include "FrameListWidget.h"
#include "RuntimeFixtures.h"
#include "SectionStack.h"
#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "edit/FrameMove.h"
#include "edit/PackageImport.h"
#include "edit/StructureCommand.h"
#include "project/ProjectFile.h"
#include "team/Avatars.h"
#include "team/GitHub.h"
#include "team/TeamChanges.h"
#include "team/TeamController.h"
#include "team/TeamDialogs.h"
#include "team/TeamPanel.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: team controller: " << message << '\n';
        std::exit(1);
    }
}

document::ProjectDocument teamProject()
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
    return document;
}

void testRefusals()
{
    document::ProjectDocument document = teamProject();

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

QString packageName(int64_t createdMs, const char* login)
{
    return QStringLiteral("%1-%2-%3.cdmdchanges")
        .arg(createdMs)
        .arg(QString::fromLatin1(login),
             QString::fromStdString(document::ComponentId::generate().toString()));
}

void testTeamSection()
{
    document::ProjectDocument document = teamProject();
    QWidget window;
    app::TeamController::Host host;
    host.document = [&document]() { return &document; };
    host.projectPath = []() { return QString(); };
    host.save = []() { return false; };
    host.record = [](std::unique_ptr<edit::EditCommand>) {};
    host.status = [](const QString&, int) {};
    host.selectedFrames = []() { return std::vector<std::size_t>{}; };
    int changes = 0;
    host.teamChanged = [&changes]() { ++changes; };
    app::TeamController controller(&window, host);
    controller.setClaims({}, QStringLiteral("kraken"));

    app::TeamPanel panel;
    panel.setState(controller.panelState());
    expect(panel.accountText().contains(QStringLiteral("o/r")) &&
               panel.accountText().contains(QStringLiteral("kraken")),
           "the section names the repository and the login");
    expect(panel.pendingTexts().isEmpty() && panel.checkedText() == QStringLiteral("Checking…"),
           "nothing waits before the first sync");

    // The background sync finds two shared changes of the partner.
    app::TeamSync::Result result;
    result.claims = team::Claims{};
    result.pendingPackages = {packageName(1761220000000, "enthusiast"),
                              packageName(1761223600000, "enthusiast")};
    controller.deliverSyncResultForTest(result);
    expect(changes == 1, "the window hears that the Team section changed");
    const app::TeamPanel::State state = controller.panelState();
    expect(state.pendingPackages.size() == 2 && state.checked.has_value(),
           "the state lists both and when the sync answered");
    panel.setState(state);
    const QStringList texts = panel.pendingTexts();
    expect(texts.size() == 2 && texts[0].startsWith(QStringLiteral("From enthusiast, ")),
           "each shared change says from whom and when");
    expect(panel.checkedText().startsWith(QStringLiteral("Checked at ")), "when it was checked");
    panel.setAuthorTimes(
        {QStringLiteral("Kraken74 02:05:10"), QStringLiteral("enthusiast 00:10:00")});
    expect(panel.authorTimesText() ==
               QStringLiteral("Time spent: Kraken74 02:05:10, enthusiast 00:10:00"),
           "each author's time");
    app::TeamPanel::State withNotes = state;
    withNotes.notes[state.pendingPackages[1]].comment = QStringLiteral("Attract mode done");
    QImage avatar(64, 64, QImage::Format_ARGB32);
    avatar.fill(Qt::red);
    withNotes.avatars[QStringLiteral("enthusiast")] = avatar;
    panel.setState(withNotes);
    expect(panel.pendingTexts()[1].endsWith(QStringLiteral("\nAttract mode done")) &&
               !panel.pendingTexts()[0].contains(QLatin1Char('\n')),
           "a shared change's comment below its line");
    expect(panel.hasAvatar(0) && panel.hasAvatar(1), "the author's avatar beside it");
    // A shared change that comes as a file, not added yet.
    app::TeamPanel::State withFile = state;
    withFile.pendingPackages[0] = app::packageFileName(state.pendingPackages[0]);
    withFile.pendingPackages[0].replace(QStringLiteral(".cdmdchanges"),
                                        QStringLiteral(".cdmdnotice"));
    app::PendingNote fileNote;
    fileNote.asFile = true;
    fileNote.available = false;
    fileNote.sizeBytes = 120LL * 1024 * 1024;
    withFile.notes[withFile.pendingPackages[0]] = fileNote;
    panel.setState(withFile);
    expect(panel.pendingTexts()[0].contains(
               QStringLiteral("120 MB, comes as a file: add it with Add File")),
           "a file still to add says so, with its size: " +
               panel.pendingTexts()[0].toStdString());
    withFile.notes[withFile.pendingPackages[0]].available = true;
    panel.setState(withFile);
    expect(panel.pendingTexts()[0].contains(QStringLiteral("120 MB, added as a file")),
           "an added one too");
    panel.setState(state);
    expect(!panel.hasAvatar(0), "no avatar before it is loaded");
    QPushButton* merge = nullptr;
    for (QPushButton* button : panel.findChildren<QPushButton*>()) {
        if (button->text().startsWith(QStringLiteral("Merge Changes"))) {
            merge = button;
        }
    }
    expect(merge && merge->text() == QStringLiteral("Merge Changes (2)...") &&
               merge->isEnabled(),
           "the merge button counts them");
    bool mergeAsked = false;
    panel.onMergeRequested = [&mergeAsked]() { mergeAsked = true; };
    merge->click();
    expect(mergeAsked, "the merge button asks for the merge");

    // Signed out, the section asks to sign in.
    app::TeamPanel::State signedOut;
    signedOut.repository = QStringLiteral("o/r");
    panel.setState(signedOut);
    expect(!merge->isEnabled() && panel.accountText().contains(QStringLiteral("sign in")),
           "signed out: no merge, a hint to sign in");

    // The bell on the section's header.
    app::SectionStack stack;
    auto* content = new QWidget;
    const int index = stack.addTab(content, QStringLiteral("Team"), QStringLiteral("Team"));
    stack.show();
    QApplication::processEvents();
    const auto badgeShown = [&stack]() {
        for (const QLabel* label : stack.findChildren<QLabel*>()) {
            if (label->isVisible() && !label->pixmap().isNull()) {
                return true;
            }
        }
        return false;
    };
    expect(stack.sectionBadge(index) == 0 && !badgeShown(), "no bell while nothing waits");
    stack.setSectionBadge(index, 2, QStringLiteral("2 shared changes to merge"));
    expect(stack.sectionBadge(index) == 2 && badgeShown(), "the bell while two wait");
    stack.setSectionBadge(index, 0);
    expect(stack.sectionBadge(index) == 0 && !badgeShown(), "merged: the bell goes");
}

// Answers every request at once with a red square, and keeps the URLs asked for.
class PictureTransport final : public app::HttpTransport {
public:
    void send(const app::HttpRequest& request, std::function<void(app::HttpReply)> done) override
    {
        urls.append(request.url.toString());
        QImage square(80, 80, QImage::Format_RGB32);
        square.fill(Qt::red);
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        square.save(&buffer, "PNG");
        done({200, buffer.data(), QString()});
    }
    QStringList urls;
};

void testAvatars()
{
    expect(app::AvatarCache::avatarUrl(QStringLiteral("Kraken74")) ==
               QUrl(QStringLiteral("https://github.com/Kraken74.png?size=64")),
           "a login's avatar URL");
    expect(app::AvatarCache::avatarUrl(QStringLiteral("../x?y")).isEmpty() &&
               app::AvatarCache::avatarUrl(QStringLiteral("-a")).isEmpty(),
           "no URL for a text that is no login");
    PictureTransport transport;
    int loaded = 0;
    app::AvatarCache cache(transport, [&loaded]() { ++loaded; });
    // The answer comes at once here; in the editor it comes later, and avatar() is asked
    // again when it did.
    (void)cache.avatar(QStringLiteral("enthusiast"));
    const QImage avatar = cache.avatar(QStringLiteral("enthusiast"));
    expect(loaded == 1 && !avatar.isNull() && avatar.width() == app::AvatarCache::kSize,
           "the avatar is loaded once and kept");
    expect(qAlpha(avatar.pixel(0, 0)) == 0 &&
               avatar.pixelColor(avatar.width() / 2, avatar.height() / 2) == QColor(Qt::red),
           "round: the corners are transparent");
    (void)cache.avatar(QStringLiteral("../x"));
    expect(transport.urls.size() == 1, "one request, none for a text that is no login");
}

// The frames a partner works on, for the badges of the frame list: not one whose version this
// copy has merged, not this copy's own.
void testClaimedFrames()
{
    document::ProjectDocument document = teamProject();
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
        {document::ComponentKind::Frame, document.frameMeta(2).id, "enthusiast", 1000, false,
         document::ComponentId::generate()});
    // A version this copy has as the common one: merged.
    claims.claims.push_back({document::ComponentKind::Frame, document.frameMeta(4).id,
                             "enthusiast", 1000, false,
                             document.team()->common[0].at(document.frameMeta(4).id)});
    claims.claims.push_back(
        {document::ComponentKind::Frame, document.frameMeta(5).id, "kraken", 1000, false});
    claims.claims.push_back(
        {document::ComponentKind::Frame, document.frameMeta(6).id, "other", 1000, true});
    controller.setClaims(claims, QStringLiteral("kraken"));
    const auto frames = controller.claimedFrames();
    expect(frames.size() == 2 && frames[0].first == 2 &&
               frames[0].second == QStringLiteral("enthusiast") && frames[1].first == 6 &&
               frames[1].second == QStringLiteral("other"),
           "a partner's frames and reservations; not a merged version, not mine");

    // The badge: the avatar in a ring, or a lock.
    QImage avatar(64, 64, QImage::Format_ARGB32);
    avatar.fill(Qt::red);
    const QImage withAvatar = app::FrameListWidget::claimBadge(avatar, 20);
    const QImage lock = app::FrameListWidget::claimBadge(QImage(), 20);
    expect(withAvatar.size() == QSize(20, 20) && qAlpha(withAvatar.pixel(0, 0)) == 0 &&
               withAvatar.pixelColor(10, 10) == QColor(Qt::red),
           "the avatar, round");
    expect(qGray(lock.pixel(10, 15)) < 100, "a dark lock without an avatar");
    app::FrameListWidget list;
    list.setClaimBadges({{0, avatar}, {3, QImage()}});
    expect(list.claimBadges().size() == 2, "the list keeps the badges of its rows");
}

void testStartTeamDialog()
{
    app::StartTeamDialog dialog(QStringLiteral("afm_113b"), QStringLiteral("kraken"));
    // The macOS style, where there is one, keeps a form's fields at their own width unless the
    // form says otherwise.
    std::unique_ptr<QStyle> macStyle(QStyleFactory::create(QStringLiteral("macOS")));
    if (macStyle) {
        dialog.setStyle(macStyle.get());
        for (QWidget* child : dialog.findChildren<QWidget*>()) {
            child->setStyle(macStyle.get());
        }
    }
    dialog.show();
    QApplication::processEvents();
    const QList<QLineEdit*> fields = dialog.findChildren<QLineEdit*>();
    expect(fields.size() == 3, "three fields");
    const QFormLayout* form = dialog.findChild<QFormLayout*>();
    expect(form && form->fieldGrowthPolicy() == QFormLayout::AllNonFixedFieldsGrow,
           "the fields take the dialog's width on every platform");
    for (const QLineEdit* field : fields) {
        expect(field->width() > dialog.width() / 2, "a field is not a short one");
    }
    expect(fields[1]->text() == QStringLiteral("afm_113b-colorization"),
           "the repository is named after the ROM");
    fields[0]->setText(QStringLiteral("Kraken"));
    expect(dialog.owner().isEmpty(), "the own login is the own account");
    fields[0]->setText(QStringLiteral("serum-team"));
    expect(dialog.owner() == QStringLiteral("serum-team"), "an organization");
    fields[2]->setText(QStringLiteral("@enthusiast, kraken; other  enthusiast"));
    expect(dialog.partners() == QStringList({QStringLiteral("enthusiast"), QStringLiteral("other")}),
           "partners: without @, without the own login, each once");
}

}  // namespace

void runTeamControllerWidgetTests()
{
    testRefusals();
    testTeamSection();
    testAvatars();
    testClaimedFrames();
    testStartTeamDialog();
}
