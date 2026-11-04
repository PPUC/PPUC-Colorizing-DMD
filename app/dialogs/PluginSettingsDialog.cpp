#include "dialogs/PluginSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <utility>

namespace colorizing::app {

PluginSettingsDialog::PluginSettingsDialog(const QString& title,
                                           std::vector<vpx::SettingField> fields,
                                           const std::map<std::string, std::string>& values,
                                           QWidget* parent)
    : QDialog(parent)
    , m_fields(std::move(fields))
{
    setWindowTitle(title.isEmpty() ? tr("Plugin settings") : title);
    auto* form = new QFormLayout;
    for (const vpx::SettingField& field : m_fields) {
        const auto stored = values.find(field.key);
        const std::string& value =
            stored != values.end() && !stored->second.empty() ? stored->second
                                                               : field.defaultValue;
        const QString label = QString::fromStdString(field.label);
        QWidget* input = nullptr;
        switch (field.kind) {
        case vpx::SettingField::Kind::Text:
        case vpx::SettingField::Kind::Password: {
            auto* edit = new QLineEdit(QString::fromStdString(value), this);
            if (field.kind == vpx::SettingField::Kind::Password) {
                edit->setEchoMode(QLineEdit::Password);
            }
            edit->setMinimumWidth(320);
            input = edit;
            form->addRow(label, edit);
            break;
        }
        case vpx::SettingField::Kind::Checkbox: {
            auto* box = new QCheckBox(label, this);
            box->setChecked(value == "1");
            input = box;
            form->addRow(QString(), box);
            break;
        }
        case vpx::SettingField::Kind::Choice: {
            auto* combo = new QComboBox(this);
            for (const auto& [choice, text] : field.choices) {
                combo->addItem(QString::fromStdString(text), QString::fromStdString(choice));
            }
            const int index = combo->findData(QString::fromStdString(value));
            combo->setCurrentIndex(index < 0 ? 0 : index);
            input = combo;
            form->addRow(label, combo);
            break;
        }
        }
        m_inputs.push_back(input);
        if (!field.help.empty()) {
            auto* help = new QLabel(QString::fromStdString(field.help), this);
            help->setWordWrap(true);
            QFont font = help->font();
            font.setPointSizeF(font.pointSizeF() * 0.9);
            help->setFont(font);
            form->addRow(QString(), help);
        }
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

std::map<std::string, std::string> PluginSettingsDialog::values() const
{
    std::map<std::string, std::string> result;
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        QWidget* input = m_inputs[index];
        std::string value;
        if (const auto* edit = qobject_cast<QLineEdit*>(input)) {
            value = edit->text().toStdString();
        } else if (const auto* box = qobject_cast<QCheckBox*>(input)) {
            value = box->isChecked() ? "1" : "0";
        } else if (const auto* combo = qobject_cast<QComboBox*>(input)) {
            value = combo->currentData().toString().toStdString();
        }
        result[m_fields[index].key] = value;
    }
    return result;
}

QWidget* PluginSettingsDialog::input(const std::string& key) const
{
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        if (m_fields[index].key == key) {
            return m_inputs[index];
        }
    }
    return nullptr;
}

}  // namespace colorizing::app
