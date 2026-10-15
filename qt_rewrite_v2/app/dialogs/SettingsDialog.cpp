#include "dialogs/SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
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
    auto* canvasForm = new QFormLayout(canvasGroup);
    canvasForm->addRow(tr("Pixel Shape"), m_pixelShapeComboBox);
    canvasForm->addRow(tr("Pixel Size"), m_pixelSizeSpinBox);

    auto* appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->addLayout(themeForm);
    appearanceLayout->addWidget(canvasGroup);
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

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(buttons);
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
    }
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

DmdRenderOptions SettingsDialog::canvasOptions() const
{
    DmdRenderOptions options;
    options.pixelShape = static_cast<DmdPixelShape>(m_pixelShapeComboBox->currentData().toInt());
    options.pixelCoverage = static_cast<double>(m_pixelSizeSpinBox->value()) / 100.0;
    return options;
}

void SettingsDialog::setPlugins(const std::vector<PluginChoice>& plugins,
                                const QStringList& enabled)
{
    m_pluginList->clear();
    for (const PluginChoice& plugin : plugins) {
        const QString name = plugin.name.isEmpty() ? plugin.id : plugin.name;
        auto* item = new QListWidgetItem(
            plugin.version.isEmpty() ? name : tr("%1 (version %2)").arg(name, plugin.version),
            m_pluginList);
        item->setData(Qt::UserRole, plugin.id);
        item->setToolTip(plugin.description);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(enabled.contains(plugin.id) ? Qt::Checked : Qt::Unchecked);
    }
    m_pluginNote->setText(
        plugins.empty()
            ? tr("No editor plugins in the plugin folder. A plugin is a folder with its "
                 "plugin.cfg in the editor's plugin folder.")
            : tr("Plugins checked here are loaded now and at every start of the editor; "
                 "their entries appear in the Plugins menu."));
}

QStringList SettingsDialog::enabledPlugins() const
{
    QStringList enabled;
    for (int row = 0; row < m_pluginList->count(); ++row) {
        const QListWidgetItem* item = m_pluginList->item(row);
        if (item->checkState() == Qt::Checked) {
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
