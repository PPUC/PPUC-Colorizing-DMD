#pragma once

#include <cstddef>
#include <cstdint>
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

// A picture of a form's image field (ShowLiveForm).
struct FormImage {
    uint32_t width = 0;
    uint32_t height = 0;
    // Each pixel drawn as a square of this size.
    uint32_t scale = 1;
    // RGB565, row-major.
    std::vector<uint16_t> pixels;
    std::string caption;
};

// A field of a plugin's settings form (EditSettings, ShowForm, ShowLiveForm).
struct SettingField {
    enum class Kind {
        Text,
        Password,
        Checkbox,
        Choice,
        // A path, with a button that chooses the file; choices holds the name filters.
        File,
        // A paragraph (the label), without a value.
        Note,
        // A checkbox shown unchecked, which has to be checked to accept the form.
        Consent,
        // A row of pictures, without a value (live forms only).
        Image,
        // A choice shown as radio buttons.
        Radio,
    };
    std::string key;
    std::string label;
    Kind kind = Kind::Text;
    std::string defaultValue;
    // Choice, Radio: the values and their labels; File: the name filters, each as value and label.
    std::vector<std::pair<std::string, std::string>> choices;
    std::string help;
    // How it starts in a live form.
    bool enabled = true;
    bool visible = true;
    // Image: the pictures, left to right.
    std::vector<FormImage> images;
};

// A change of a field of a live form (SetFormField): what is set.
struct FormFieldUpdate {
    std::optional<bool> enabled;
    std::optional<bool> visible;
    std::optional<std::string> value;
    std::optional<std::string> label;
    std::optional<std::vector<std::pair<std::string, std::string>>> choices;
    std::optional<std::string> help;
    std::optional<std::vector<FormImage>> images;
};

// How the author closed a live form: cancelled, accepted, or kFormButton plus the index of
// the extra button.
constexpr int kFormCancelled = 0;
constexpr int kFormAccepted = 1;
constexpr int kFormButton = 2;

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
    // Records an applied command as one undoable step and refreshes the views. False when the
    // editor refused it and took it back (a partner's claim in a team project).
    virtual bool recordEdit(std::unique_ptr<edit::EditCommand> command) = 0;
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
    // acceptLabel: the accept button's text; empty for Save.
    virtual bool editSettings(const std::string& pluginId,
                              const std::string& title,
                              const std::string& acceptLabel,
                              std::vector<SettingField> fields,
                              std::function<void(bool saved)> done) = 0;
    // A form that does not block the editor and that the plugin changes while it is open, with
    // extra buttons that close it as the accept button does. changed runs when the author
    // changes a field's value; closed when the author closes the form (kForm...), the values
    // kept as settings unless cancelled. False when the plugin has a form open.
    virtual bool showLiveForm(const std::string& pluginId,
                              const std::string& title,
                              const std::string& acceptLabel,
                              std::vector<std::string> extraButtons,
                              std::vector<SettingField> fields,
                              std::function<void(const std::string& key)> changed,
                              std::function<void(int result)> closed) = 0;
    // The value the field of the plugin's live form shows; nothing without the form or a field
    // with a value.
    [[nodiscard]] virtual std::optional<std::string> formValue(const std::string& pluginId,
                                                               const std::string& key) const = 0;
    // False, changing nothing, without the form or the field, or for a change it cannot take.
    virtual bool updateFormField(const std::string& pluginId,
                                 const std::string& key,
                                 const FormFieldUpdate& update) = 0;
    // Closes the live form without telling the plugin; false without one.
    virtual bool closeForm(const std::string& pluginId) = 0;

    // The language the editor's menus are shown in ("en", "de", "fr"); empty when not known.
    [[nodiscard]] virtual std::string language() const = 0;
    // Whether the editor shows its dark theme.
    [[nodiscard]] virtual bool darkTheme() const = 0;

    // The editor's own menu actions a plugin may run: those of the published list
    // (EditorPlugins::pluginEditorActionIds()), by the ids of the keyboard shortcut settings.
    // Runs one as if the author chose it; false for another id or one not enabled now.
    virtual bool runEditorAction(const std::string& id) = 0;
    [[nodiscard]] virtual bool editorActionEnabled(const std::string& id) const = 0;
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
