#include "dialogs/PluginSettingsDialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "edit/ColorModel.h"

namespace colorizing::app {

namespace {

using Kind = vpx::SettingField::Kind;

// A radio field's buttons per line before it wraps.
constexpr int kRadiosPerLine = 4;

QImage formImage(const vpx::FormImage& picture)
{
    const int width = static_cast<int>(picture.width);
    const int height = static_cast<int>(picture.height);
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const edit::Rgb888 color = edit::rgb565ToRgb888(
                picture.pixels[static_cast<std::size_t>(y) * picture.width +
                               static_cast<std::size_t>(x)]);
            line[x] = qRgb(color.red, color.green, color.blue);
        }
    }
    // Unsmoothed, so that pixel art stays sharp.
    const int scale = static_cast<int>(std::max<uint32_t>(1, picture.scale));
    return image.scaled(width * scale, height * scale, Qt::IgnoreAspectRatio,
                        Qt::FastTransformation);
}

void fillChoices(QComboBox* combo,
                 const std::vector<std::pair<std::string, std::string>>& choices,
                 const std::string& value)
{
    combo->clear();
    for (const auto& [choice, text] : choices) {
        combo->addItem(QString::fromStdString(text), QString::fromStdString(choice));
    }
    const int index = combo->findData(QString::fromStdString(value));
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

bool hasChoice(const std::vector<std::pair<std::string, std::string>>& choices,
               const std::string& value)
{
    return std::any_of(choices.begin(), choices.end(),
                       [&value](const auto& choice) { return choice.first == value; });
}

// The value of the checked radio button.
std::string radioValue(const QButtonGroup* radios)
{
    const QAbstractButton* checked = radios->checkedButton();
    return checked ? checked->property("choice").toString().toStdString() : std::string();
}

}  // namespace

PluginSettingsDialog::PluginSettingsDialog(const QString& title,
                                           const QString& acceptLabel,
                                           std::vector<vpx::SettingField> fields,
                                           const std::map<std::string, std::string>& values,
                                           QWidget* parent,
                                           const std::vector<std::string>& extraButtons)
    : QDialog(parent)
    , m_fields(std::move(fields))
{
    setWindowTitle(title.isEmpty() ? tr("Plugin settings") : title);
    auto* form = new QFormLayout;
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        const vpx::SettingField& field = m_fields[index];
        Row row;
        const QString label = QString::fromStdString(field.label);
        if (field.kind == Kind::Note) {
            auto* note = new QLabel(label, this);
            note->setWordWrap(true);
            note->setTextInteractionFlags(Qt::TextSelectableByMouse);
            note->setMinimumWidth(420);
            form->addRow(note);
            row.field = note;
        } else {
            const auto stored = values.find(field.key);
            const std::string& value =
                stored != values.end() && !stored->second.empty() ? stored->second
                                                                   : field.defaultValue;
            const std::string key = field.key;
            row.label = new QLabel(this);
            switch (field.kind) {
            case Kind::Text:
            case Kind::Password: {
                auto* edit = new QLineEdit(QString::fromStdString(value), this);
                if (field.kind == Kind::Password) {
                    edit->setEchoMode(QLineEdit::Password);
                }
                edit->setMinimumWidth(320);
                connect(edit, &QLineEdit::textEdited, this, [this, key]() { changed(key); });
                row.label->setText(label);
                row.input = row.field = edit;
                break;
            }
            case Kind::Checkbox:
            case Kind::Consent: {
                auto* box = new QCheckBox(label, this);
                // A consent box shows unchecked every time.
                box->setChecked(field.kind == Kind::Checkbox && value == "1");
                if (field.kind == Kind::Consent) {
                    connect(box, &QCheckBox::toggled, this, &PluginSettingsDialog::updateAccept);
                    m_consents.push_back(box);
                }
                connect(box, &QCheckBox::clicked, this, [this, key]() { changed(key); });
                row.input = row.field = box;
                break;
            }
            case Kind::File: {
                auto* container = new QWidget(this);
                auto* edit = new QLineEdit(QString::fromStdString(value), container);
                edit->setMinimumWidth(320);
                auto* choose = new QPushButton(tr("Choose..."), container);
                connect(edit, &QLineEdit::textEdited, this, [this, key]() { changed(key); });
                connect(choose, &QPushButton::clicked, this, [this, index, edit, key]() {
                    QStringList filters;
                    for (const auto& [filter, text] : m_fields[index].choices) {
                        filters << QString::fromStdString(filter);
                    }
                    const QString path = QFileDialog::getOpenFileName(
                        this, m_rows[index].label->text(),
                        QFileInfo(edit->text()).absolutePath(),
                        filters.join(QStringLiteral(";;")));
                    if (!path.isEmpty()) {
                        edit->setText(path);
                        changed(key);
                    }
                });
                auto* layout = new QHBoxLayout(container);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->addWidget(edit, 1);
                layout->addWidget(choose);
                row.label->setText(label);
                row.field = container;
                row.input = edit;
                break;
            }
            case Kind::Choice: {
                auto* combo = new QComboBox(this);
                fillChoices(combo, field.choices, value);
                connect(combo, &QComboBox::activated, this, [this, key]() { changed(key); });
                row.label->setText(label);
                row.input = row.field = combo;
                break;
            }
            case Kind::Radio: {
                row.label->setText(label);
                row.input = row.field = new QWidget(this);
                auto* layout = new QGridLayout(row.input);
                layout->setContentsMargins(0, 0, 0, 0);
                row.radios = new QButtonGroup(row.input);
                // Clicked by the author only, not when the plugin sets the value.
                connect(row.radios, &QButtonGroup::buttonClicked, this,
                        [this, key]() { changed(key); });
                break;
            }
            case Kind::Image: {
                row.label->setText(label);
                row.field = new QWidget(this);
                auto* layout = new QHBoxLayout(row.field);
                layout->setContentsMargins(0, 0, 0, 0);
                break;
            }
            case Kind::Note:
                break;
            }
            form->addRow(row.label, row.field);
        }
        // Always there, so that a live form can give a field help later.
        row.help = new QLabel(QString::fromStdString(field.help), this);
        row.help->setWordWrap(true);
        QFont font = row.help->font();
        font.setPointSizeF(font.pointSizeF() * 0.9);
        row.help->setFont(font);
        form->addRow(QString(), row.help);
        m_rows.push_back(row);
        if (field.kind == Kind::Image) {
            fillImages(index);
        }
        if (field.kind == Kind::Radio) {
            const auto stored = values.find(field.key);
            fillRadios(index, stored != values.end() && !stored->second.empty()
                                  ? stored->second
                                  : field.defaultValue);
        }
        showRow(index);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    if (acceptLabel.isEmpty()) {
        m_accept = buttons->addButton(QDialogButtonBox::Save);
    } else {
        m_accept = buttons->addButton(acceptLabel, QDialogButtonBox::AcceptRole);
    }
    m_accept->setDefault(true);
    for (std::size_t index = 0; index < extraButtons.size(); ++index) {
        QPushButton* button = buttons->addButton(QString::fromStdString(extraButtons[index]),
                                                 QDialogButtonBox::ActionRole);
        const int result = vpx::kFormButton + static_cast<int>(index);
        connect(button, &QPushButton::clicked, this, [this, result]() { done(result); });
        m_extraButtons.push_back(button);
    }
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

void PluginSettingsDialog::setChangedAction(std::function<void(const std::string& key)> changed)
{
    m_changed = std::move(changed);
}

void PluginSettingsDialog::changed(const std::string& key)
{
    if (m_changed) {
        m_changed(key);
    }
}

std::optional<std::size_t> PluginSettingsDialog::indexOf(const std::string& key) const
{
    if (key.empty()) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < m_fields.size(); ++index) {
        if (m_fields[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

void PluginSettingsDialog::showRow(std::size_t index)
{
    const vpx::SettingField& field = m_fields[index];
    const Row& row = m_rows[index];
    for (QWidget* widget : {static_cast<QWidget*>(row.label), row.field}) {
        if (widget) {
            widget->setHidden(!field.visible);
            widget->setEnabled(field.enabled);
        }
    }
    row.help->setText(QString::fromStdString(field.help));
    row.help->setHidden(!field.visible || field.help.empty());
    row.help->setEnabled(field.enabled);
}

void PluginSettingsDialog::fillImages(std::size_t index)
{
    QWidget* container = m_rows[index].field;
    QLayout* layout = container->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const vpx::FormImage& picture : m_fields[index].images) {
        auto* cell = new QWidget(container);
        auto* cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        auto* image = new QLabel(cell);
        image->setPixmap(QPixmap::fromImage(formImage(picture)));
        cellLayout->addWidget(image);
        if (!picture.caption.empty()) {
            auto* caption = new QLabel(QString::fromStdString(picture.caption), cell);
            caption->setAlignment(Qt::AlignHCenter);
            cellLayout->addWidget(caption);
        }
        layout->addWidget(cell);
    }
    if (auto* box = qobject_cast<QHBoxLayout*>(layout)) {
        box->addStretch(1);
    }
}

void PluginSettingsDialog::fillRadios(std::size_t index, const std::string& value)
{
    const Row& row = m_rows[index];
    for (QAbstractButton* button : row.radios->buttons()) {
        row.radios->removeButton(button);
        delete button;
    }
    auto* layout = static_cast<QGridLayout*>(row.input->layout());
    const auto& choices = m_fields[index].choices;
    const std::string checked = hasChoice(choices, value) ? value : choices.front().first;
    int position = 0;
    for (const auto& [choice, text] : choices) {
        auto* button = new QRadioButton(QString::fromStdString(text), row.input);
        button->setProperty("choice", QString::fromStdString(choice));
        button->setChecked(choice == checked);
        row.radios->addButton(button);
        layout->addWidget(button, position / kRadiosPerLine, position % kRadiosPerLine);
        ++position;
    }
    // The buttons stay at the left, however few there are.
    for (int column = 0; column <= kRadiosPerLine; ++column) {
        layout->setColumnStretch(column, column == kRadiosPerLine ? 1 : 0);
    }
}

bool PluginSettingsDialog::update(const std::string& key, const vpx::FormFieldUpdate& update)
{
    const std::optional<std::size_t> found = indexOf(key);
    if (!found) {
        return false;
    }
    const std::size_t index = *found;
    vpx::SettingField& field = m_fields[index];
    const Row& row = m_rows[index];
    // Checked first, so that a refused update changes nothing.
    const bool choice = field.kind == Kind::Choice || field.kind == Kind::Radio;
    if (update.choices && !choice && field.kind != Kind::File) {
        return false;
    }
    if (update.choices && choice && update.choices->empty()) {
        return false;
    }
    if (update.images && field.kind != Kind::Image) {
        return false;
    }
    if (update.value) {
        switch (field.kind) {
        case Kind::Note:
        case Kind::Image:
        case Kind::Consent:
            return false;
        case Kind::Checkbox:
            if (*update.value != "0" && *update.value != "1") {
                return false;
            }
            break;
        case Kind::Choice:
        case Kind::Radio:
            if (!hasChoice(update.choices ? *update.choices : field.choices, *update.value)) {
                return false;
            }
            break;
        case Kind::Text:
        case Kind::Password:
        case Kind::File:
            break;
        }
    }

    if (update.label) {
        const QString text = QString::fromStdString(*update.label);
        field.label = *update.label;
        if (field.kind == Kind::Note) {
            qobject_cast<QLabel*>(row.field)->setText(text);
        } else if (auto* box = qobject_cast<QCheckBox*>(row.input)) {
            box->setText(text);
        } else {
            row.label->setText(text);
        }
    }
    if (update.choices) {
        field.choices = *update.choices;
        if (auto* combo = qobject_cast<QComboBox*>(row.input)) {
            fillChoices(combo, field.choices, combo->currentData().toString().toStdString());
        } else if (row.radios) {
            fillRadios(index, radioValue(row.radios));
        }
    }
    if (update.value) {
        const QString text = QString::fromStdString(*update.value);
        if (auto* edit = qobject_cast<QLineEdit*>(row.input)) {
            edit->setText(text);
        } else if (auto* box = qobject_cast<QCheckBox*>(row.input)) {
            box->setChecked(*update.value == "1");
        } else if (auto* combo = qobject_cast<QComboBox*>(row.input)) {
            combo->setCurrentIndex(combo->findData(text));
        } else if (row.radios) {
            for (QAbstractButton* button : row.radios->buttons()) {
                button->setChecked(button->property("choice").toString() == text);
            }
        }
    }
    if (update.images) {
        field.images = *update.images;
        fillImages(index);
    }
    if (update.help) {
        field.help = *update.help;
    }
    if (update.enabled) {
        field.enabled = *update.enabled;
    }
    if (update.visible) {
        field.visible = *update.visible;
    }
    showRow(index);
    return true;
}

std::optional<std::string> PluginSettingsDialog::value(const std::string& key) const
{
    const std::optional<std::size_t> index = indexOf(key);
    if (!index) {
        return std::nullopt;
    }
    if (const QButtonGroup* radios = m_rows[*index].radios) {
        return radioValue(radios);
    }
    QWidget* input = m_rows[*index].input;
    if (const auto* edit = qobject_cast<QLineEdit*>(input)) {
        return edit->text().toStdString();
    }
    if (const auto* box = qobject_cast<QCheckBox*>(input)) {
        return std::string(box->isChecked() ? "1" : "0");
    }
    if (const auto* combo = qobject_cast<QComboBox*>(input)) {
        return combo->currentData().toString().toStdString();
    }
    return std::nullopt;
}

std::map<std::string, std::string> PluginSettingsDialog::values() const
{
    std::map<std::string, std::string> result;
    for (const vpx::SettingField& field : m_fields) {
        if (std::optional<std::string> fieldValue = value(field.key)) {
            result[field.key] = std::move(*fieldValue);
        }
    }
    return result;
}

QWidget* PluginSettingsDialog::input(const std::string& key) const
{
    const std::optional<std::size_t> index = indexOf(key);
    return index ? m_rows[*index].input : nullptr;
}

QWidget* PluginSettingsDialog::row(const std::string& key) const
{
    const std::optional<std::size_t> index = indexOf(key);
    return index ? m_rows[*index].field : nullptr;
}

QLabel* PluginSettingsDialog::rowLabel(const std::string& key) const
{
    const std::optional<std::size_t> index = indexOf(key);
    return index ? m_rows[*index].label : nullptr;
}

QLabel* PluginSettingsDialog::helpLabel(const std::string& key) const
{
    const std::optional<std::size_t> index = indexOf(key);
    return index ? m_rows[*index].help : nullptr;
}

QPushButton* PluginSettingsDialog::extraButton(std::size_t index) const
{
    return index < m_extraButtons.size() ? m_extraButtons[index] : nullptr;
}

}  // namespace colorizing::app
