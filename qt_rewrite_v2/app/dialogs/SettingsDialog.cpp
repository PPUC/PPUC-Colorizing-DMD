#include "dialogs/SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

#include "dialogs/DmdOutputSettingsPage.h"

namespace colorizing::app {

SettingsDialog::SettingsDialog(Theme theme,
                               const DmdOutputSettings& dmdOutput,
                               bool dmdOutputAvailable,
                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Settings"));
    m_tabs = new QTabWidget(this);

    auto* appearance = new QWidget(m_tabs);
    m_themeComboBox = new QComboBox(appearance);
    m_themeComboBox->addItem(QStringLiteral("Follow the system"), static_cast<int>(Theme::System));
    m_themeComboBox->addItem(QStringLiteral("Light"), static_cast<int>(Theme::Light));
    m_themeComboBox->addItem(QStringLiteral("Dark"), static_cast<int>(Theme::Dark));
    m_themeComboBox->setCurrentIndex(m_themeComboBox->findData(static_cast<int>(theme)));
    m_themeComboBox->setToolTip(
        QStringLiteral("Light or dark whatever the operating system uses, or as it does"));
    auto* appearanceForm = new QFormLayout(appearance);
    appearanceForm->addRow(QStringLiteral("Theme"), m_themeComboBox);
    m_tabs->addTab(appearance, QStringLiteral("Appearance"));

    auto* dmd = new QWidget(m_tabs);
    auto* dmdLayout = new QVBoxLayout(dmd);
    dmdLayout->setContentsMargins(0, 0, 0, 0);
    if (!dmdOutputAvailable) {
        auto* note = new QLabel(
            QStringLiteral("This build has no DMDUtil plugin: the settings are kept, but there "
                           "is no DMD output."),
            dmd);
        note->setWordWrap(true);
        dmdLayout->addWidget(note);
    }
    m_dmdOutputPage = new DmdOutputSettingsPage(dmdOutput, dmd);
    dmdLayout->addWidget(m_dmdOutputPage);
    m_tabs->addTab(dmd, QStringLiteral("DMD Output"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(buttons);
}

void SettingsDialog::showPage(Page page)
{
    m_tabs->setCurrentIndex(page == Page::Appearance ? 0 : 1);
}

Theme SettingsDialog::theme() const
{
    return static_cast<Theme>(m_themeComboBox->currentData().toInt());
}

DmdOutputSettings SettingsDialog::dmdOutputSettings() const
{
    return m_dmdOutputPage->settings();
}

}  // namespace colorizing::app
