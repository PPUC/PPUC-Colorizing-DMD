#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "edit/EditBatch.h"

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::edit {
class EditCommand;
}

namespace colorizing::vpx {

// A picture a plugin proposes (ReviewPictures), checked against the project.
struct ProposedPicture {
    edit::PictureEdit picture;
    // The plugin's name for it; empty for the editor's own.
    std::string label;
};

// A field of a plugin's settings form (EditSettings).
struct SettingField {
    enum class Kind {
        Text,
        Password,
        Checkbox,
        Choice,
    };
    std::string key;
    std::string label;
    Kind kind = Kind::Text;
    std::string defaultValue;
    // Choice: the values and their labels.
    std::vector<std::pair<std::string, std::string>> choices;
    std::string help;
};

// What the editor answers plugins through its editor API (plugin-api/ColorizingDMDPlugin.h),
// which the plugin host serves on the plugin bus. The editor implements it (EditorPlugins);
// everything is called on the UI thread.
class EditorBackend {
public:
    virtual ~EditorBackend() = default;

    // The open project; nullptr without one.
    [[nodiscard]] virtual const document::ProjectDocument* document() const = 0;
    // The same, for the editing part of the API, which changes it only through edit commands
    // and hands each finished batch to recordEdit().
    [[nodiscard]] virtual document::ProjectDocument* editableDocument() = 0;
    // Records an applied command as one undoable step and refreshes the views.
    virtual void recordEdit(std::unique_ptr<edit::EditCommand> command) = 0;
    [[nodiscard]] virtual std::optional<std::size_t> currentFrame() const = 0;
    // Ascending; the current frame when no other is selected.
    [[nodiscard]] virtual std::vector<std::size_t> selectedFrames() const = 0;
    // Makes the frame current and selects the frames (empty: the current one only).
    virtual void showFrames(std::size_t current, const std::vector<std::size_t>& selected) = 0;
    // What the background and sprite panels show.
    [[nodiscard]] virtual std::optional<std::size_t> currentBackground() const = 0;
    [[nodiscard]] virtual std::optional<std::size_t> currentSprite() const = 0;

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
    // The plugin's windows: each returns false when the plugin has one open. done runs when
    // the author closes it, with the pictures applied or whether the form was saved; not when
    // the plugin unloads first.
    virtual bool reviewPictures(const std::string& pluginId,
                                const std::string& title,
                                const std::string& undoName,
                                std::vector<ProposedPicture> pictures,
                                std::function<void(std::size_t applied)> done) = 0;
    virtual bool editSettings(const std::string& pluginId,
                              const std::string& title,
                              std::vector<SettingField> fields,
                              std::function<void(bool saved)> done) = 0;
};

// The backend the API answers with from now on; nullptr: no project, no menu. Not owned.
void setEditorBackend(EditorBackend* backend);

// Records an edit batch a plugin left open (see BeginEdit); called after the plugin bus ran
// the plugins' messages.
void finishOpenEditBatch();

// Broadcast to the plugins (CDMDPI_EVT_ON_PROJECT_CHANGED, CDMDPI_EVT_ON_SELECTION_CHANGED);
// nothing while the plugin bus is not open.
void notifyProjectChanged();
void notifySelectionChanged();

// The API's function table (a const ColorizingDMDEditorAPI*), as a plugin gets it; nullptr in
// a build without plugin hosting. For tests, which call it as a plugin would.
[[nodiscard]] const void* editorApiTable();

}  // namespace colorizing::vpx
