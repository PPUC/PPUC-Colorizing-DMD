// Scenes: the inspector's Scenes and Scaling section and the scene dialog.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QApplication>
#include <QListWidget>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QPushButton>
#include <QSpinBox>

#include "FrameImageUtils.h"
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
#include "runtime/RuntimePreview.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: scenes widgets: " << message << '\n';
        std::exit(1);
    }
}

QAbstractButton* button(QWidget& parent, const QString& text)
{
    for (QAbstractButton* candidate : parent.findChildren<QAbstractButton*>()) {
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
    int selectionChanges = 0;
    panel.onSelectionChanged = [&]() { ++selectionChanges; };
    panel.setProject(&document);
    expect(selectionChanges == 0, "filling the list reports no selection");
    auto* list = panel.findChild<QListWidget*>();
    expect(list && list->count() == 1 && list->item(0)->text().contains(QStringLiteral("51001")) &&
               list->item(0)->text().contains(QStringLiteral("2 × 3")) &&
               list->item(0)->text().contains(QStringLiteral("background")) &&
               list->item(0)->text().contains(QStringLiteral("1 missing")),
           "the section lists the scene with its groups, kind and missing frames");
    QAbstractButton* add = button(panel, QStringLiteral("New"));
    QAbstractButton* deleteButton = button(panel, QStringLiteral("Delete"));
    expect(add && add->isEnabled() && deleteButton && !deleteButton->isEnabled() &&
               !button(panel, QStringLiteral("Edit")) &&
               !button(panel, QStringLiteral("Show Frames")),
           "New works in a 128x32 project, Delete needs a selected scene; no Edit or Show "
           "Frames (the Inspector's Scene section and the double click)");
    list->setCurrentRow(0);
    expect(deleteButton->isEnabled() && panel.selectedScene() == 51001,
           "a selected scene can be deleted");
    std::vector<int> opened;
    panel.onOpenRequested = [&](uint16_t id) { opened.push_back(id); };
    list->itemDoubleClicked(list->item(0));
    expect(opened == std::vector<int>{51001},
           "a double click shows the scene's frames instead of opening the window");
    expect(panel.dragSource() == list && list->dragEnabled() &&
               list->dragDropMode() == QAbstractItemView::DragOnly,
           "a scene can be dragged (onto the canvas, to become the frames' trigger)");
    expect(selectionChanges == 1, "selecting a scene reports it (the frame filter follows)");
    // "Used by the current frame": only the frame's scene.
    panel.showOnlyScene(true, uint16_t{51001});
    expect(!list->item(0)->isHidden(), "the current frame's scene stays listed");
    panel.showOnlyScene(true, std::nullopt);
    expect(list->item(0)->isHidden(), "a frame of no scene lists none");
    // "Not used by any frame": the scenes given.
    panel.showOnlyScenes(true, {uint16_t{51001}});
    expect(!list->item(0)->isHidden(), "a scene given stays listed");
    panel.showOnlyScenes(true, {});
    expect(list->item(0)->isHidden(), "none given lists none");
    panel.showOnlyScene(false, std::nullopt);
    expect(!list->item(0)->isHidden(), "unfiltered, every scene is listed");
    // The scene's thumbnail: its first frame.
    panel.buildAllThumbnails();
    expect(!list->item(0)->icon().isNull(),
           "a scene has the thumbnail of its first frame");

    // Sorted by ID, whatever order they were added in (SoyGre).
    scene.sceneId = 50002;
    scene.options = 0;
    auto second = edit::addScene(document, scene);
    scene.sceneId = 50001;
    auto third = edit::addScene(document, scene);
    panel.setProject(&document);
    expect(list->count() == 3 && list->item(0)->data(Qt::UserRole).toInt() == 50001 &&
               list->item(1)->data(Qt::UserRole).toInt() == 50002 &&
               list->item(2)->data(Qt::UserRole).toInt() == 51001,
           "the scenes are listed by their IDs");

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
    // The upscaling, the shadow offset, the speaker lights' four defaults, then the license;
    // no libserum to choose (projects are made for libserum 2.9).
    expect(boxes.size() == 7 && boxes[0]->currentData().toInt() == document::kScalingScale2x &&
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
    expect(dialog.isModal() && dialogBoxes.size() == 7 &&
               dialog.shadowOffset() == document::kShadowOffsetProportional,
           "the dialog shows the project's settings");
    dialogBoxes[0]->setCurrentIndex(dialogBoxes[0]->findData(document::kScalingLineDoubling));
    expect(dialog.scalingAlgorithm() == document::kScalingLineDoubling &&
               document.colorizationSettings()->scalingAlgorithm == document::kScalingScale2x,
           "a choice in the dialog changes nothing until the owner applies it");
}

// The license in the project settings.
void testLicense()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    document::ProjectLicense license;
    license.kind = document::LicenseKind::CcBySa;
    license.attribution = "Kraken";
    license.url = "https://example.org";
    license.customText = "Old terms";
    document.setLicense(license);
    app::ProjectSettingsDialog dialog(document);
    app::ProjectSettingsPanel* panel = dialog.panel();
    dialog.show();
    QApplication::processEvents();
    expect(dialog.license() == license && panel->attributionEdit()->text() == "Kraken" &&
               panel->licenseUrlEdit()->text() == "https://example.org",
           "the dialog shows the project's license");
    expect(panel->licenseLabel()->text().contains(
               QStringLiteral("https://creativecommons.org/licenses/by-sa/4.0/")) &&
               panel->licenseLabel()->openExternalLinks() &&
               !panel->licenseTextEdit()->isVisible(),
           "a Creative Commons license links its deed and has no text of its own");
    QComboBox* box = panel->licenseComboBox();
    box->setCurrentIndex(box->findData(static_cast<int>(document::LicenseKind::Custom)));
    expect(panel->licenseTextEdit()->isVisible() &&
               panel->licenseTextEdit()->toPlainText() == "Old terms" &&
               !panel->licenseLabel()->text().contains(QStringLiteral("href")),
           "a custom license shows its text");
    panel->licenseTextEdit()->setPlainText(QStringLiteral("Ask first."));
    panel->attributionEdit()->setText(QStringLiteral("  Kraken, Enthusiast  "));
    const document::ProjectLicense chosen = dialog.license();
    expect(chosen.kind == document::LicenseKind::Custom && chosen.customText == "Ask first." &&
               chosen.attribution == "Kraken, Enthusiast" && document.license() == license,
           "the choice is read trimmed and changes nothing until it is applied");
    panel->setProject(nullptr);
    expect(!box->isEnabled() && !panel->attributionEdit()->isEnabled(),
           "without a project the license is off");
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
    QAbstractButton* apply = button(panel, QStringLiteral("Apply"));
    QAbstractButton* revert = button(panel, QStringLiteral("Revert"));
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

// The canvas shows the first frame of the background scene a frame starts through the frame's
// background mask, in place of a background of its own, still, so that nothing moves while
// drawing; on HD upscaled, the scene frame having no HD.
void testBackgroundSceneOnTheCanvas()
{
    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3);
    runtime_fixtures::assignBackground(snapshot, 0, document::kNoBackgroundId);
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(snapshot);
    document::SceneDefinition scene;
    scene.sceneId = 50700;
    scene.frameCount = 2;
    scene.options = document::kSceneAsBackground;
    const auto added = edit::addScene(document, scene);
    document.setTriggerId(0, 50700);
    const std::size_t first = edit::sceneFrames(document, 50700).front();
    const document::IndexedFrame& original = document.originalFrame(0);
    int unlitX = -1;
    int unlitY = -1;
    for (int y = 0; y < original.size.height && unlitX < 0; ++y) {
        for (int x = 0; x < original.size.width; ++x) {
            if (original.pixels[static_cast<std::size_t>(y) * original.size.width + x] == 0) {
                unlitX = x;
                unlitY = y;
                break;
            }
        }
    }
    const document::Rgb565Frame& sceneSd = document.colorizedFrame(first, render::Resolution::Sd);
    const std::size_t sdIndex = static_cast<std::size_t>(unlitY) * sceneSd.size.width + unlitX;
    expect(unlitX >= 0 &&
               app::resolvedFramePixel(document, 0, render::Resolution::Sd, unlitX, unlitY,
                                       true) == sceneSd.pixels[sdIndex] &&
               app::resolvedFramePixel(document, 0, render::Resolution::Sd, unlitX, unlitY,
                                       false) ==
                   document.colorizedFrame(0, render::Resolution::Sd).pixels[sdIndex],
           "SD: the scene's first frame shows through the mask, not without the background");
    // Played, the scene's frames follow each other, one per durationMs, in a loop.
    const std::size_t second = edit::sceneFrames(document, 50700)[1];
    document.setColorizedPixel(first, render::Resolution::Sd, unlitX, unlitY, 0x1234);
    document.setColorizedPixel(second, render::Resolution::Sd, unlitX, unlitY, 0x4321);
    const auto shownAt = [&](uint32_t elapsedMs) {
        return app::composeResolvedFrame(document, 0, render::Resolution::Sd, true, nullptr,
                                         true, elapsedMs)
            .pixels[sdIndex];
    };
    expect(shownAt(0) == 0x1234 && shownAt(scene.durationMs - 1) == 0x1234 &&
               shownAt(scene.durationMs) == 0x4321 && shownAt(2 * scene.durationMs) == 0x1234,
           "the scene plays through the mask");
    document.createHdFrame(0);
    const document::Rgb565Frame* sceneHd = document.backgroundPicture(0, render::Resolution::Hd);
    expect(sceneHd &&
               app::resolvedFramePixel(document, 0, render::Resolution::Hd, unlitX * 2,
                                       unlitY * 2, true) ==
                   sceneHd->pixels[static_cast<std::size_t>(unlitY * 2) * sceneHd->size.width +
                                   unlitX * 2],
           "HD: the scene's first frame upscaled");
}

// The options of a background scene decide more of the picture, as in libserum: with "Keep
// only dynamic content in front" lit static pixels under the background mask show the scene;
// with "Black dynamic pixels show the scene" a lit dynamic pixel whose set color is black shows
// it there, and an unlit one whose set has a color for shade 0 keeps that color. The canvas
// shows what libserum renders on the SD plane, pixel for pixel.
void testBackgroundSceneOptionsOnTheCanvas()
{
    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2);
    runtime_fixtures::assignBackground(snapshot, 0, document::kNoBackgroundId);
    document::FrameDynamicData& dynamic = runtime_fixtures::addDynamicData(snapshot, 0);
    // Zone of set 1 over the left half; its shade 1 is black, shade 0 has a color.
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            dynamic.maskSd.pixels[static_cast<std::size_t>(y) * 128 + x] = 1;
        }
    }
    const std::size_t colors = runtime_fixtures::kColorCount;
    dynamic.colorsSd[1 * colors + 0] = 0x0841;
    dynamic.colorsSd[1 * colors + 1] = 0;
    dynamic.colorsSd[1 * colors + 2] = 0xf800;
    dynamic.colorsSd[1 * colors + 3] = 0x07e0;
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(snapshot);
    document::SceneDefinition scene;
    scene.sceneId = 51500;
    scene.frameCount = 2;
    scene.repeat = 1;
    scene.options = document::kSceneAsBackground | document::kSceneOnlyDynamicContent |
                    document::kSceneReplaceDynamicBlack;
    const auto added = edit::addScene(document, scene);
    document.setTriggerId(0, 51500);
    const std::size_t first = edit::sceneFrames(document, 51500).front();

    const document::Rgb565Frame canvas =
        app::composeResolvedFrame(document, 0, render::Resolution::Sd, true);
    const document::Rgb565Frame& scenePicture =
        document.colorizedFrame(first, render::Resolution::Sd);
    const document::IndexedFrame& original = document.originalFrame(0);
    bool staticChecked = false;
    bool blackChecked = false;
    bool unlitChecked = false;
    for (std::size_t i = 0; i < original.pixels.size(); ++i) {
        const bool zone = i % 128 < 64;
        const uint8_t shade = original.pixels[i];
        if (!zone && shade != 0 && !staticChecked) {
            expect(canvas.pixels[i] == scenePicture.pixels[i],
                   "a lit static pixel under the mask shows the scene");
            staticChecked = true;
        } else if (zone && shade == 1 && !blackChecked) {
            expect(canvas.pixels[i] == scenePicture.pixels[i],
                   "a lit dynamic pixel of a black set color shows the scene");
            blackChecked = true;
        } else if (zone && shade == 0 && !unlitChecked) {
            expect(canvas.pixels[i] == 0x0841, "an unlit dynamic pixel keeps its set color");
            unlitChecked = true;
        }
    }
    expect(staticChecked && blackChecked && unlitChecked, "the fixture has every case");

    runtime::RuntimePreview& preview = runtime::RuntimePreview::instance();
    std::string error;
    expect(preview.openSequence(document, {0}, runtime::PlaneRequest::Rows32, &error),
           "libserum loads the frame with its scene: " + error);
    const runtime::RuntimeFrame rendered = preview.renderSequenceFrame(0);
    preview.close();
    const runtime::RuntimePlane* plane = rendered.plane(runtime::PlaneRequest::Rows32);
    std::size_t differing = 0;
    for (std::size_t i = 0; plane && i < canvas.pixels.size(); ++i) {
        differing += canvas.pixels[i] != plane->image.pixels[i] ? 1 : 0;
    }
    expect(plane && differing == 0,
           "the canvas shows what libserum renders: " + std::to_string(differing) +
               " pixels differ");
}

void runScenesWidgetTests()
{
    testPanel();
    testBackgroundSceneOnTheCanvas();
    testBackgroundSceneOptionsOnTheCanvas();
    testThumbnailCaptions();
    testSettingsPanel();
    testProjectSettings();
    testLicense();
    testDialog();
}
