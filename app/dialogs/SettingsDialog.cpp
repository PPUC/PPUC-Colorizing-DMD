#include "dialogs/SettingsDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "Translations.h"
#include "dialogs/DmdOutputSettingsPage.h"

namespace colorizing::app {

SettingsDialog::SettingsDialog(Theme theme,
                               const DmdRenderOptions& canvas,
                               const DmdOutputSettings& dmdOutput,
                               bool dmdOutputAvailable,
                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    m_tabs = new QTabWidget(this);

    auto* appearance = new QWidget(m_tabs);
    m_themeComboBox = new QComboBox(appearance);
    m_themeComboBox->addItem(tr("Follow the system"), static_cast<int>(Theme::System));
    m_themeComboBox->addItem(tr("Light"), static_cast<int>(Theme::Light));
    m_themeComboBox->addItem(tr("Dark"), static_cast<int>(Theme::Dark));
    m_themeComboBox->setCurrentIndex(m_themeComboBox->findData(static_cast<int>(theme)));
    m_themeComboBox->setToolTip(
        tr("Light or dark whatever the operating system uses, or as it does"));
    m_languageComboBox = new QComboBox(appearance);
    m_languageComboBox->addItem(tr("Follow the system"), QString());
    for (const QString& language : availableLanguages()) {
        m_languageComboBox->addItem(languageName(language), language);
    }
    m_languageComboBox->setToolTip(
        tr("Language of the menus, panels and the handbook, from the next start"));
    auto* themeForm = new QFormLayout;
    themeForm->addRow(tr("Theme"), m_themeComboBox);
    themeForm->addRow(tr("Language"), m_languageComboBox);

    auto* canvasGroup = new QGroupBox(tr("Canvas"), appearance);
    m_pixelShapeComboBox = new QComboBox(canvasGroup);
    m_pixelShapeComboBox->addItem(tr("Squares"), static_cast<int>(DmdPixelShape::Square));
    m_pixelShapeComboBox->addItem(tr("Dots"), static_cast<int>(DmdPixelShape::Dot));
    m_pixelShapeComboBox->setCurrentIndex(
        m_pixelShapeComboBox->findData(static_cast<int>(canvas.pixelShape)));
    m_pixelShapeComboBox->setToolTip(
        tr("DMD pixel shape of the canvas, the sprite editor and PNG export"));
    m_pixelSizeSpinBox = new QSpinBox(canvasGroup);
    m_pixelSizeSpinBox->setRange(50, 100);
    m_pixelSizeSpinBox->setSingleStep(10);
    m_pixelSizeSpinBox->setSuffix(QStringLiteral("%"));
    m_pixelSizeSpinBox->setValue(static_cast<int>(std::lround(canvas.pixelCoverage * 100)));
    m_pixelSizeSpinBox->setToolTip(tr("DMD pixel size, relative to its cell"));
    connect(m_pixelSizeSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, [this](int value) {
                // The size snaps to steps of ten.
                const int rounded = std::clamp((value + 5) / 10 * 10, 50, 100);
                if (rounded != value) {
                    const QSignalBlocker blocker(m_pixelSizeSpinBox);
                    m_pixelSizeSpinBox->setValue(rounded);
                }
            });
    m_offPixelColorButton = new QToolButton(canvasGroup);
    m_offPixelColorButton->setFixedWidth(48);
    m_offPixelColorButton->setToolTip(
        tr("The color of off pixels in original frames, to tell them from the lowest shades"));
    setOffPixelColorShown(canvas.offPixelColor);
    connect(m_offPixelColorButton, &QToolButton::clicked, this, [this]() {
        const QColor color =
            QColorDialog::getColor(m_offPixelColor, this, tr("Off Pixel Color"));
        if (color.isValid()) {
            setOffPixelColorShown(color);
        }
    });
    auto* offPixelDefault = new QToolButton(canvasGroup);
    offPixelDefault->setText(tr("Default"));
    offPixelDefault->setToolTip(tr("Back to the very dark grey"));
    connect(offPixelDefault, &QToolButton::clicked, this,
            [this]() { setOffPixelColorShown(defaultOffPixelColor()); });
    auto* offPixelRow = new QHBoxLayout;
    offPixelRow->addWidget(m_offPixelColorButton);
    offPixelRow->addWidget(offPixelDefault);
    offPixelRow->addStretch(1);
    auto* canvasForm = new QFormLayout(canvasGroup);
    canvasForm->addRow(tr("Pixel Shape"), m_pixelShapeComboBox);
    canvasForm->addRow(tr("Pixel Size"), m_pixelSizeSpinBox);
    canvasForm->addRow(tr("Off Pixels"), offPixelRow);

    // As the pixels of the canvas, the speakers are the editor's: machines differ.
    auto* speakersGroup = new QGroupBox(tr("Speaker Light Preview"), appearance);
    auto* speakersForm = new QFormLayout(speakersGroup);
    m_speakersShown = new QCheckBox(tr("Show the speakers beside the canvas and in the frame "
                                       "tester"),
                                    speakersGroup);
    m_speakerShape = new QComboBox(speakersGroup);
    m_speakerShape->addItem(tr("Circle"), static_cast<int>(SpeakerShape::Circle));
    m_speakerShape->addItem(tr("Rectangle"), static_cast<int>(SpeakerShape::Rectangle));
    m_speakerLeds = new QSpinBox(speakersGroup);
    m_speakerLeds->setRange(1, 256);
    m_speakerLeds->setToolTip(tr("The LEDs of each speaker, as set up on the DMD"));
    m_leftChannel = new QComboBox(speakersGroup);
    m_rightChannel = new QComboBox(speakersGroup);
    for (QComboBox* box : {m_leftChannel, m_rightChannel}) {
        for (int channel = 0; channel < 4; ++channel) {
            box->addItem(tr("Channel %1").arg(channel + 1), channel);
        }
    }
    speakersForm->addRow(m_speakersShown);
    speakersForm->addRow(tr("Shape"), m_speakerShape);
    speakersForm->addRow(tr("LEDs"), m_speakerLeds);
    speakersForm->addRow(tr("Left speaker"), m_leftChannel);
    speakersForm->addRow(tr("Right speaker"), m_rightChannel);
    setSpeakerLightPreview(SpeakerLightPreviewOptions{});

    auto* appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->addLayout(themeForm);
    appearanceLayout->addWidget(canvasGroup);
    appearanceLayout->addWidget(speakersGroup);
    appearanceLayout->addStretch(1);
    m_tabs->addTab(appearance, tr("Appearance"));

    auto* dmd = new QWidget(m_tabs);
    auto* dmdLayout = new QVBoxLayout(dmd);
    dmdLayout->setContentsMargins(0, 0, 0, 0);
    if (!dmdOutputAvailable) {
        auto* note = new QLabel(
            tr("This build has no DMDUtil plugin: the settings are kept, but there "
                           "is no DMD output."),
            dmd);
        note->setWordWrap(true);
        dmdLayout->addWidget(note);
    }
    m_dmdOutputPage = new DmdOutputSettingsPage(dmdOutput, dmd);
    dmdLayout->addWidget(m_dmdOutputPage);
    m_tabs->addTab(dmd, tr("DMD Output"));

    auto* plugins = new QWidget(m_tabs);
    m_pluginNote = new QLabel(plugins);
    m_pluginNote->setWordWrap(true);
    m_pluginList = new QListWidget(plugins);
    auto* pluginsLayout = new QVBoxLayout(plugins);
    pluginsLayout->addWidget(m_pluginNote);
    pluginsLayout->addWidget(m_pluginList, 1);
    m_tabs->addTab(plugins, tr("Plugins"));
    setPlugins({}, {});

    auto* projects = new QWidget(m_tabs);
    m_authorNameEdit = new QLineEdit(projects);
    m_authorNameEdit->setPlaceholderText(tr("For example your name in the Serum community"));
    auto* authorNote = new QLabel(
        tr("A save writes it as the author of every frame, mask, background, sprite and "
           "palette that changed, with the time. Partners see who changed what."),
        projects);
    authorNote->setWordWrap(true);
    auto* projectsForm = new QFormLayout;
    projectsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    projectsForm->addRow(tr("Your name"), m_authorNameEdit);
    projectsForm->addRow(authorNote);
    auto* projectsLayout = new QVBoxLayout(projects);
    projectsLayout->addLayout(projectsForm);
    projectsLayout->addStretch(1);
    m_tabs->addTab(projects, tr("Projects"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(buttons);
}

void SettingsDialog::setOffPixelColorShown(const QColor& color)
{
    m_offPixelColor = color;
    m_offPixelColorButton->setStyleSheet(
        QStringLiteral("QToolButton { background-color: %1; }").arg(color.name()));
}

void SettingsDialog::showPage(Page page)
{
    switch (page) {
        case Page::Appearance:
            m_tabs->setCurrentIndex(0);
            break;
        case Page::DmdOutput:
            m_tabs->setCurrentIndex(1);
            break;
        case Page::Plugins:
            m_tabs->setCurrentIndex(2);
            break;
        case Page::Projects:
            m_tabs->setCurrentIndex(3);
            break;
    }
}

void SettingsDialog::setAuthorName(const QString& name)
{
    m_authorNameEdit->setText(name);
}

QString SettingsDialog::authorName() const
{
    return m_authorNameEdit->text().trimmed();
}

Theme SettingsDialog::theme() const
{
    return static_cast<Theme>(m_themeComboBox->currentData().toInt());
}

void SettingsDialog::setLanguage(const QString& language)
{
    m_languageComboBox->setCurrentIndex(std::max(0, m_languageComboBox->findData(language)));
}

QString SettingsDialog::language() const
{
    return m_languageComboBox->currentData().toString();
}

void SettingsDialog::setSpeakerLightPreview(const SpeakerLightPreviewOptions& options)
{
    m_speakersShown->setChecked(options.visible);
    m_speakerShape->setCurrentIndex(
        std::max(0, m_speakerShape->findData(static_cast<int>(options.shape))));
    m_speakerLeds->setValue(options.ledCount);
    m_leftChannel->setCurrentIndex(options.leftChannel);
    m_rightChannel->setCurrentIndex(options.rightChannel);
}

SpeakerLightPreviewOptions SettingsDialog::speakerLightPreview() const
{
    SpeakerLightPreviewOptions options;
    options.visible = m_speakersShown->isChecked();
    options.shape = static_cast<SpeakerShape>(m_speakerShape->currentData().toInt());
    options.ledCount = m_speakerLeds->value();
    options.leftChannel = m_leftChannel->currentIndex();
    options.rightChannel = m_rightChannel->currentIndex();
    return options;
}

DmdRenderOptions SettingsDialog::canvasOptions() const
{
    DmdRenderOptions options;
    options.pixelShape = static_cast<DmdPixelShape>(m_pixelShapeComboBox->currentData().toInt());
    options.pixelCoverage = static_cast<double>(m_pixelSizeSpinBox->value()) / 100.0;
    options.offPixelColor = m_offPixelColor;
    return options;
}

void SettingsDialog::setPlugins(const std::vector<PluginChoice>& plugins,
                                const QStringList& enabled)
{
    m_pluginList->clear();
    for (const PluginChoice& plugin : plugins) {
        const QString name = plugin.name.isEmpty() ? plugin.id : plugin.name;
        QString text =
            plugin.version.isEmpty() ? name : tr("%1 (version %2)").arg(name, plugin.version);
        if (plugin.builtIn) {
            text = tr("%1 — built in, for the %2").arg(text, plugin.builtInUse);
        }
        auto* item = new QListWidgetItem(text, m_pluginList);
        item->setData(Qt::UserRole, plugin.id);
        item->setData(Qt::UserRole + 1, plugin.builtIn);
        item->setToolTip(plugin.description);
        if (plugin.builtIn) {
            // Shown checked; without ItemIsUserCheckable the check box cannot be changed.
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        } else {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(enabled.contains(plugin.id) ? Qt::Checked : Qt::Unchecked);
        }
    }
    const bool anyChoice = std::any_of(plugins.begin(), plugins.end(),
                                       [](const PluginChoice& plugin) { return !plugin.builtIn; });
    m_pluginNote->setText(
        !anyChoice
            ? tr("No plugins to choose in the plugin folder, only those built in. A plugin is a "
                 "folder with its plugin.cfg in the editor's plugin folder.")
            : tr("Plugins checked here are loaded now and at every start of the editor; "
                 "their entries appear in the Plugins menu."));
}

QStringList SettingsDialog::enabledPlugins() const
{
    QStringList enabled;
    for (int row = 0; row < m_pluginList->count(); ++row) {
        const QListWidgetItem* item = m_pluginList->item(row);
        if (item->checkState() == Qt::Checked && !item->data(Qt::UserRole + 1).toBool()) {
            enabled << item->data(Qt::UserRole).toString();
        }
    }
    return enabled;
}

DmdOutputSettings SettingsDialog::dmdOutputSettings() const
{
    return m_dmdOutputPage->settings();
}

}  // namespace colorizing::app
