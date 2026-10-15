// The Settings dialog and the theme. The offscreen platform takes no color scheme request,
// so a forced theme takes Fusion with a palette of its own here.

#include <cstdlib>
#include <iostream>
#include <string>

#include <QApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>

#include "Appearance.h"
#include "dialogs/SettingsDialog.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: settings: " << message << '\n';
        std::exit(1);
    }
}

int windowLightness()
{
    return QApplication::palette().window().color().lightness();
}

}  // namespace

void runSettingsWidgetTests()
{
    // An INI file of a test organization in a folder of its own (see DmdOutputWidgetTests).
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("SettingsWidgetTests"));

    expect(app::loadTheme() == app::Theme::System, "the system's theme by default");
    app::saveTheme(app::Theme::Dark);
    expect(app::loadTheme() == app::Theme::Dark, "a theme is kept");

    const QString styleName = QApplication::style()->name();
    const int lightness = windowLightness();
    app::applyTheme(app::Theme::Dark);
    expect(windowLightness() < 100, "dark is dark");
    app::applyTheme(app::Theme::Light);
    expect(windowLightness() > 150, "light is light");
    app::applyTheme(app::Theme::System);
    expect(QApplication::style()->name() == styleName && windowLightness() == lightness,
           "back to the system's, the style and palette are as before");

    app::DmdOutputSettings dmd;
    dmd.picture = app::DmdOutputSettings::Picture::Extra;
    dmd.serverPort = 6791;
    expect(app::loadDmdRenderOptions().pixelShape == app::DmdPixelShape::Square &&
               app::loadDmdRenderOptions().pixelCoverage == 0.8,
           "DMD pixels are squares at 80 % by default");
    app::DmdRenderOptions canvas;
    canvas.pixelShape = app::DmdPixelShape::Dot;
    canvas.pixelCoverage = 0.7;
    app::saveDmdRenderOptions(canvas);
    const app::DmdRenderOptions loadedCanvas = app::loadDmdRenderOptions();
    expect(loadedCanvas.pixelShape == app::DmdPixelShape::Dot &&
               loadedCanvas.pixelCoverage == 0.7,
           "the DMD pixel shape and size are kept");
    app::SettingsDialog dialog(app::Theme::Light, canvas, dmd, false);
    expect(dialog.canvasOptions().pixelShape == app::DmdPixelShape::Dot &&
               dialog.canvasOptions().pixelCoverage == 0.7,
           "the dialog shows the DMD pixels it was given");
    QSpinBox* sizeSpinBox = nullptr;
    for (QSpinBox* spinBox : dialog.findChildren<QSpinBox*>()) {
        if (spinBox->toolTip() == QStringLiteral("DMD pixel size, relative to its cell")) {
            sizeSpinBox = spinBox;
        }
    }
    expect(sizeSpinBox != nullptr, "the DMD pixel size is on the Appearance page");
    sizeSpinBox->setValue(83);
    expect(sizeSpinBox->value() == 80 && dialog.canvasOptions().pixelCoverage == 0.8,
           "the DMD pixel size snaps to steps of ten");
    expect(dialog.theme() == app::Theme::Light &&
               dialog.dmdOutputSettings().picture == app::DmdOutputSettings::Picture::Extra &&
               dialog.dmdOutputSettings().serverPort == 6791,
           "the dialog shows what it was given");
    dialog.showPage(app::SettingsDialog::Page::DmdOutput);
    const auto* tabs = dialog.findChild<QTabWidget*>();
    expect(tabs && tabs->currentIndex() == 1 &&
               tabs->tabText(tabs->currentIndex()) == QStringLiteral("DMD Output"),
           "it opens at the page asked for");

    QSettings().remove(QStringLiteral("appearance"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
