#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <map>
#include <string>
#include <vector>

#include "vpx/EditorApi.h"

class QWidget;

namespace colorizing::app {

// A plugin's settings form (the editor API's EditSettings): one row per field, showing the
// plugin's value of the key, or the field's default while it has none.
class PluginSettingsDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(PluginSettingsDialog)

public:
    // values: the plugin's settings by key (empty: not set).
    PluginSettingsDialog(const QString& title,
                         std::vector<vpx::SettingField> fields,
                         const std::map<std::string, std::string>& values,
                         QWidget* parent = nullptr);

    // The form's values by key: a checkbox "1" or "0", a choice its value.
    [[nodiscard]] std::map<std::string, std::string> values() const;
    // For tests: the field's input (QLineEdit, QCheckBox or QComboBox).
    [[nodiscard]] QWidget* input(const std::string& key) const;

private:
    std::vector<vpx::SettingField> m_fields;
    std::vector<QWidget*> m_inputs;
};

}  // namespace colorizing::app
