// The Settings dialog and the theme. The offscreen platform takes no color scheme request,
// so a forced theme takes Fusion with a palette of its own here.

#include <cstdlib>
#include <iostream>
#include <string>

#include <QApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QListWidget>
#include <QProxyStyle>
#include <QSettings>
#include <QStyle>
#include <QImage>
#include <QPixmap>
#include <QSpinBox>
#include <QToolButton>
#include <QTabWidget>
#include <QTemporaryDir>

#include "Appearance.h"
#include "Translations.h"
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
    // The application's style is wrapped in the one that marks checked buttons.
    const auto* proxy = qobject_cast<QProxyStyle*>(QApplication::style());
    expect(proxy && proxy->baseStyle()->name() == styleName && windowLightness() == lightness,
           "back to the system's, the style and palette are as before");

    // Windows 11's style gives way to Fusion, unless a style was asked for.
    expect(app::editorStyleName(QStringLiteral("windows11"), false) == QStringLiteral("Fusion"),
           "Fusion on Windows 11");
    expect(app::editorStyleName(QStringLiteral("windows11"), true) == QStringLiteral("windows11"),
           "a style asked for is kept");
    expect(app::editorStyleName(QStringLiteral("windowsvista"), false) ==
                   QStringLiteral("windowsvista") &&
               app::editorStyleName(QStringLiteral("macOS"), false) == QStringLiteral("macOS"),
           "other platform styles are kept");
    {
        const char* plain[] = {"editor", "project.cdmd"};
        const char* separate[] = {"editor", "-style", "windows11"};
        const char* joined[] = {"editor", "-style=windows11"};
        expect(!app::styleAskedFor(2, plain) && app::styleAskedFor(3, separate) &&
                   app::styleAskedFor(2, joined),
               "-style asks for a style");
    }

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
    expect(loadedCanvas.offPixelColor == app::defaultOffPixelColor(),
           "off pixels are the default grey until chosen");
    {
        app::DmdRenderOptions chosen = canvas;
        chosen.offPixelColor = QColor(0, 0, 40);
        app::saveDmdRenderOptions(chosen);
        expect(app::loadDmdRenderOptions().offPixelColor == QColor(0, 0, 40),
               "the color of off pixels is kept");
        app::SettingsDialog chosenDialog(app::Theme::Light, chosen, dmd, false);
        expect(chosenDialog.canvasOptions().offPixelColor == QColor(0, 0, 40),
               "the dialog shows the color it was given");
        // The button shows the color, whatever the platform's style makes of a button.
        QToolButton* colorButton = nullptr;
        for (QToolButton* button : chosenDialog.findChildren<QToolButton*>()) {
            if (button->toolTip().startsWith(QStringLiteral("The color of off pixels"))) {
                colorButton = button;
            }
        }
        expect(colorButton != nullptr, "the color has its button");
        const QImage shown = colorButton->grab().toImage();
        expect(shown.pixelColor(shown.width() / 2, shown.height() / 2) == QColor(0, 0, 40),
               "the button shows the chosen color");
        QToolButton* defaultButton = nullptr;
        for (QToolButton* button : chosenDialog.findChildren<QToolButton*>()) {
            if (button->text() == QStringLiteral("Default")) {
                defaultButton = button;
            }
        }
        expect(defaultButton != nullptr, "the color can go back to the default");
        defaultButton->click();
        expect(chosenDialog.canvasOptions().offPixelColor == app::defaultOffPixelColor(),
               "Default gives the grey back");
        app::saveDmdRenderOptions(canvas);
    }
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
    expect(dialog.language().isEmpty(), "the language follows the system until chosen");
    dialog.setLanguage(QStringLiteral("de"));
    expect(dialog.language() == QStringLiteral("de"), "a language can be chosen");
    app::saveLanguage(QStringLiteral("fr"));
    expect(app::loadLanguage() == QStringLiteral("fr"), "the language is kept");
    app::saveLanguage(QString());
    expect(app::loadLanguage().isEmpty(), "and back to the system's");
    // Plugins: a built-in one is listed checked and cannot be unchecked; the others are chosen.
    app::SettingsDialog::PluginChoice dmdUtil{QStringLiteral("DMDUtil"), QStringLiteral("DMDUtil"),
                                              QString(), QStringLiteral("1.0"), true,
                                              QStringLiteral("DMD output")};
    app::SettingsDialog::PluginChoice other{QStringLiteral("Other"), QStringLiteral("Other"),
                                            QString(), QString(), false, QString()};
    dialog.setPlugins({dmdUtil, other}, {});
    QListWidget* pluginList = nullptr;
    for (QListWidget* list : dialog.findChildren<QListWidget*>()) {
        pluginList = list;
    }
    expect(pluginList && pluginList->count() == 2 &&
               pluginList->item(0)->checkState() == Qt::Checked &&
               !(pluginList->item(0)->flags() & Qt::ItemIsUserCheckable) &&
               pluginList->item(0)->text().contains(QStringLiteral("built in")) &&
               (pluginList->item(1)->flags() & Qt::ItemIsUserCheckable),
           "a built-in plugin is listed checked, not to be unchecked");
    expect(dialog.enabledPlugins().isEmpty(), "and is not one of the chosen plugins");
    pluginList->item(1)->setCheckState(Qt::Checked);
    expect(dialog.enabledPlugins() == QStringList{QStringLiteral("Other")},
           "the others are chosen with their check box");
    dialog.showPage(app::SettingsDialog::Page::DmdOutput);
    const auto* tabs = dialog.findChild<QTabWidget*>();
    expect(tabs && tabs->currentIndex() == 1 &&
               tabs->tabText(tabs->currentIndex()) == QStringLiteral("DMD Output"),
           "it opens at the page asked for");

    QSettings().remove(QStringLiteral("appearance"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
