// The editor API for plugins (plugin-api/ColorizingDMDPlugin.h), called as a plugin does,
// answered by EditorPlugins over a project; and EditorPlugins' menu and notifications.

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include "EditorPlugins.h"
#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/EditBatch.h"
#include "edit/UndoHistories.h"
#include "vpx/EditorApi.h"

#ifdef COLORIZINGDMD_HAS_EDITOR_PLUGIN_API
#include "ColorizingDMDPlugin.h"
#endif

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: editor plugins: " << message << '\n';
        std::exit(1);
    }
}

#ifdef COLORIZINGDMD_HAS_EDITOR_PLUGIN_API
struct Answers {
    int reviews = 0;
    uint32_t applied = 0;
    int forms = 0;
    int saved = -1;
};

void MSGPIAPI onReviewed(void* userData, uint32_t applied)
{
    auto* answers = static_cast<Answers*>(userData);
    ++answers->reviews;
    answers->applied = applied;
}

void MSGPIAPI onSettings(void* userData, int saved)
{
    auto* answers = static_cast<Answers*>(userData);
    ++answers->forms;
    answers->saved = saved;
}

// Version 5: libserum run over originals, and the identification of frames edited, on a
// project with comparison masks and dynamic zones (RuntimeFixtures::makeFullProject: frames 0
// and 7 are plain, frame 2 has mask 1 and dynamic zones).
void testIdentification()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    edit::UndoHistories histories;
    std::vector<std::string> recorded;
    QMenu menu;
    app::EditorPlugins::Window window;
    window.menu = &menu;
    window.document = [&document]() { return &document; };
    window.editableDocument = [&document]() { return &document; };
    window.recordEdit = [&](std::unique_ptr<edit::EditCommand> command) {
        const auto* batch = dynamic_cast<const edit::EditBatchCommand*>(command.get());
        recorded.push_back(batch ? batch->name() : std::string());
        histories.push(edit::structureHistoryKey(), std::move(command));
    };
    window.currentFrame = []() { return std::optional<std::size_t>(0); };
    window.selectedFrames = []() { return std::vector<std::size_t>{0}; };
    app::EditorPlugins plugins(std::move(window));
    const auto* api = static_cast<const ColorizingDMDEditorAPI*>(vpx::editorApiTable());
    const uint32_t plugin = 7;
    constexpr std::size_t kPixels = 128 * 32;

    // Frame 0, frame 0 again, frame 0 with a pixel no frame has there, frame 7.
    std::vector<uint8_t> originals;
    const auto append = [&originals](const document::IndexedFrame& frame) {
        originals.insert(originals.end(), frame.pixels.begin(), frame.pixels.end());
    };
    append(document.originalFrame(0));
    append(document.originalFrame(0));
    document::IndexedFrame changed = document.originalFrame(0);
    const std::size_t pixel = 16 * 128 + 64;
    changed.pixels[pixel] = static_cast<uint8_t>((changed.pixels[pixel] + 1) % 4);
    append(changed);
    append(document.originalFrame(7));
    std::vector<ColorizingDMDRuntimeResult> results(4);
    std::vector<uint16_t> pictures(4 * kPixels, 1);
    expect(api->RunOriginals(originals.data(), 4, 0, results.data(), pictures.data()) == 1,
           "libserum runs the originals");
    expect(results[0].outcome == CDMDPI_RUNTIME_NEW && results[0].identified == 0 &&
               results[0].shown == 0,
           "the first is frame 0");
    const document::Rgb565Frame& frame0 = document.colorizedFrame(0, render::Resolution::Sd);
    bool same = true;
    for (std::size_t index = 0; index < kPixels; ++index) {
        same = same && pictures[index] == frame0.pixels[index];
    }
    expect(same, "shown in its colors");
    expect(results[1].outcome == CDMDPI_RUNTIME_SAME && results[1].identified == CDMDPI_NONE &&
               results[1].shown == 0,
           "the same again keeps the picture");
    expect(results[2].outcome == CDMDPI_RUNTIME_UNKNOWN && results[2].shown == 0 &&
               pictures[2 * kPixels + 5] == frame0.pixels[5],
           "an unknown frame leaves the display as it is");
    expect(results[3].outcome == CDMDPI_RUNTIME_NEW && results[3].identified == 7,
           "then frame 7");
    std::vector<uint8_t> tooBright(kPixels, 4);
    expect(api->RunOriginals(tooBright.data(), 1, 0, results.data(), nullptr) == 0,
           "a shade the project's frames cannot have is refused");

    // A mask that ignores every pixel makes frame 7 catch the unknown frame.
    const std::vector<uint8_t> everything(kPixels, 1);
    const uint32_t frames[] = {7};
    const std::size_t masks = document.comparisonMaskCount();
    std::vector<uint16_t> colors(4, 0xf800);
    expect(api->SetFrameComparison(plugin, 7, 1, 0) == 0, "no change without a batch");
    expect(api->BeginEdit(plugin, "Identify") == 1, "a batch");
    expect(api->AddComparisonMask(plugin, everything.data(), kPixels, frames, 1) == 1 &&
               document.comparisonMaskCount() == masks + 1 &&
               document.comparisonMaskId(7) == masks,
           "a new mask, given to frame 7");
    expect(api->AddComparisonMask(plugin, everything.data(), 5, frames, 1) == 0 &&
               api->AddComparisonMask(plugin, everything.data(), kPixels, frames + 1, 0) == 1 &&
               document.comparisonMaskCount() == masks + 2,
           "a mask of the wrong size is refused; a mask for no frame is added");
    expect(api->SetFrameComparison(plugin, 0, 1, 1) == 1 && document.comparisonMaskId(0) == 1 &&
               document.shapeComparisonMode(0) == 1 &&
               api->SetFrameComparison(plugin, 0, CDMDPI_NONE, 0) == 1 &&
               document.comparisonMaskId(0) == document::kNoComparisonMaskId &&
               document.shapeComparisonMode(0) == 0,
           "a frame's mask and shape mode, and none again");
    expect(api->SetFrameComparison(plugin, 0, 60, 0) == 0 &&
               api->SetFrameComparison(plugin, 0, 1, 2) == 0,
           "a mask that does not exist or a shape mode other than 0 and 1 is refused");
    std::vector<uint8_t> one(kPixels, 0);
    one[3] = 1;
    expect(api->SetComparisonMask(plugin, 1, one.data(), kPixels) == 1 &&
               document.comparisonMaskPixel(1, 3, 0) == 1 &&
               document.comparisonMaskPixel(1, 4, 0) == 0,
           "a mask's pixels (mask 1 ignored the first 16 of the top row)");
    expect(api->SetDynamicColors(plugin, 2, 4, colors.data(), 4) == 1 &&
               document.dynamicColor(2, document.dynamicPlane(), 4, 3) == 0xf800 &&
               api->SetDynamicColors(plugin, 2, 32, colors.data(), 4) == 0 &&
               api->SetDynamicColors(plugin, 2, 4, colors.data(), 2) == 0,
           "a dynamic set's colors; a set out of range or too few colors is refused");
    expect(api->EndEdit(plugin, 1) == 1 && recorded == std::vector<std::string>{"Identify"},
           "one step");
    expect(api->RunOriginals(changed.pixels.data(), 1, 0, results.data(), nullptr) == 1 &&
               results[0].outcome == CDMDPI_RUNTIME_NEW && results[0].identified == 7,
           "the frame that ignores every pixel catches the unknown frame");
    expect(histories.undo(edit::structureHistoryKey(), document) &&
               document.comparisonMaskCount() == masks &&
               document.comparisonMaskId(7) == document::kNoComparisonMaskId &&
               document.comparisonMaskPixel(1, 4, 0) == 1 &&
               document.dynamicColor(2, document.dynamicPlane(), 4, 3) != 0xf800,
           "one undo takes it all back");
}

// Version 3: pictures, the review window and the settings form, on a project with
// backgrounds and sprites (RuntimeFixtures::makeFullProject).
void testPictures()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    edit::UndoHistories histories;
    std::vector<std::string> recorded;
    std::optional<std::size_t> currentSprite = 1;
    QMenu menu;
    app::EditorPlugins::Window window;
    window.menu = &menu;
    window.document = [&document]() { return &document; };
    window.editableDocument = [&document]() { return &document; };
    window.recordEdit = [&](std::unique_ptr<edit::EditCommand> command) {
        const auto* batch = dynamic_cast<const edit::EditBatchCommand*>(command.get());
        recorded.push_back(batch ? batch->name() : std::string());
        histories.push(edit::structureHistoryKey(), std::move(command));
    };
    window.currentFrame = []() { return std::optional<std::size_t>(4); };
    window.selectedFrames = []() { return std::vector<std::size_t>{4}; };
    window.currentBackground = []() { return std::optional<std::size_t>(2); };
    window.currentSprite = [&currentSprite]() { return currentSprite; };
    Answers answers;
    {
        app::EditorPlugins plugins(std::move(window));
        const auto* api = static_cast<const ColorizingDMDEditorAPI*>(vpx::editorApiTable());
        const uint32_t plugin = 7;

        // Frame 4 has background 0 and looks for sprites 0 and 1; it has no HD.
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> mask(128 * 32, 9);
        expect(api->GetBackgroundMask(4, 0, mask.data(), 128 * 32, &width, &height) == 128 * 32 &&
                   width == 128 && height == 32,
               "a frame's background mask");
        const document::IndexedFrame& sdMask = document.backgroundData(4)->maskSd;
        bool same = true;
        for (std::size_t index = 0; index < mask.size(); ++index) {
            same = same && mask[index] == (sdMask.pixels[index] != 0 ? 1 : 0);
        }
        expect(same, "1 where the background shows through");
        expect(api->GetBackgroundMask(4, 1, nullptr, 0, &width, &height) == 256 * 64 &&
                   width == 256,
               "on the HD plane as well");
        expect(api->GetBackgroundPicture(0, 0, nullptr, 0, &width, &height) == 128 * 32 &&
                   api->GetBackgroundPicture(9, 0, nullptr, 0, nullptr, nullptr) == 0,
               "a background's picture");
        std::vector<uint32_t> sprites(4, 99);
        expect(api->GetFrameSprites(4, sprites.data(), 4) == 2 && sprites[0] == 0 &&
                   sprites[1] == 1,
               "the sprites a frame looks for");
        expect(api->GetSpritePicture(1, 0, nullptr, 0, &width, &height) ==
                       CDMDPI_SPRITE_WIDTH * CDMDPI_SPRITE_HEIGHT &&
                   width == CDMDPI_SPRITE_WIDTH,
               "a sprite's picture");
        std::vector<uint8_t> shape(CDMDPI_SPRITE_WIDTH * CDMDPI_SPRITE_HEIGHT, 9);
        api->GetSpriteMask(1, 0, shape.data(), static_cast<uint32_t>(shape.size()), nullptr,
                           nullptr);
        expect(shape[0] <= 1 &&
                   shape[1 * 256 + 2] ==
                       (document.sprite(1).originalSd.pixels[1 * 256 + 2] != 255 ? 1 : 0),
               "and its shape");
        expect(api->GetCurrentBackground() == 2 && api->GetCurrentSprite() == 1,
               "the background and sprite the author works on");
        currentSprite.reset();
        expect(api->GetCurrentSprite() == CDMDPI_NONE, "none without one");

        // A picture written in a batch: the frame gets HD for it.
        std::vector<uint16_t> picture(256 * 64, 0x07e0);
        expect(api->SetFramePicture(plugin, 4, 1, picture.data(), nullptr, 256 * 64) == 0,
               "no picture without a batch");
        expect(api->BeginEdit(plugin, "Upscale") == 1 &&
                   api->SetFramePicture(plugin, 4, 1, picture.data(), nullptr, 100) == 0 &&
                   api->SetFramePicture(plugin, 4, 1, nullptr, nullptr, 256 * 64) == 0,
               "a picture of the wrong size, or none, is refused");
        expect(api->SetFramePicture(plugin, 4, 1, picture.data(), nullptr, 256 * 64) == 1 &&
                   document.hasHdFrame(4) &&
                   document.colorizedPixel(4, render::Resolution::Hd, 10, 10) == 0x07e0,
               "the frame's HD picture");
        std::vector<uint16_t> sdPicture(128 * 32, 0x001f);
        expect(api->SetBackgroundPicture(plugin, 0, 0, sdPicture.data(), nullptr, 128 * 32) == 1,
               "a background's picture");
        expect(api->EndEdit(plugin, 1) == 1 && recorded.size() == 1 &&
                   histories.undo(edit::structureHistoryKey(), document) &&
                   !document.hasHdFrame(4),
               "one undo step takes it all back");

        // The review window: the editor applies what the author picks.
        std::vector<uint8_t> write(256 * 64, 1);
        write[0] = 0;
        ColorizingDMDPicture proposals[2] = {
            {CDMDPI_PICTURE_FRAME, 4, 1, picture.data(), write.data(), 256 * 64, "Frame 4"},
            {CDMDPI_PICTURE_BACKGROUND, 0, 0, sdPicture.data(), nullptr, 128 * 32, nullptr},
        };
        ColorizingDMDPicture wrong = proposals[0];
        wrong.count = 12;
        expect(api->ReviewPictures(plugin, "Upscale", "Upscale", &wrong, 1, &onReviewed,
                                   &answers) == 0,
               "a proposal of the wrong size is refused");
        // The endpoint's plugin ID is empty without a plugin bus: the windows are kept by it.
        expect(api->ReviewPictures(plugin, "Upscale", "Upscale with SeedVR", proposals, 2,
                                   &onReviewed, &answers) == 1 &&
                   api->ReviewPictures(plugin, "Again", "Again", proposals, 2, &onReviewed,
                                       &answers) == 0,
               "one review window at a time");
        app::PictureReviewDialog* review = plugins.reviewWindow("");
        expect(review && review->chosen().size() == 2 && review->changedPixels() > 0,
               "every proposal is checked at first");
        review->setChecked(1, false);
        plugins.applyReview("");
        expect(answers.reviews == 1 && answers.applied == 1 && recorded.size() == 2 &&
                   recorded.back() == "Upscale with SeedVR" && document.hasHdFrame(4) &&
                   document.colorizedPixel(4, render::Resolution::Hd, 10, 10) == 0x07e0 &&
                   document.colorizedPixel(4, render::Resolution::Hd, 0, 0) != 0x07e0 &&
                   !plugins.reviewWindow(""),
               "Apply writes the checked pictures as one step and tells the plugin");

        expect(api->ReviewPictures(plugin, "Upscale", "Late", proposals, 2, &onReviewed,
                                   &answers) == 1,
               "a second proposal");
        document.setColorizedPixel(5, render::Resolution::Sd, 0, 0, 0x1234);
        plugins.tick();
        review = plugins.reviewWindow("");
        expect(review && review->isOutdated() && !review->canApply(),
               "an edit of the project outdates it");
        plugins.applyReview("");
        expect(recorded.size() == 2 && plugins.reviewWindow(""), "it applies nothing");
        review->reject();
        expect(answers.reviews == 2 && answers.applied == 0 && !plugins.reviewWindow(""),
               "Discard tells the plugin that nothing was applied");

        // The settings form.
        const ColorizingDMDSettingField fields[] = {
            {"url", "Service", CDMDPI_FIELD_TEXT, "http://127.0.0.1:8976", nullptr, "Where"},
            {"token", "Token", CDMDPI_FIELD_PASSWORD, nullptr, nullptr, nullptr},
            {"quantise", "Colors", CDMDPI_FIELD_CHOICE, "frame", "none=None\nframe=Frame",
             nullptr},
            {"dynamic", "Leave dynamic zones", CDMDPI_FIELD_CHECKBOX, "1", nullptr, nullptr},
        };
        ColorizingDMDSettingField noChoices = fields[2];
        noChoices.choices = "";
        expect(api->EditSettings(plugin, "SeedVR", &noChoices, 1, &onSettings, &answers) == 0,
               "a choice without choices is refused");
        plugins.setSetting("", "token", "secret");
        expect(api->EditSettings(plugin, "SeedVR", fields, 4, &onSettings, &answers) == 1,
               "the form opens");
        app::PluginSettingsDialog* form = plugins.settingsWindow("");
        auto* url = form ? qobject_cast<QLineEdit*>(form->input("url")) : nullptr;
        auto* token = form ? qobject_cast<QLineEdit*>(form->input("token")) : nullptr;
        auto* quantise = form ? qobject_cast<QComboBox*>(form->input("quantise")) : nullptr;
        auto* dynamic = form ? qobject_cast<QCheckBox*>(form->input("dynamic")) : nullptr;
        expect(url && token && quantise && dynamic && url->text() == "http://127.0.0.1:8976" &&
                   token->text() == "secret" && token->echoMode() == QLineEdit::Password &&
                   quantise->currentData().toString() == "frame" && dynamic->isChecked(),
               "with the settings, or the defaults while not set");
        url->setText("http://gpu:8976");
        quantise->setCurrentIndex(0);
        dynamic->setChecked(false);
        form->accept();
        expect(answers.forms == 1 && answers.saved == 1 &&
                   plugins.setting("", "url") == "http://gpu:8976" &&
                   plugins.setting("", "quantise") == "none" &&
                   plugins.setting("", "dynamic") == "0" && !plugins.settingsWindow(""),
               "Save keeps the values and tells the plugin");

        // A form of version 4: a note, a file and a consent box, accepted with Start.
        const ColorizingDMDSettingField runFields[] = {
            {nullptr, "What the run does.", CDMDPI_FIELD_NOTE, nullptr, nullptr, nullptr},
            {"pal", "PAL file", CDMDPI_FIELD_FILE, nullptr, "PAL files (*.pal)\nAll files (*)",
             "The colorization"},
            {"consent", "I made it", CDMDPI_FIELD_CONSENT, "1", nullptr, nullptr},
        };
        ColorizingDMDSettingField emptyNote = runFields[0];
        emptyNote.label = nullptr;
        expect(api->ShowForm(plugin, "VNI", "Start", &emptyNote, 1, &onSettings, &answers) == 0,
               "a note without text is refused");
        ColorizingDMDSettingField unknown = runFields[1];
        unknown.kind = CDMDPI_FIELD_CONSENT + 1;
        expect(api->ShowForm(plugin, "VNI", "Start", &unknown, 1, &onSettings, &answers) == 0,
               "a field of an unknown kind is refused");
        plugins.setSetting("", "pal", "/colorizations/game/pin2dmd.pal");
        plugins.setSetting("", "consent", "1");
        expect(api->ShowForm(plugin, "VNI", "Start", runFields, 3, &onSettings, &answers) == 1,
               "the form opens");
        form = plugins.settingsWindow("");
        auto* pal = form ? qobject_cast<QLineEdit*>(form->input("pal")) : nullptr;
        auto* consent = form ? qobject_cast<QCheckBox*>(form->input("consent")) : nullptr;
        expect(pal && consent && pal->text() == "/colorizations/game/pin2dmd.pal" &&
                   !consent->isChecked() && form->acceptButton() &&
                   form->acceptButton()->text() == "Start" &&
                   !form->acceptButton()->isEnabled() && !form->canAccept(),
               "the file with its setting, the consent box unchecked although it was given");
        form->accept();
        expect(answers.forms == 1 && plugins.settingsWindow("") == form,
               "it is not accepted without consent");
        consent->setChecked(true);
        expect(form->acceptButton()->isEnabled(), "consent enables Start");
        pal->setText("/colorizations/other.pal");
        form->accept();
        const std::map<std::string, std::string> kept{
            {"pal", plugins.setting("", "pal")}, {"consent", plugins.setting("", "consent")}};
        expect(answers.forms == 2 && answers.saved == 1 &&
                   kept.at("pal") == "/colorizations/other.pal" && kept.at("consent") == "1" &&
                   plugins.setting("", "").empty() && !plugins.settingsWindow(""),
               "Start keeps the values, the note has none");
        expect(api->EditSettings(plugin, "VNI", runFields, 3, &onSettings, &answers) == 1,
               "EditSettings takes the fields of version 4 too");
        form = plugins.settingsWindow("");
        expect(form && form->acceptButton()->text().contains("Save"),
               "with its Save button");
        form->reject();
        expect(answers.forms == 3 && answers.saved == 0, "Cancel tells the plugin");

        // A window still open when the plugins go: closed without calling back.
        expect(api->ReviewPictures(plugin, "Upscale", "Gone", proposals, 1, &onReviewed,
                                   &answers) == 1,
               "a window left open");
    }
    expect(answers.reviews == 2, "is closed without calling back into a plugin that is gone");
}
#endif

}  // namespace

void runEditorPluginsWidgetTests()
{
    document::ProjectDocument document = document::ProjectDocument::createEmpty({"api", 3, false, 16});
    document.setColorizedPixel(1, render::Resolution::Sd, 5, 2, 0xf800);
    document.markClean();
    std::optional<std::size_t> current = 1;
    std::vector<std::size_t> selected{1};
    std::vector<std::size_t> shown;
    edit::UndoHistories histories;
    std::vector<std::string> recorded;
    QMenu menu;

    app::EditorPlugins::Window window;
    window.menu = &menu;
    window.document = [&document]() { return &document; };
    window.editableDocument = [&document]() { return &document; };
    window.recordEdit = [&](std::unique_ptr<edit::EditCommand> command) {
        const auto* batch = dynamic_cast<const edit::EditBatchCommand*>(command.get());
        recorded.push_back(batch ? batch->name() : std::string());
        histories.push(edit::structureHistoryKey(), std::move(command));
    };
    window.currentFrame = [&current]() { return current; };
    window.selectedFrames = [&selected]() { return selected; };
    window.showFrames = [&](std::size_t frame, const std::vector<std::size_t>& frames) {
        current = frame;
        shown = frames;
    };
    app::EditorPlugins plugins(std::move(window));

    // The menu shows only while a plugin has entries in it.
    expect(!menu.menuAction()->isVisible(), "the Plugins menu is hidden without entries");
    int triggered = 0;
    plugins.addMenuEntry("Test", "Do it", [&triggered]() { ++triggered; });
    expect(menu.menuAction()->isVisible() && menu.actions().size() == 1,
           "a plugin's entry shows the menu");
    menu.actions().front()->trigger();
    expect(triggered == 1, "the entry runs the plugin's action");
    plugins.removeMenuEntries("Test");
    expect(!menu.menuAction()->isVisible() && menu.actions().isEmpty(),
           "its entries go with the plugin");

#ifdef COLORIZINGDMD_HAS_EDITOR_PLUGIN_API
    const auto* api = static_cast<const ColorizingDMDEditorAPI*>(vpx::editorApiTable());
    expect(api && api->version == 5, "the API table is version 5");

    ColorizingDMDProjectInfo project{};
    expect(api->GetProjectInfo(&project) == 1 && project.hasProject == 1 &&
               std::string(project.romName) == "api" && project.frameCount == 3 &&
               project.colorCount == 16 && project.sdWidth == 128 && project.sdHeight == 32 &&
               project.hdWidth == 256 && project.hdHeight == 64 &&
               project.revision == document.revision(),
           "the project summary");

    ColorizingDMDFrameInfo frame{};
    const uint8_t maskId = document.comparisonMaskId(1);
    expect(api->GetFrameInfo(1, &frame) == 1 &&
               frame.comparisonMask ==
                   (maskId == document::kNoComparisonMaskId ? CDMDPI_NONE : maskId) &&
               frame.background == CDMDPI_NONE && frame.triggerId == CDMDPI_NONE &&
               frame.section == CDMDPI_NONE && frame.hasDynamicPixels == 0 && frame.hasHd == 0,
           "a frame's summary");
    expect(api->GetFrameInfo(3, &frame) == 0, "no summary for a frame that does not exist");

    uint32_t width = 0;
    uint32_t height = 0;
    expect(api->GetOriginalFrame(0, nullptr, 0, &width, &height) == 128 * 32 && width == 128 &&
               height == 32,
           "asking with no buffer gives the size");
    std::vector<uint16_t> colorized(128 * 32, 1);
    expect(api->GetColorizedFrame(1, 0, colorized.data(),
                                  static_cast<uint32_t>(colorized.size()), &width, &height) ==
                   colorized.size() &&
               colorized[2 * 128 + 5] == 0xf800 && colorized[0] == 0,
           "the colorized frame");
    std::vector<uint16_t> tooSmall(10, 7);
    api->GetColorizedFrame(1, 0, tooSmall.data(), 10, nullptr, nullptr);
    expect(tooSmall[0] == 7, "a buffer that is too small stays untouched");
    std::vector<uint8_t> dynamicMask(128 * 32, 0);
    expect(api->GetDynamicMask(1, dynamicMask.data(), static_cast<uint32_t>(dynamicMask.size()),
                               nullptr, nullptr) == dynamicMask.size() &&
               dynamicMask[100] == 255,
           "a frame without dynamic zones: every pixel outside them");
    expect(api->GetDynamicColors(1, nullptr, 0) == CDMDPI_DYNAMIC_SETS * 16,
           "32 dynamic sets of 16 colors");

    expect(api->GetCurrentFrame() == 1, "the current frame");
    selected = {0, 2};
    std::vector<uint32_t> frames(4, 9);
    expect(api->GetSelectedFrames(frames.data(), 4) == 2 && frames[0] == 0 && frames[1] == 2,
           "the selected frames");
    const uint32_t toShow[] = {0, 1};
    expect(api->ShowFrames(2, toShow, 2) == 1 && current == 2 && shown.size() == 2,
           "a plugin shows frames");
    const uint32_t missing[] = {7};
    expect(api->ShowFrames(0, missing, 1) == 0 && current == 2,
           "but not frames that do not exist");

    // Editing: a batch of changes, one undo step.
    const uint32_t plugin = 7;
    std::vector<uint8_t> keep(128 * 32, 0);
    expect(api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32, CDMDPI_NONE) == 0,
           "no change without a batch");
    expect(api->BeginEdit(plugin, "Strip") == 1 && api->BeginEdit(plugin, "Again") == 0,
           "one batch at a time");
    expect(api->ResetFramePixels(plugin + 1, 1, keep.data(), 128 * 32, CDMDPI_NONE) == 0,
           "only for the plugin that began it");
    expect(api->ResetFramePixels(plugin, 1, keep.data(), 5, CDMDPI_NONE) == 0,
           "a keep mask of the wrong size is refused");
    std::vector<uint16_t> plain(16, 1);
    expect(api->GetPlainColors(plain.data(), 16) == 16, "the plain colors of 16 shades");
    expect(api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32, CDMDPI_NONE) == 1 &&
               document.colorizedPixel(1, render::Resolution::Sd, 5, 2) ==
                   plain[document.originalFrame(1).pixels[2 * 128 + 5]],
           "the frame gets the original's plain look at once");
    expect(api->ResetFramePixels(plugin, 2, keep.data(), 128 * 32, 0x10000) == 0 &&
               api->ResetFramePixels(plugin, 2, keep.data(), 128 * 32, 0) == 1 &&
               document.colorizedPixel(2, render::Resolution::Sd, 9, 9) == 0,
           "a fill color, RGB565");
    std::vector<uint8_t> zones(128 * 32, 255);
    zones[0] = 40;
    expect(api->SetDynamicZones(plugin, 1, zones.data(), 128 * 32) == 0,
           "a dynamic set out of range is refused");
    expect(api->ClearColorRotations(plugin, 1) == 1 && api->DeleteBackground(plugin, 0) == 0,
           "a change that changes nothing is made; a background that does not exist is refused");
    expect(api->EndEdit(plugin + 1, 1) == 0 && api->EndEdit(plugin, 1) == 1 &&
               recorded == std::vector<std::string>{"Strip"},
           "the batch is recorded as one step under its name");
    expect(histories.undo(edit::structureHistoryKey(), document) &&
               document.colorizedPixel(1, render::Resolution::Sd, 5, 2) == 0xf800,
           "and undone as one");

    expect(api->BeginEdit(plugin, "Cancelled") == 1 &&
               api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32, CDMDPI_NONE) == 1 &&
               api->EndEdit(plugin, 0) == 1 &&
               document.colorizedPixel(1, render::Resolution::Sd, 5, 2) == 0xf800 &&
               recorded.size() == 1,
           "a batch ended without applying takes its changes back");
    expect(api->BeginEdit(plugin, "Left open") == 1 &&
               api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32, CDMDPI_NONE) == 1,
           "a batch left open");
    plugins.tick();
    expect(recorded.size() == 2 && recorded.back() == "Left open" &&
               api->BeginEdit(plugin, "Next") == 1 && api->EndEdit(plugin, 1) == 1 &&
               recorded.size() == 2,
           "is recorded by the next bus tick; an empty batch records nothing");
#endif

    // A plugin's settings, in an INI file of a test organization.
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("EditorPluginsWidgetTests"));
    expect(plugins.setting("Test", "port").empty(), "a setting is empty until set");
    plugins.setSetting("Test", "port", "8975");
    expect(plugins.setting("Test", "port") == "8975" && plugins.setting("Other", "port").empty(),
           "a plugin's setting is kept, for that plugin");
    expect(app::EditorPlugins::enabledInSettings().isEmpty() && plugins.loaded().isEmpty(),
           "no plugin is chosen at first");
#ifdef COLORIZINGDMD_HAS_EDITOR_PLUGIN_API
    testPictures();
    testIdentification();
#endif
    QSettings().remove(QStringLiteral("plugins"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
