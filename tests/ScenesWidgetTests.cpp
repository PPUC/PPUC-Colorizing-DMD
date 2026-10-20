// Scenes: the inspector's Scenes and Scaling section and the scene dialog.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QApplication>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>

#include "RuntimeFixtures.h"
#include "ThumbnailCaptions.h"
#include "dialogs/SceneDialog.h"
#include "document/ProjectDocument.h"
#include "edit/SceneEdits.h"
#include "edit/StructureCommand.h"
#include "dialogs/ProjectSettingsDialog.h"
#include "panels/ProjectSettingsPanel.h"
#include "panels/SceneSettingsForm.h"
#include "panels/SceneSettingsPanel.h"
#include "panels/ScenesPanel.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: scenes widgets: " << message << '\n';
        std::exit(1);
    }
}

QPushButton* button(QWidget& parent, const QString& text)
{
    for (QPushButton* candidate : parent.findChildren<QPushButton*>()) {
        if (candidate->text() == text) {
            return candidate;
        }
    }
    return nullptr;
}

void testPanel()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    document::SceneDefinition scene;
    scene.sceneId = 51001;
    scene.frameCount = 3;
    scene.frameGroups = 2;
    scene.durationMs = 40;
    scene.options = document::kSceneAsBackground;
    auto added = edit::addScene(document, scene);
    // One of its frames deleted from the strip.
    auto deleted = edit::deleteFrames(document, std::vector<std::size_t>{3});

    app::ScenesPanel panel;
    std::vector<int> edits;
    int selectionChanges = 0;
    panel.onEditRequested = [&](uint16_t id) { edits.push_back(id); };
    panel.onSelectionChanged = [&]() { ++selectionChanges; };
    panel.setProject(&document);
    expect(selectionChanges == 0, "filling the list reports no selection");
    auto* list = panel.findChild<QListWidget*>();
    expect(list && list->count() == 1 && list->item(0)->text().contains(QStringLiteral("51001")) &&
               list->item(0)->text().contains(QStringLiteral("2 × 3")) &&
               list->item(0)->text().contains(QStringLiteral("background")) &&
               list->item(0)->text().contains(QStringLiteral("1 missing")),
           "the section lists the scene with its groups, kind and missing frames");
    QPushButton* add = button(panel, QStringLiteral("Add"));
    QPushButton* edit = button(panel, QStringLiteral("Edit"));
    expect(add && add->isEnabled() && edit && !edit->isEnabled(),
           "Add works in a 128x32 project, Edit needs a selected scene");
    list->setCurrentRow(0);
    expect(edit->isEnabled() && panel.selectedScene() == 51001, "a selected scene can be edited");
    edit->click();
    expect(edits == std::vector<int>{51001}, "Edit asks for it");
    expect(selectionChanges == 1, "selecting a scene reports it (the frame filter follows)");
    // "Used by the current frame": only the frame's scene.
    panel.showOnlyScene(true, uint16_t{51001});
    expect(!list->item(0)->isHidden(), "the current frame's scene stays listed");
    panel.showOnlyScene(true, std::nullopt);
    expect(list->item(0)->isHidden(), "a frame of no scene lists none");
    panel.showOnlyScene(false, std::nullopt);
    expect(!list->item(0)->isHidden(), "unfiltered, every scene is listed");
    // The scene's thumbnail: its first frame.
    panel.buildAllThumbnails();
    expect(!list->item(0)->icon().isNull(),
           "a scene has the thumbnail of its first frame");

    document::ProjectDocument native = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::NativeOriginal, 2));
    panel.setProject(&native);
    expect(list->count() == 0 && !add->isEnabled(), "a 256x64 project offers no scenes");
    (void)added;
    (void)deleted;
}

// The left sidebar's rows: the caption below the thumbnail, wrapped to the list's width.
void testThumbnailCaptions()
{
    QListWidget list;
    list.setIconSize(QSize(160, 42));
    app::showCaptionsBelowThumbnails(&list);
    QPixmap pixmap(160, 40);
    pixmap.fill(Qt::red);
    auto* item = new QListWidgetItem(QIcon(pixmap),
                                     QStringLiteral("50002 — 199 frames, 50 ms, background, "
                                                    "with a long caption"),
                                     &list);
    auto* plain = new QListWidgetItem(QStringLiteral("no icon"), &list);
    list.resize(400, 300);
    list.show();
    QApplication::processEvents();
    const int wide = list.visualItemRect(item).height();
    const int withoutIcon = list.visualItemRect(plain).height();
    expect(wide > 42 && withoutIcon < 42,
           "a row is the thumbnail and its caption below it; without an icon the caption");
    list.resize(140, 300);
    QApplication::processEvents();
    expect(list.visualItemRect(item).height() > wide,
           "a narrow list wraps the caption onto more lines instead of cutting it");
}

void testProjectSettings()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    document::ColorizationSettings settings;
    settings.shadowOffset = document::kShadowOffsetProportional;
    document.setColorizationSettings(settings);
    app::ProjectSettingsPanel panel;
    std::vector<std::pair<int, int>> scaling;
    panel.onScalingChanged = [&](uint8_t algorithm, uint8_t offset) {
        scaling.emplace_back(algorithm, offset);
    };
    panel.setProject(&document);
    const QList<QComboBox*> boxes = panel.findChildren<QComboBox*>();
    expect(boxes.size() == 2 && boxes[0]->currentData().toInt() == document::kScalingScale2x &&
               boxes[1]->currentData().toInt() == document::kShadowOffsetProportional &&
               boxes[0]->isEnabled(),
           "the project's scaling settings are shown");
    expect(scaling.empty(), "filling the section reports nothing");
    boxes[0]->setCurrentIndex(boxes[0]->findData(document::kScalingLineDoubling));
    expect(scaling.size() == 1 && scaling[0].first == document::kScalingLineDoubling &&
               scaling[0].second == document::kShadowOffsetProportional,
           "choosing another reports both scaling settings");
    panel.setProject(nullptr);
    expect(!boxes[0]->isEnabled() && !boxes[1]->isEnabled(), "without a project they are off");

    app::ProjectSettingsDialog dialog(document);
    const QList<QComboBox*> dialogBoxes = dialog.findChildren<QComboBox*>();
    expect(dialog.isModal() && dialogBoxes.size() == 2 &&
               dialog.shadowOffset() == document::kShadowOffsetProportional,
           "the dialog shows the project's settings");
    dialogBoxes[0]->setCurrentIndex(dialogBoxes[0]->findData(document::kScalingLineDoubling));
    expect(dialog.scalingAlgorithm() == document::kScalingLineDoubling &&
               document.colorizationSettings()->scalingAlgorithm == document::kScalingScale2x,
           "a choice in the dialog changes nothing until the owner applies it");
}

// The Scene section: the scene of the frame shown, changed with Apply.
void testSettingsPanel()
{
    document::SceneDefinition scene;
    scene.sceneId = 50003;
    scene.frameCount = 68;
    scene.frameGroups = 1;
    scene.durationMs = 50;
    scene.options = document::kSceneAsBackground;
    app::SceneSettingsPanel panel;
    std::vector<document::SceneDefinition> applied;
    std::vector<uint16_t> appliedIds;
    panel.onApplyRequested = [&](uint16_t id, const document::SceneDefinition& changed) {
        appliedIds.push_back(id);
        applied.push_back(changed);
    };
    QPushButton* apply = button(panel, QStringLiteral("Apply"));
    QPushButton* revert = button(panel, QStringLiteral("Revert"));
    expect(apply && revert && !panel.form()->isEnabled() && !panel.sceneId(),
           "without a scene the section offers nothing");
    panel.setScene(scene, [](const document::SceneDefinition& candidate) {
        return candidate.sceneId == 50004 ? std::string("taken") : std::string();
    });
    expect(panel.sceneId() == 50003 && panel.form()->scene() == scene && !apply->isEnabled() &&
               !revert->isEnabled() && !panel.hasChanges(),
           "the section shows the scene's settings, nothing to apply yet");
    QSpinBox* duration = nullptr;
    QSpinBox* id = nullptr;
    for (QSpinBox* spinBox : panel.findChildren<QSpinBox*>()) {
        if (spinBox->value() == 50) {
            duration = spinBox;
        }
        if (spinBox->value() == 50003) {
            id = spinBox;
        }
    }
    expect(duration && id, "the section has the fields");
    duration->setValue(40);
    expect(apply->isEnabled() && revert->isEnabled() && panel.hasChanges(),
           "a change can be applied or reverted");
    id->setValue(50004);
    expect(!apply->isEnabled(), "a problem disables Apply");
    id->setValue(50003);
    apply->click();
    expect(appliedIds == std::vector<uint16_t>{50003} && applied.size() == 1 &&
               applied[0].durationMs == 40 && applied[0].frameCount == 68,
           "Apply asks for the scene as the fields say, by its old ID");
    revert->click();
    expect(duration->value() == 50 && !panel.hasChanges() && !apply->isEnabled(),
           "Revert shows the scene's settings again");
    panel.setScene(std::nullopt, nullptr);
    expect(!panel.form()->isEnabled() && !panel.sceneId(), "a frame of no scene: nothing");
}

void testDialog()
{
    document::SceneDefinition scene;
    scene.sceneId = 7;
    scene.frameCount = 4;
    scene.frameGroups = 1;
    scene.repeat = 3;
    scene.interruptable = false;
    scene.autoStartSeconds = 2;
    scene.options = document::kSceneShowPreviousFrameWhenFinished | 64;
    app::SceneDialog dialog(scene, [](const document::SceneDefinition& candidate) {
        return candidate.sceneId == 8 ? std::string("taken") : std::string();
    });
    expect(dialog.scene() == scene, "the dialog gives back the scene it was given");

    QList<QSpinBox*> spinBoxes = dialog.findChildren<QSpinBox*>();
    QSpinBox* id = nullptr;
    QSpinBox* groups = nullptr;
    for (QSpinBox* spinBox : spinBoxes) {
        if (spinBox->value() == 7 && spinBox->maximum() == 65535) {
            id = spinBox;
        }
        if (spinBox->maximum() == 255 && spinBox->minimum() == 1) {
            groups = spinBox;
        }
    }
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    expect(id && groups && buttons, "the dialog has the fields");
    id->setValue(8);
    expect(!buttons->button(QDialogButtonBox::Ok)->isEnabled(), "a problem disables OK");
    id->setValue(9);
    expect(buttons->button(QDialogButtonBox::Ok)->isEnabled(), "and is gone again");
    groups->setValue(3);
    bool sawTotal = false;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        sawTotal = sawTotal || label->text() == QStringLiteral("Frames in the project: 12");
    }
    expect(sawTotal, "the dialog says how many frames the project gets");
    const document::SceneDefinition changed = dialog.scene();
    expect(changed.sceneId == 9 && changed.frameGroups == 3 && changed.repeat == 3 &&
               (changed.options & 64) && changed.autoStartSeconds == 2,
           "a change keeps the rest, flags it does not know included");
    for (QCheckBox* box : dialog.findChildren<QCheckBox*>()) {
        if (box->text() == QStringLiteral("Black dynamic pixels show the scene")) {
            expect(!box->isEnabled(), "black dynamic pixels need a background scene");
        }
    }
}

}  // namespace

void runScenesWidgetTests()
{
    testPanel();
    testThumbnailCaptions();
    testSettingsPanel();
    testProjectSettings();
    testDialog();
}
