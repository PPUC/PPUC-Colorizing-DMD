#include "dialogs/SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "dialogs/DmdOutputSettingsPage.h"

namespace colorizing::app {

SettingsDialog::SettingsDialog(Theme theme,
                               const DmdRenderOptions& canvas,
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
    auto* themeForm = new QFormLayout;
    themeForm->addRow(QStringLiteral("Theme"), m_themeComboBox);

    auto* canvasGroup = new QGroupBox(QStringLiteral("Canvas"), appearance);
    m_pixelShapeComboBox = new QComboBox(canvasGroup);
    m_pixelShapeComboBox->addItem(QStringLiteral("Squares"), static_cast<int>(DmdPixelShape::Square));
    m_pixelShapeComboBox->addItem(QStringLiteral("Dots"), static_cast<int>(DmdPixelShape::Dot));
    m_pixelShapeComboBox->setCurrentIndex(
        m_pixelShapeComboBox->findData(static_cast<int>(canvas.pixelShape)));
    m_pixelShapeComboBox->setToolTip(
        QStringLiteral("DMD pixel shape of the canvas, the sprite editor and PNG export"));
    m_pixelSizeSpinBox = new QSpinBox(canvasGroup);
    m_pixelSizeSpinBox->setRange(50, 100);
    m_pixelSizeSpinBox->setSingleStep(10);
    m_pixelSizeSpinBox->setSuffix(QStringLiteral("%"));
    m_pixelSizeSpinBox->setValue(static_cast<int>(std::lround(canvas.pixelCoverage * 100)));
    m_pixelSizeSpinBox->setToolTip(QStringLiteral("DMD pixel size, relative to its cell"));
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
    canvasForm->addRow(QStringLiteral("DMD Shape"), m_pixelShapeComboBox);
    canvasForm->addRow(QStringLiteral("DMD Size"), m_pixelSizeSpinBox);

    auto* appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->addLayout(themeForm);
    appearanceLayout->addWidget(canvasGroup);
    appearanceLayout->addStretch(1);
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

DmdRenderOptions SettingsDialog::canvasOptions() const
{
    DmdRenderOptions options;
    options.pixelShape = static_cast<DmdPixelShape>(m_pixelShapeComboBox->currentData().toInt());
    options.pixelCoverage = static_cast<double>(m_pixelSizeSpinBox->value()) / 100.0;
    return options;
}

DmdOutputSettings SettingsDialog::dmdOutputSettings() const
{
    return m_dmdOutputPage->settings();
}

}  // namespace colorizing::app
