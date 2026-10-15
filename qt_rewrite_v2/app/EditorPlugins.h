#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "vpx/EditorApi.h"

class QAction;
class QMenu;
class QWidget;

namespace colorizing::app {

// The editor's plugins: VPX plugins in the plugin folder that use the editor API
// (plugin-api/ColorizingDMDPlugin.h), chosen under Settings -> Plugins and loaded at the
// editor's start. DMDUtil is not one of them: the DMD output loads it.
//
// It answers the editor API (vpx::EditorBackend) with what the window gives it, runs the
// plugin bus while a plugin is loaded, and tells the plugins when the project or the selection
// changed.
class EditorPlugins final : public vpx::EditorBackend {
    Q_DECLARE_TR_FUNCTIONS(EditorPlugins)

public:
    // What the window provides.
    struct Window {
        QWidget* parent = nullptr;
        // The Plugins menu, shown while it has entries.
        QMenu* menu = nullptr;
        std::function<const document::ProjectDocument*()> document;
        std::function<std::optional<std::size_t>()> currentFrame;
        std::function<std::vector<std::size_t>()> selectedFrames;
        std::function<void(std::size_t current, const std::vector<std::size_t>& selected)>
            showFrames;
        std::function<void(const QString& text)> showStatus;
    };

    explicit EditorPlugins(Window window);
    ~EditorPlugins() override;
    EditorPlugins(const EditorPlugins&) = delete;
    EditorPlugins& operator=(const EditorPlugins&) = delete;

    struct Plugin {
        QString id;
        QString name;
        QString description;
        QString version;
    };
    // The plugins in the plugin folder that can be chosen (not DMDUtil).
    [[nodiscard]] static std::vector<Plugin> available();
    // The plugins chosen to load at start, kept in the settings.
    [[nodiscard]] static QStringList enabledInSettings();

    // Loads the plugins chosen in the settings. Returns the messages of those that did not load.
    QStringList loadEnabled();
    // Chooses the plugins: loads the new ones, unloads the others, keeps the choice. Returns
    // the messages of those that did not load.
    QStringList setEnabled(const QStringList& ids);
    [[nodiscard]] QStringList loaded() const;

    // vpx::EditorBackend
    [[nodiscard]] const document::ProjectDocument* document() const override;
    [[nodiscard]] std::optional<std::size_t> currentFrame() const override;
    [[nodiscard]] std::vector<std::size_t> selectedFrames() const override;
    void showFrames(std::size_t current, const std::vector<std::size_t>& selected) override;
    void addMenuEntry(const std::string& pluginId,
                      const std::string& text,
                      std::function<void()> action) override;
    void removeMenuEntries(const std::string& pluginId) override;
    void showMessage(const std::string& pluginId,
                     const std::string& title,
                     const std::string& text) override;
    void showStatus(const std::string& pluginId, const std::string& text) override;
    [[nodiscard]] std::string setting(const std::string& pluginId,
                                      const std::string& key) const override;
    void setSetting(const std::string& pluginId,
                    const std::string& key,
                    const std::string& value) override;

    // Runs the plugin bus once and tells the plugins about changes (the timer does this while
    // a plugin is loaded; for tests).
    void tick();

private:
    bool load(const QString& id, QStringList& errors);
    void unload(const QString& id);
    void updateMenu();

    Window m_window;
    QStringList m_loaded;
    QTimer m_timer;
    std::map<std::string, std::vector<QAction*>> m_menuEntries;
    // What the plugins were last told about.
    const document::ProjectDocument* m_toldDocument = nullptr;
    uint64_t m_toldRevision = 0;
    std::optional<std::size_t> m_toldCurrent;
    std::vector<std::size_t> m_toldSelection;
};

}  // namespace colorizing::app
