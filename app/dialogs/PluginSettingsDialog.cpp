#include "dialogs/PluginSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <utility>

namespace colorizing::app {

PluginSettingsDialog::PluginSettingsDialog(const QString& title,
                                           const QString& acceptLabel,
                                           std::vector<vpx::SettingField> fields,
                                           const std::map<std::string, std::string>& values,
                                           QWidget* parent)
    : QDialog(parent)
    , m_fields(std::move(fields))
{
    setWindowTitle(title.isEmpty() ? tr("Plugin settings") : title);
    auto* form = new QFormLayout;
    for (const vpx::SettingField& field : m_fields) {
        if (field.kind == vpx::SettingField::Kind::Note) {
            auto* note = new QLabel(QString::fromStdString(field.label), this);
            note->setWordWrap(true);
            note->setTextInteractionFlags(Qt::TextSelectableByMouse);
            note->setMinimumWidth(420);
            form->addRow(note);
            m_inputs.push_back(nullptr);
            continue;
        }
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
        case vpx::SettingField::Kind::Consent: {
            auto* box = new QCheckBox(label, this);
            box->setChecked(false);
            connect(box, &QCheckBox::toggled, this, &PluginSettingsDialog::updateAccept);
            m_consents.push_back(box);
            input = box;
            form->addRow(QString(), box);
            break;
        }
        case vpx::SettingField::Kind::File: {
            auto* edit = new QLineEdit(QString::fromStdString(value), this);
            edit->setMinimumWidth(320);
            auto* choose = new QPushButton(tr("Choose..."), this);
            QStringList filters;
            for (const auto& [filter, text] : field.choices) {
                filters << QString::fromStdString(filter);
            }
            connect(choose, &QPushButton::clicked, this, [this, edit, label, filters]() {
                const QString path = QFileDialog::getOpenFileName(
                    this, label, QFileInfo(edit->text()).absolutePath(),
                    filters.join(QStringLiteral(";;")));
                if (!path.isEmpty()) {
                    edit->setText(path);
                }
            });
            auto* row = new QHBoxLayout;
            row->addWidget(edit, 1);
            row->addWidget(choose);
            input = edit;
            form->addRow(label, row);
            break;
        }
        case vpx::SettingField::Kind::Note:
            break;
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
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    if (acceptLabel.isEmpty()) {
        m_accept = buttons->addButton(QDialogButtonBox::Save);
    } else {
        m_accept = buttons->addButton(acceptLabel, QDialogButtonBox::AcceptRole);
    }
    m_accept->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    updateAccept();
}

bool PluginSettingsDialog::canAccept() const
{
    for (const QCheckBox* box : m_consents) {
        if (!box->isChecked()) {
            return false;
        }
    }
    return true;
}

void PluginSettingsDialog::accept()
{
    if (canAccept()) {
        QDialog::accept();
    }
}

void PluginSettingsDialog::updateAccept()
{
    if (m_accept) {
        m_accept->setEnabled(canAccept());
    }
}

std::map<std::string, std::string> PluginSettingsDialog::values() const
{
    std::map<std::string, std::string> result;
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        QWidget* input = m_inputs[index];
        if (!input) {
            continue;
        }
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
        if (m_inputs[index] && m_fields[index].key == key) {
            return m_inputs[index];
        }
    }
    return nullptr;
}

}  // namespace colorizing::app
