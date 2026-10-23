// The team controller's guard (team/TeamController.h): an edit of what a partner claimed is
// refused, others are not, and looking at a deletion leaves it as it was. The Team section
// lists the partners' shared changes, and its header shows a bell while some wait.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <QApplication>
#include <QFormLayout>
#include <QLabel>
#include <QStyle>
#include <QStyleFactory>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include "RuntimeFixtures.h"
#include "SectionStack.h"
#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "edit/FrameMove.h"
#include "edit/PackageImport.h"
#include "edit/StructureCommand.h"
#include "project/ProjectFile.h"
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
    panel.setState({QString(), QStringLiteral("o/r"), {}, std::nullopt, QString()});
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
    testStartTeamDialog();
}
