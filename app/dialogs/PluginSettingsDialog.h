#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <map>
#include <string>
#include <vector>

#include "vpx/EditorApi.h"

class QCheckBox;
class QPushButton;
class QWidget;

namespace colorizing::app {

// A plugin's settings form (the editor API's EditSettings and ShowForm): one row per field,
// showing the plugin's value of the key, or the field's default while it has none. A consent
// box shows unchecked, and the form is accepted only while every consent box is checked.
class PluginSettingsDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(PluginSettingsDialog)

public:
    // values: the plugin's settings by key (empty: not set). acceptLabel: the accept button's
    // text; empty for Save.
    PluginSettingsDialog(const QString& title,
                         const QString& acceptLabel,
                         std::vector<vpx::SettingField> fields,
                         const std::map<std::string, std::string>& values,
                         QWidget* parent = nullptr);

    // The form's values by key: a checkbox or consent "1" or "0", a choice its value; notes
    // have none.
    [[nodiscard]] std::map<std::string, std::string> values() const;
    // For tests: the field's input (QLineEdit, QCheckBox or QComboBox; a file field's
    // QLineEdit).
    [[nodiscard]] QWidget* input(const std::string& key) const;
    // Whether every consent box is checked.
    [[nodiscard]] bool canAccept() const;
    // For tests: the accept button.
    [[nodiscard]] QPushButton* acceptButton() const { return m_accept; }

    void accept() override;

private:
    void updateAccept();


    std::vector<vpx::SettingField> m_fields;
    std::vector<QWidget*> m_inputs;
    std::vector<QCheckBox*> m_consents;
    QPushButton* m_accept = nullptr;
};

}  // namespace colorizing::app
