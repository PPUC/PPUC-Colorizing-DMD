// The editor API for plugins (plugin-api/ColorizingDMDPlugin.h), called as a plugin does,
// answered by EditorPlugins over a project; and EditorPlugins' menu and notifications.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>

#include "EditorPlugins.h"
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
    expect(api && api->version == 2, "the API table is version 2");

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
    expect(api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32) == 0,
           "no change without a batch");
    expect(api->BeginEdit(plugin, "Strip") == 1 && api->BeginEdit(plugin, "Again") == 0,
           "one batch at a time");
    expect(api->ResetFramePixels(plugin + 1, 1, keep.data(), 128 * 32) == 0,
           "only for the plugin that began it");
    expect(api->ResetFramePixels(plugin, 1, keep.data(), 5) == 0,
           "a keep mask of the wrong size is refused");
    std::vector<uint16_t> plain(16, 1);
    expect(api->GetPlainColors(plain.data(), 16) == 16, "the plain colors of 16 shades");
    expect(api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32) == 1 &&
               document.colorizedPixel(1, render::Resolution::Sd, 5, 2) ==
                   plain[document.originalFrame(1).pixels[2 * 128 + 5]],
           "the frame gets the original's plain look at once");
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
               api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32) == 1 &&
               api->EndEdit(plugin, 0) == 1 &&
               document.colorizedPixel(1, render::Resolution::Sd, 5, 2) == 0xf800 &&
               recorded.size() == 1,
           "a batch ended without applying takes its changes back");
    expect(api->BeginEdit(plugin, "Left open") == 1 &&
               api->ResetFramePixels(plugin, 1, keep.data(), 128 * 32) == 1,
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
    QSettings().remove(QStringLiteral("plugins"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
