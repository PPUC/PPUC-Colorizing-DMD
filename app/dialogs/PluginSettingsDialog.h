#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vpx/EditorApi.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QWidget;

namespace colorizing::app {

// A plugin's settings form (the editor API's EditSettings, ShowForm and ShowLiveForm): one row
// per field, showing the plugin's value of the key, or the field's default while it has none. A
// consent box shows unchecked, and the form is accepted only while every consent box is
// checked. A live form's plugin changes its fields while it is open (update()) and hears of
// the author's changes (setChangedAction()); its extra buttons close it with
// vpx::kFormButton plus their index as the result.
class PluginSettingsDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(PluginSettingsDialog)

public:
    // values: the plugin's settings by key (empty: not set). acceptLabel: the accept button's
    // text; empty for Save.
    PluginSettingsDialog(const QString& title,
                         const QString& acceptLabel,
                         std::vector<vpx::SettingField> fields,
                         const std::map<std::string, std::string>& values,
                         QWidget* parent = nullptr,
                         const std::vector<std::string>& extraButtons = {});

    // The form's values by key: a checkbox or consent "1" or "0", a choice its value; notes
    // and images have none.
    [[nodiscard]] std::map<std::string, std::string> values() const;
    // The value of one field; nothing for no such field with a value.
    [[nodiscard]] std::optional<std::string> value(const std::string& key) const;
    // For tests: the field's input (QLineEdit, QCheckBox or QComboBox; a file field's
    // QLineEdit).
    [[nodiscard]] QWidget* input(const std::string& key) const;
    // For tests: the field's row (the input, a file field's row with its button, a note's text,
    // an image field's pictures), its label and its help line.
    [[nodiscard]] QWidget* row(const std::string& key) const;
    [[nodiscard]] QLabel* rowLabel(const std::string& key) const;
    [[nodiscard]] QLabel* helpLabel(const std::string& key) const;
    // Whether every consent box is checked.
    [[nodiscard]] bool canAccept() const;
    // For tests: the accept button and the extra buttons.
    [[nodiscard]] QPushButton* acceptButton() const { return m_accept; }
    [[nodiscard]] QPushButton* extraButton(std::size_t index) const;

    // Runs with the field's key when the author changes its value (not when update() does).
    void setChangedAction(std::function<void(const std::string& key)> changed);
    // Changes the field; false, changing nothing, for no such field or a change it cannot
    // take (see SetFormField in plugin-api/ColorizingDMDPlugin.h).
    bool update(const std::string& key, const vpx::FormFieldUpdate& update);

    void accept() override;

private:
    struct Row {
        // The label beside the field; for a checkbox the box itself has the text.
        QLabel* label = nullptr;
        // What is shown and enabled with the field.
        QWidget* field = nullptr;
        // Where the value is: nullptr for a note and an image.
        QWidget* input = nullptr;
        QLabel* help = nullptr;
    };

    void updateAccept();
    void changed(const std::string& key);
    [[nodiscard]] std::optional<std::size_t> indexOf(const std::string& key) const;
    void showRow(std::size_t index);
    void fillImages(std::size_t index);

    std::vector<vpx::SettingField> m_fields;
    std::vector<Row> m_rows;
    std::vector<QCheckBox*> m_consents;
    std::vector<QPushButton*> m_extraButtons;
    QPushButton* m_accept = nullptr;
    std::function<void(const std::string&)> m_changed;
};

}  // namespace colorizing::app
