#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::vpx {

// What the editor answers plugins through its editor API (plugin-api/ColorizingDMDPlugin.h),
// which the plugin host serves on the plugin bus. The editor implements it (EditorPlugins);
// everything is called on the UI thread.
class EditorBackend {
public:
    virtual ~EditorBackend() = default;

    // The open project; nullptr without one.
    [[nodiscard]] virtual const document::ProjectDocument* document() const = 0;
    [[nodiscard]] virtual std::optional<std::size_t> currentFrame() const = 0;
    // Ascending; the current frame when no other is selected.
    [[nodiscard]] virtual std::vector<std::size_t> selectedFrames() const = 0;
    // Makes the frame current and selects the frames (empty: the current one only).
    virtual void showFrames(std::size_t current, const std::vector<std::size_t>& selected) = 0;

    // The plugin's entries in the Plugins menu.
    virtual void addMenuEntry(const std::string& pluginId,
                              const std::string& text,
                              std::function<void()> action) = 0;
    virtual void removeMenuEntries(const std::string& pluginId) = 0;
    virtual void showMessage(const std::string& pluginId,
                             const std::string& title,
                             const std::string& text) = 0;
    virtual void showStatus(const std::string& pluginId, const std::string& text) = 0;
    // The plugin's own settings; empty when not set.
    [[nodiscard]] virtual std::string setting(const std::string& pluginId,
                                              const std::string& key) const = 0;
    virtual void setSetting(const std::string& pluginId,
                            const std::string& key,
                            const std::string& value) = 0;
};

// The backend the API answers with from now on; nullptr: no project, no menu. Not owned.
void setEditorBackend(EditorBackend* backend);

// Broadcast to the plugins (CDMDPI_EVT_ON_PROJECT_CHANGED, CDMDPI_EVT_ON_SELECTION_CHANGED);
// nothing while the plugin bus is not open.
void notifyProjectChanged();
void notifySelectionChanged();

// The API's function table (a const ColorizingDMDEditorAPI*), as a plugin gets it; nullptr in
// a build without plugin hosting. For tests, which call it as a plugin would.
[[nodiscard]] const void* editorApiTable();

}  // namespace colorizing::vpx
