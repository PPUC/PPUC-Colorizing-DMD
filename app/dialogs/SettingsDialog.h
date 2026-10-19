#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QStringList>

#include <vector>

#include "Appearance.h"
#include "DmdOutput.h"

class QComboBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QTabWidget;

namespace colorizing::app {

class DmdOutputSettingsPage;

// The editor's settings, one page per topic: Appearance (the theme, the language, how the canvas
// draws DMD pixels) and DMD Output. It only shows and collects them; the caller stores and applies what
// it returns.
class SettingsDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(SettingsDialog)

public:
    enum class Page {
        Appearance,
        DmdOutput,
        Plugins,
    };

    SettingsDialog(Theme theme,
                   const DmdRenderOptions& canvas,
                   const DmdOutputSettings& dmdOutput,
                   bool dmdOutputAvailable,
                   QWidget* parent = nullptr);

    void showPage(Page page);

    [[nodiscard]] Theme theme() const;
    // As Translations.h has it: empty to follow the system.
    void setLanguage(const QString& language);
    [[nodiscard]] QString language() const;
    [[nodiscard]] DmdRenderOptions canvasOptions() const;
    [[nodiscard]] DmdOutputSettings dmdOutputSettings() const;

    // The editor's plugins that can be chosen, and those chosen (EditorPlugins).
    struct PluginChoice {
        QString id;
        QString name;
        QString description;
        QString version;
        // Listed checked, and cannot be unchecked: the editor loads it itself (builtInUse:
        // for what).
        bool builtIn = false;
        QString builtInUse;
    };
    void setPlugins(const std::vector<PluginChoice>& plugins, const QStringList& enabled);
    [[nodiscard]] QStringList enabledPlugins() const;

private:
    QTabWidget* m_tabs = nullptr;
    QComboBox* m_themeComboBox = nullptr;
    QComboBox* m_languageComboBox = nullptr;
    QComboBox* m_pixelShapeComboBox = nullptr;
    QSpinBox* m_pixelSizeSpinBox = nullptr;
    DmdOutputSettingsPage* m_dmdOutputPage = nullptr;
    QListWidget* m_pluginList = nullptr;
    QLabel* m_pluginNote = nullptr;
};

}  // namespace colorizing::app
