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
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>

#include "EditorPlugins.h"
#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "document/Scene.h"
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
    // As a team project refuses an edit a partner's claim blocks: taken back, not recorded.
    bool refuse = false;
    window.recordEdit = [&](std::unique_ptr<edit::EditCommand> command) {
        if (refuse) {
            command->revert(document);
            return false;
        }
        const auto* batch = dynamic_cast<const edit::EditBatchCommand*>(command.get());
        recorded.push_back(batch ? batch->name() : std::string());
        histories.push(edit::structureHistoryKey(), std::move(command));
        return true;
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

    // Version 6: the changed frame 0 as a frame of its own, made like frame 2.
    const std::size_t frameCount = document.frameCount();
    uint32_t added = CDMDPI_NONE;
    expect(api->AddFrame(plugin, changed.pixels.data(), kPixels, 2, &added) == 0,
           "no frame without a batch");
    expect(api->BeginEdit(plugin, "Add") == 1, "a batch for the frame");
    expect(api->AddFrame(plugin, changed.pixels.data(), 5, 2, &added) == 0 &&
               api->AddFrame(plugin, changed.pixels.data(), kPixels, 999, &added) == 0 &&
               api->AddFrame(plugin, tooBright.data(), kPixels, 2, &added) == 0 &&
               added == CDMDPI_NONE && document.frameCount() == frameCount,
           "an original of the wrong size or shades, or a frame that does not exist, is refused");
    expect(api->AddFrame(plugin, changed.pixels.data(), kPixels, 2, &added) == 1 &&
               added == frameCount && document.frameCount() == frameCount + 1 &&
               document.originalFrame(added).pixels.toVector() == changed.pixels.toVector() &&
               document.hasDynamicPixels(added),
           "the frame is appended, with the original and frame 2's dynamic zones");
    expect(api->EndEdit(plugin, 1) == 1 && recorded.back() == "Add", "one step");
    expect(api->RunOriginals(changed.pixels.data(), 1, 0, results.data(), nullptr) == 1 &&
               results[0].outcome == CDMDPI_RUNTIME_NEW && results[0].identified == added,
           "libserum identifies the frame that was unknown");
    expect(histories.undo(edit::structureHistoryKey(), document) &&
               document.frameCount() == frameCount,
           "undo takes the frame back");

    // Version 6: frames moved, and a batch the editor refuses.
    const document::IndexedFrame first = document.originalFrame(0);
    const document::IndexedFrame second = document.originalFrame(1);
    const uint32_t moved[] = {1};
    expect(api->BeginEdit(plugin, "Move") == 1 &&
               api->MoveFrames(plugin, moved, 1, static_cast<uint32_t>(frameCount) + 1) == 0 &&
               api->MoveFrames(plugin, moved, 1, 0) == 1,
           "a place behind the frame count is refused; frame 1 goes before frame 0");
    expect(document.originalFrame(0).pixels.toVector() == second.pixels.toVector() &&
               document.originalFrame(1).pixels.toVector() == first.pixels.toVector(),
           "frames 0 and 1 changed places");
    expect(api->EndEdit(plugin, 1) == 1 && recorded.back() == "Move", "the move is one step");
    expect(histories.undo(edit::structureHistoryKey(), document) &&
               document.originalFrame(0).pixels.toVector() == first.pixels.toVector(),
           "undo moves them back");
    refuse = true;
    expect(api->BeginEdit(plugin, "Refused") == 1 &&
               api->AddFrame(plugin, changed.pixels.data(), kPixels, 2, &added) == 1 &&
               document.frameCount() == frameCount + 1,
           "the frame is there while the batch is open");
    expect(api->EndEdit(plugin, 1) == 0 && document.frameCount() == frameCount &&
               recorded.back() == "Move",
           "EndEdit tells the plugin when the editor refused the batch");
    refuse = false;

    // Version 7: Copy Frame Settings, here the shadows of frame 2 on a frame made like it.
    const render::Resolution plane = document.dynamicPlane();
    expect(api->BeginEdit(plugin, "Shadows") == 1 &&
               api->AddFrame(plugin, changed.pixels.data(), kPixels, 2, &added) == 1,
           "a frame like frame 2");
    document.setDynamicShadowDirection(2, plane, 0, 5);
    document.setDynamicShadowColor(2, plane, 0, 0x07e0);
    const uint32_t targets[] = {added};
    expect(document.dynamicShadowDirection(added, plane, 0) != 5,
           "the new frame has the shadow frame 2 had before");
    expect(api->CopyFrameSettings(plugin, 2, targets, 1, 0) == 0 &&
               api->CopyFrameSettings(plugin, 2, targets, 1, 0x1000) == 0 &&
               api->CopyFrameSettings(plugin, 999, targets, 1, CDMDPI_SETTINGS_DURATION) == 0,
           "no part, an unknown part or a frame that does not exist is refused");
    expect(api->CopyFrameSettings(plugin, 2, targets, 1, CDMDPI_SETTINGS_DYNAMIC_SHADOWS) == 1 &&
               document.dynamicShadowDirection(added, plane, 0) == 5 &&
               document.dynamicShadowColor(added, plane, 0) == 0x07e0,
           "the shadow of frame 2 is copied");
    expect(api->CopyFrameSettings(plugin, 2, targets, 1, CDMDPI_SETTINGS_DYNAMIC_SHADOWS) == 1,
           "copying what is equal already is no refusal");
    expect(api->EndEdit(plugin, 1) == 1 && recorded.back() == "Shadows", "one step");

    // Version 8: Create HD, deleting frames, sections.
    const uint32_t hdFrames[] = {2, 4};
    expect(api->BeginEdit(plugin, "Tidy") == 1 &&
               api->CreateFrameHd(plugin, hdFrames, 2, 9, 0) == 0 &&
               api->CreateFrameHd(plugin, hdFrames, 2, CDMDPI_UPSCALE_SCALE2X, 4) == 0 &&
               !document.hasHdFrame(2),
           "an unknown filter or flag is refused");
    expect(api->CreateFrameHd(plugin, hdFrames, 2, CDMDPI_UPSCALE_SCALE2X,
                              CDMDPI_HD_BACKGROUND_MASK | CDMDPI_HD_BACKGROUND_IMAGE) == 1 &&
               document.hasHdFrame(2) && document.hasHdFrame(4),
           "frames 2 and 4 get HD");
    expect(api->CreateSpriteHd(plugin, 999, CDMDPI_UPSCALE_NEAREST) == 0 &&
               api->CreateSpriteHd(plugin, 0, CDMDPI_UPSCALE_NEAREST) == 1 &&
               document.hasHdSprite(0),
           "a sprite gets HD; one that does not exist is refused");
    const std::size_t framesBefore = document.frameCount();
    const document::IndexedFrame third = document.originalFrame(3);
    const uint32_t doomed[] = {1, 2};
    expect(api->DeleteFrames(plugin, doomed, 2) == 1 &&
               document.frameCount() == framesBefore - 2 &&
               document.originalFrame(1).pixels.toVector() == third.pixels.toVector(),
           "frames 1 and 2 are deleted, the frames behind move down");
    ColorizingDMDProjectInfo info{};
    api->GetProjectInfo(&info);
    const uint32_t sectionsBefore = info.sectionCount;
    expect(api->SetSection(plugin, 2, "") == 0 && api->SetSection(plugin, 2, "Finals") == 1,
           "an empty name is refused; a section starts at frame 2");
    api->GetProjectInfo(&info);
    char name[64] = {};
    uint32_t sectionStart = CDMDPI_NONE;
    expect(info.sectionCount == sectionsBefore + 1 &&
               api->GetSection(info.sectionCount - 1, name, sizeof name, &sectionStart) == 1 &&
               std::string(name) == "Finals" && sectionStart == 2,
           "the new section is the last in the list");
    expect(api->SetSection(plugin, 2, "Final scores") == 1 &&
               api->GetSection(info.sectionCount - 1, name, sizeof name, &sectionStart) == 1 &&
               std::string(name) == "Final scores",
           "renamed");
    expect(api->DeleteSection(plugin, info.sectionCount - 1) == 1 &&
               api->DeleteSection(plugin, 999) == 0,
           "deleted; one that does not exist is refused");
    expect(api->EndEdit(plugin, 1) == 1 && recorded.back() == "Tidy", "one step");
    expect(histories.undo(edit::structureHistoryKey(), document) &&
               document.frameCount() == framesBefore && !document.hasHdFrame(2) &&
               !document.hasHdSprite(0),
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
        return true;
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
        unknown.kind = CDMDPI_FIELD_RADIO + 1;
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

// What a plugin with a live form sees: the fields the author changed, and how the form closed.
struct LiveForm {
    const ColorizingDMDEditorAPI* api = nullptr;
    uint32_t plugin = 0;
    std::vector<std::string> changed;
    std::vector<uint32_t> closed;
    // The value of "transition" while the plugin hears that the form closed.
    std::string closingValue;
    // Opens the form again when it closes with this result (a wizard's Back).
    uint32_t reopenOn = CDMDPI_NONE;
    const ColorizingDMDFormField* fields = nullptr;
    uint32_t fieldCount = 0;
};

void MSGPIAPI onFormChanged(void* userData, const char* key);
void MSGPIAPI onFormClosed(void* userData, uint32_t result);

int openLiveForm(LiveForm& form)
{
    const char* const buttons[] = {"Back"};
    return form.api->ShowLiveForm(form.plugin, "Transition", "Apply", buttons, 1, form.fields,
                                  form.fieldCount, &onFormChanged, &onFormClosed, &form);
}

void MSGPIAPI onFormChanged(void* userData, const char* key)
{
    auto* form = static_cast<LiveForm*>(userData);
    form->changed.emplace_back(key);
    if (std::string(key) != "transition") {
        return;
    }
    // A slide has a direction and at most 5 frames.
    char value[16] = {};
    if (form->api->GetFormValue(form->plugin, "transition", value, sizeof(value)) == 6 &&
        std::string(value) == "slide") {
        ColorizingDMDFormFieldUpdate option{};
        option.changes = CDMDPI_FORM_SET_ENABLED;
        option.enabled = 1;
        form->api->SetFormField(form->plugin, "option", &option);
        ColorizingDMDFormFieldUpdate frames{};
        frames.changes = CDMDPI_FORM_SET_CHOICES | CDMDPI_FORM_SET_HELP;
        frames.choices = "1\n2\n3\n4\n5";
        frames.help = "At most 5";
        form->api->SetFormField(form->plugin, "frames", &frames);
    }
}

void MSGPIAPI onFormClosed(void* userData, uint32_t result)
{
    auto* form = static_cast<LiveForm*>(userData);
    form->closed.push_back(result);
    char value[16] = {};
    form->api->GetFormValue(form->plugin, "transition", value, sizeof(value));
    form->closingValue = value;
    if (result == form->reopenOn) {
        form->reopenOn = CDMDPI_NONE;
        openLiveForm(*form);
    }
}

// Version 9: a form the plugin changes while it is open, with an extra button and pictures.
void testLiveForm()
{
    document::ProjectDocument document =
        document::ProjectDocument::createEmpty({"live", 2, false, 16});
    QMenu menu;
    app::EditorPlugins::Window window;
    window.menu = &menu;
    window.document = [&document]() { return &document; };
    window.editableDocument = [&document]() { return &document; };
    window.recordEdit = [](std::unique_ptr<edit::EditCommand>) { return true; };
    window.currentFrame = []() { return std::optional<std::size_t>(0); };
    window.selectedFrames = []() { return std::vector<std::size_t>{0}; };
    app::EditorPlugins plugins(std::move(window));
    const auto* api = static_cast<const ColorizingDMDEditorAPI*>(vpx::editorApiTable());

    std::vector<uint16_t> pixels(4 * 2, 0xf800);
    const ColorizingDMDFormImage pictures[] = {
        {pixels.data(), 4, 2, 3, "Start"},
        {pixels.data(), 4, 2, 0, nullptr},
    };
    const ColorizingDMDFormField fields[] = {
        {"transition", "Transition", CDMDPI_FIELD_CHOICE, "dissolve", "dissolve=Dissolve\nslide",
         nullptr, 0, nullptr, 0},
        {"option", "Direction", CDMDPI_FIELD_CHOICE, nullptr, "up\ndown", nullptr,
         CDMDPI_FORM_DISABLED, nullptr, 0},
        {"frames", "Number of frames", CDMDPI_FIELD_CHOICE, "3", "1\n2\n3", nullptr, 0, nullptr,
         0},
        {"preview", nullptr, CDMDPI_FIELD_IMAGE, nullptr, nullptr, nullptr, 0, pictures, 1},
        {"hint", "A hint", CDMDPI_FIELD_NOTE, nullptr, nullptr, nullptr, CDMDPI_FORM_HIDDEN,
         nullptr, 0},
    };
    LiveForm form;
    form.api = api;
    form.plugin = 7;
    form.fields = fields;
    form.fieldCount = 5;

    // Refused: unknown flags, two fields of one key, a picture too big, too many buttons, an
    // image in a form of version 4.
    ColorizingDMDFormField wrong[2] = {fields[0], fields[0]};
    expect(api->ShowLiveForm(7, "T", "Apply", nullptr, 0, wrong, 2, nullptr, &onFormClosed,
                             &form) == 0,
           "two fields of one key are refused");
    wrong[0].flags = 0x4;
    expect(api->ShowLiveForm(7, "T", "Apply", nullptr, 0, wrong, 1, nullptr, &onFormClosed,
                             &form) == 0,
           "an unknown flag is refused");
    const ColorizingDMDFormImage huge{pixels.data(), 2000, 2, 1, nullptr};
    wrong[0] = fields[3];
    wrong[0].images = &huge;
    expect(api->ShowLiveForm(7, "T", "Apply", nullptr, 0, wrong, 1, nullptr, &onFormClosed,
                             &form) == 0,
           "a picture out of range is refused");
    const char* const tooMany[] = {"1", "2", "3", "4", "5"};
    expect(api->ShowLiveForm(7, "T", "Apply", tooMany, 5, fields, 5, nullptr, &onFormClosed,
                             &form) == 0,
           "more than four extra buttons are refused");
    expect(api->ShowLiveForm(7, "T", "Apply", nullptr, 0, fields, 5, nullptr, nullptr, &form) ==
               0,
           "a form nobody hears closing is refused");
    const ColorizingDMDSettingField image{"preview", "Preview", CDMDPI_FIELD_IMAGE, nullptr,
                                          nullptr, nullptr};
    expect(api->ShowForm(7, "T", "Apply", &image, 1, &onSettings, nullptr) == 0,
           "ShowForm takes no image");

    const ColorizingDMDSettingField text{"name", "Name", CDMDPI_FIELD_TEXT, nullptr, nullptr,
                                         nullptr};
    expect(openLiveForm(form) == 1 && openLiveForm(form) == 0 &&
               api->ShowForm(7, "T", "Apply", &text, 1, &onSettings, nullptr) == 0,
           "the form opens, once, and no other form meanwhile");
    app::PluginSettingsDialog* dialog = plugins.settingsWindow("");
    expect(dialog && !dialog->isModal() && dialog->extraButton(0) &&
               dialog->extraButton(0)->text() == "Back" && !dialog->row("option")->isEnabled() &&
               dialog->row("hint")->isHidden() && !dialog->row("frames")->isHidden(),
           "beside the editor, with Back, a disabled and a hidden field");
    const QList<QLabel*> shown = dialog->row("preview")->findChildren<QLabel*>();
    expect(shown.size() == 2 && shown[0]->pixmap().size() == QSize(12, 6) &&
               shown[1]->text() == "Start",
           "a picture three times its size, with its caption");

    // The author chooses Slide: the plugin enables the direction and gives five frames.
    auto* transition = qobject_cast<QComboBox*>(dialog->input("transition"));
    auto* frames = qobject_cast<QComboBox*>(dialog->input("frames"));
    transition->setCurrentIndex(1);
    Q_EMIT transition->activated(1);
    expect(form.changed == std::vector<std::string>{"transition"} &&
               dialog->row("option")->isEnabled() && frames->count() == 5 &&
               frames->currentData().toString() == "3" &&
               dialog->helpLabel("frames")->text() == "At most 5" &&
               !dialog->helpLabel("frames")->isHidden(),
           "the plugin changes the form as the author fills it in; the value stays in the list");

    ColorizingDMDFormFieldUpdate update{};
    update.changes = CDMDPI_FORM_SET_CHOICES;
    update.choices = "1\n2";
    expect(api->SetFormField(7, "frames", &update) == 1 &&
               frames->currentData().toString() == "1" && form.changed.size() == 1,
           "a value no longer in the list becomes the first, without telling the plugin");
    update.changes = CDMDPI_FORM_SET_VALUE;
    update.value = "4";
    expect(api->SetFormField(7, "frames", &update) == 0, "a value not in the list is refused");
    update.value = "2";
    expect(api->SetFormField(7, "frames", &update) == 1 &&
               frames->currentData().toString() == "2",
           "one in the list is set");
    update.changes = CDMDPI_FORM_SET_VALUE;
    expect(api->SetFormField(7, "preview", &update) == 0 &&
               api->SetFormField(7, "missing", &update) == 0,
           "a picture has no value; a field that does not exist is refused");
    update.changes = 0x80;
    expect(api->SetFormField(7, "frames", &update) == 0, "an unknown change is refused");
    update = {};
    update.changes = CDMDPI_FORM_SET_IMAGES | CDMDPI_FORM_SET_LABEL;
    update.images = pictures;
    update.imageCount = 2;
    update.label = "Preview";
    expect(api->SetFormField(7, "preview", &update) == 1 &&
               dialog->row("preview")->findChildren<QLabel*>().size() == 3 &&
               dialog->rowLabel("preview")->text() == "Preview",
           "new pictures and a label");
    update = {};
    update.changes = CDMDPI_FORM_SET_VISIBLE | CDMDPI_FORM_SET_LABEL;
    update.visible = 1;
    update.label = "Another hint";
    expect(api->SetFormField(7, "hint", &update) == 1 && !dialog->row("hint")->isHidden() &&
               qobject_cast<QLabel*>(dialog->row("hint"))->text() == "Another hint",
           "a hidden note is shown with another text");

    char value[16] = {};
    expect(api->GetFormValue(7, "transition", nullptr, 0) == 6 &&
               api->GetFormValue(7, "frames", value, sizeof(value)) == 2 &&
               std::string(value) == "2" && api->GetFormValue(7, "preview", value, 16) == 0,
           "the values shown, not saved yet");
    expect(plugins.setting("", "transition").empty(), "nothing is kept before it closes");

    // Back closes the form, keeps the values, and the plugin opens the previous step.
    form.reopenOn = CDMDPI_FORM_BUTTON;
    dialog->extraButton(0)->click();
    expect(form.closed == std::vector<uint32_t>{CDMDPI_FORM_BUTTON} &&
               form.closingValue == "slide" && plugins.setting("", "transition") == "slide" &&
               plugins.settingsWindow("") && plugins.settingsWindow("") != dialog,
           "Back: the values are read and kept, and the next form opens from the callback");
    dialog = plugins.settingsWindow("");
    expect(api->GetFormValue(7, "transition", value, sizeof(value)) == 6 &&
               std::string(value) == "slide",
           "the new form shows the kept values");
    qobject_cast<QComboBox*>(dialog->input("frames"))->setCurrentIndex(0);
    dialog->reject();
    expect(form.closed.back() == CDMDPI_FORM_CANCELLED && plugins.setting("", "frames") == "2" &&
               api->GetFormValue(7, "frames", value, sizeof(value)) == 0,
           "Cancel keeps nothing; the form is gone");
    expect(openLiveForm(form) == 1 && api->CloseForm(7) == 1 && api->CloseForm(7) == 0 &&
               form.closed.size() == 2 && !plugins.settingsWindow(""),
           "the plugin closes its form without hearing of it");
    expect(openLiveForm(form) == 1, "a form for Apply");
    plugins.settingsWindow("")->accept();
    expect(form.closed.back() == CDMDPI_FORM_ACCEPTED, "Apply");
}

// A radio field's buttons by their value.
std::map<std::string, QRadioButton*> radioButtons(app::PluginSettingsDialog* dialog,
                                                  const std::string& key)
{
    std::map<std::string, QRadioButton*> buttons;
    for (QRadioButton* button : dialog->input(key)->findChildren<QRadioButton*>()) {
        buttons[button->property("choice").toString().toStdString()] = button;
    }
    return buttons;
}

// Version 10: a choice shown as radio buttons, in a settings form and in a live form.
void testRadioButtons()
{
    document::ProjectDocument document =
        document::ProjectDocument::createEmpty({"radio", 1, false, 16});
    QMenu menu;
    app::EditorPlugins::Window window;
    window.menu = &menu;
    window.document = [&document]() { return &document; };
    window.editableDocument = [&document]() { return &document; };
    window.recordEdit = [](std::unique_ptr<edit::EditCommand>) { return true; };
    window.currentFrame = []() { return std::optional<std::size_t>(0); };
    window.selectedFrames = []() { return std::vector<std::size_t>{0}; };
    app::EditorPlugins plugins(std::move(window));
    const auto* api = static_cast<const ColorizingDMDEditorAPI*>(vpx::editorApiTable());

    ColorizingDMDSettingField setting{"place", "Place", CDMDPI_FIELD_RADIO, "end",
                                      "after=After it\nend=At the end", nullptr};
    ColorizingDMDSettingField empty = setting;
    empty.choices = "";
    Answers answers;
    expect(api->ShowForm(7, "T", "Start", &empty, 1, &onSettings, &answers) == 0,
           "radio buttons without choices are refused");
    expect(api->ShowForm(7, "T", "Start", &setting, 1, &onSettings, &answers) == 1,
           "a form of version 4 takes radio buttons");
    app::PluginSettingsDialog* dialog = plugins.settingsWindow("");
    std::map<std::string, QRadioButton*> place = radioButtons(dialog, "place");
    expect(place.size() == 2 && place["after"]->text() == "After it" &&
               !place["after"]->isChecked() && place["end"]->isChecked() &&
               dialog->value("place") == "end",
           "a button per choice, the default checked");
    place["after"]->click();
    expect(dialog->value("place") == "after" && !place["end"]->isChecked(), "one at a time");
    dialog->accept();
    expect(answers.saved == 1 && plugins.setting("", "place") == "after",
           "the value is kept as a setting");
    expect(api->EditSettings(7, "T", &setting, 1, &onSettings, &answers) == 1 &&
               radioButtons(plugins.settingsWindow(""), "place")["after"]->isChecked(),
           "and shown the next time");
    plugins.settingsWindow("")->reject();

    const ColorizingDMDFormField fields[] = {
        {"effect", "Effect", CDMDPI_FIELD_RADIO, "dissolve",
         "dissolve=Dissolve\nfade=Fade\nslide=Slide\nsplit=Split\nwipe=Wipe", nullptr, 0,
         nullptr, 0},
    };
    LiveForm form;
    form.api = api;
    form.plugin = 7;
    form.fields = fields;
    form.fieldCount = 1;
    expect(openLiveForm(form) == 1, "a live form takes radio buttons");
    dialog = plugins.settingsWindow("");
    std::map<std::string, QRadioButton*> effect = radioButtons(dialog, "effect");
    expect(effect.size() == 5 && effect["dissolve"]->isChecked(),
           "five buttons, the default checked");
    const auto* grid = qobject_cast<QGridLayout*>(dialog->input("effect")->layout());
    const auto line = [grid](QWidget* button) {
        int row = -1, column = -1, rows = 0, columns = 0;
        grid->getItemPosition(grid->indexOf(button), &row, &column, &rows, &columns);
        return row;
    };
    expect(grid && line(effect["split"]) == 0 && line(effect["wipe"]) == 1,
           "four on a line, the fifth on the next");

    // The author clicks Slide: the plugin hears of it.
    effect["slide"]->click();
    char value[16] = {};
    expect(form.changed == std::vector<std::string>{"effect"} &&
               api->GetFormValue(7, "effect", value, sizeof(value)) == 6 &&
               std::string(value) == "slide",
           "a click tells the plugin");

    ColorizingDMDFormFieldUpdate update{};
    update.changes = CDMDPI_FORM_SET_VALUE;
    update.value = "none";
    expect(api->SetFormField(7, "effect", &update) == 0, "a value not in the list is refused");
    update.value = "fade";
    expect(api->SetFormField(7, "effect", &update) == 1 && effect["fade"]->isChecked() &&
               !effect["slide"]->isChecked() && form.changed.size() == 1,
           "the plugin sets one in the list, without hearing of it");
    update.changes = CDMDPI_FORM_SET_CHOICES;
    update.choices = "";
    expect(api->SetFormField(7, "effect", &update) == 0, "no choices are refused");
    update.choices = "fade=Fade\nwipe=Wipe";
    expect(api->SetFormField(7, "effect", &update) == 1, "other choices");
    effect = radioButtons(dialog, "effect");
    expect(effect.size() == 2 && effect["fade"]->isChecked(),
           "the buttons of the new choices; the value stays in the list");
    update.choices = "split=Split\nwipe=Wipe";
    expect(api->SetFormField(7, "effect", &update) == 1 &&
               dialog->value("effect") == "split" && form.changed.size() == 1,
           "a value no longer in the list becomes the first");
    radioButtons(dialog, "effect")["wipe"]->click();
    expect(form.changed.size() == 2 && dialog->value("effect") == "wipe",
           "the new buttons tell the plugin too");
    dialog->accept();
    expect(plugins.setting("", "effect") == "wipe", "kept when the form closes");
}

// Version 9: a scene made from frames, and the frame that starts it.
void testScenes()
{
    document::ProjectDocument document =
        document::ProjectDocument::createEmpty({"scenes", 3, false, 16});
    document.setColorizedPixel(2, render::Resolution::Sd, 5, 2, 0xf800);
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
        return true;
    };
    window.currentFrame = []() { return std::optional<std::size_t>(0); };
    window.selectedFrames = []() { return std::vector<std::size_t>{0}; };
    app::EditorPlugins plugins(std::move(window));
    const auto* api = static_cast<const ColorizingDMDEditorAPI*>(vpx::editorApiTable());
    const uint32_t plugin = 7;

    ColorizingDMDScene scene{};
    scene.sceneId = CDMDPI_NONE;
    scene.name = "Wipe";
    scene.durationMs = 40;
    scene.finish = CDMDPI_SCENE_BLACK;
    scene.flags = CDMDPI_SCENE_INTERRUPTABLE | CDMDPI_SCENE_BACK_AND_FORTH;
    const uint32_t from[] = {1, 2};
    uint32_t sceneId = 0;
    uint32_t first = 0;
    expect(api->CreateScene(plugin, &scene, from, 2, &sceneId, &first) == 0,
           "no scene without a batch");
    expect(api->BeginEdit(plugin, "Transition") == 1, "a batch");
    ColorizingDMDScene wrong = scene;
    wrong.flags = 0x100;
    expect(api->CreateScene(plugin, &wrong, from, 2, nullptr, nullptr) == 0,
           "an unknown flag is refused");
    wrong = scene;
    wrong.groups = 3;
    expect(api->CreateScene(plugin, &wrong, from, 2, nullptr, nullptr) == 0 &&
               document.frameCount() == 3,
           "frames that do not fill the groups are refused");
    expect(api->CreateScene(plugin, &scene, from, 2, &sceneId, &first) == 1 &&
               sceneId == document::kFirstSceneId && first == 3 && document.frameCount() == 5,
           "the scene gets the lowest free ID and its frames behind the last");
    const document::SceneDefinition* made =
        document.colorizationSettings() ? document.colorizationSettings()->scene(50000)
                                        : nullptr;
    expect(made && made->name == "Wipe" && made->frameCount == 2 && made->durationMs == 40 &&
               made->interruptable && !made->immediateStart &&
               made->options == (document::kSceneBlackWhenFinished |
                                 document::kSceneBackAndForth) &&
               document.colorizedPixel(4, render::Resolution::Sd, 5, 2) == 0xf800,
           "with the settings given, its frames made like the frames given");
    wrong = scene;
    wrong.sceneId = 50000;
    expect(api->CreateScene(plugin, &wrong, from, 2, nullptr, nullptr) == 0,
           "an ID taken is refused");

    expect(api->SetFrameScene(plugin, 0, sceneId) == 1 &&
               document.sceneTriggerId(0) == document::kFirstSceneId,
           "a frame starts the scene");
    ColorizingDMDFrameInfo info{};
    expect(api->GetFrameInfo(0, &info) == 1 && info.triggerId == sceneId,
           "as the frame's summary says");
    expect(api->SetFrameScene(plugin, 3, sceneId) == 0 &&
               api->SetFrameScene(plugin, 1, 50001) == 0 &&
               api->SetFrameScene(plugin, 9, sceneId) == 0,
           "not a frame of a scene, not a scene the project lacks, not a frame that is not there");
    expect(api->EndEdit(plugin, 1) == 1 && recorded == std::vector<std::string>{"Transition"},
           "one step");
    expect(histories.undo(edit::structureHistoryKey(), document) && document.frameCount() == 3 &&
               !document.colorizationSettings() &&
               document.sceneTriggerId(0) == document::kNoTriggerId,
           "one undo takes the scene, its frames and the trigger back");
    expect(api->BeginEdit(plugin, "Again") == 1 &&
               api->CreateScene(plugin, &scene, from, 2, &sceneId, nullptr) == 1 &&
               api->SetFrameScene(plugin, 0, sceneId) == 1 &&
               api->SetFrameScene(plugin, 0, CDMDPI_NONE) == 1 &&
               document.sceneTriggerId(0) == document::kNoTriggerId &&
               api->EndEdit(plugin, 1) == 1,
           "CDMDPI_NONE clears the trigger");
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
        return true;
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
    expect(api && api->version == 10, "the API table is version 10");

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
    testLiveForm();
    testRadioButtons();
    testScenes();
#endif
    QSettings().remove(QStringLiteral("plugins"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
