// Scenes: the inspector's Scenes and Scaling section and the scene dialog.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>

#include "RuntimeFixtures.h"
#include "dialogs/SceneDialog.h"
#include "document/ProjectDocument.h"
#include "edit/SceneEdits.h"
#include "edit/StructureCommand.h"
#include "panels/ProjectSettingsPanel.h"
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
    panel.onEditRequested = [&](uint16_t id) { edits.push_back(id); };
    panel.setProject(&document);
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

    document::ProjectDocument native = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::NativeOriginal, 2));
    panel.setProject(&native);
    expect(list->count() == 0 && !add->isEnabled(), "a 256x64 project offers no scenes");
    (void)added;
    (void)deleted;
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
    testProjectSettings();
    testDialog();
}
