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
    QMenu menu;

    app::EditorPlugins::Window window;
    window.menu = &menu;
    window.document = [&document]() { return &document; };
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
    expect(api && api->version == 1, "the API table is version 1");

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
