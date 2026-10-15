#include "Appearance.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

namespace colorizing::app {

namespace {

const QString kThemeKey = QStringLiteral("appearance/theme");
const QString kPixelShapeKey = QStringLiteral("appearance/dmdPixelShape");
const QString kPixelSizeKey = QStringLiteral("appearance/dmdPixelSize");

// What the application looked like before a theme was applied, to go back to.
struct Original {
    QString styleName;
    QPalette palette;
};

// Whether useFusion() set a palette of its own, which then has to go again.
bool g_paletteSet = false;

const Original& original()
{
    static const Original value{QApplication::style()->name(), QApplication::palette()};
    return value;
}

QPalette darkPalette()
{
    QPalette palette;
    const QColor window(45, 45, 48);
    const QColor base(30, 30, 32);
    const QColor text(225, 225, 225);
    const QColor disabledText(127, 127, 127);
    const QColor highlight(42, 130, 218);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::ToolTipBase, window);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, disabledText);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Light, window.lighter(150));
    palette.setColor(QPalette::Midlight, window.lighter(125));
    palette.setColor(QPalette::Mid, window.darker(120));
    palette.setColor(QPalette::Dark, window.darker(150));
    palette.setColor(QPalette::Shadow, Qt::black);
    for (const QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, disabledText);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, window.lighter(130));
    return palette;
}

void useFusion(Theme theme)
{
    QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"));
    QApplication::setStyle(fusion);
    QApplication::setPalette(theme == Theme::Dark ? darkPalette() : fusion->standardPalette());
    g_paletteSet = true;
}

void useOriginal()
{
    if (QApplication::style()->name() != original().styleName) {
        QApplication::setStyle(QStyleFactory::create(original().styleName));
    }
    // Only a palette set here goes: setting one at all would stop the application following
    // the system's changes.
    if (g_paletteSet) {
        QApplication::setPalette(original().palette);
        g_paletteSet = false;
    }
}

}  // namespace

Theme loadTheme()
{
    const QString value = QSettings().value(kThemeKey).toString();
    if (value == QStringLiteral("light")) {
        return Theme::Light;
    }
    if (value == QStringLiteral("dark")) {
        return Theme::Dark;
    }
    return Theme::System;
}

void saveTheme(Theme theme)
{
    QSettings().setValue(kThemeKey, theme == Theme::Light  ? QStringLiteral("light")
                                    : theme == Theme::Dark ? QStringLiteral("dark")
                                                           : QStringLiteral("system"));
}

DmdRenderOptions loadDmdRenderOptions()
{
    const QSettings settings;
    DmdRenderOptions options;
    options.pixelShape = settings.value(kPixelShapeKey).toString() == QStringLiteral("dots")
                             ? DmdPixelShape::Dot
                             : DmdPixelShape::Square;
    const int percent = std::clamp(settings.value(kPixelSizeKey, 100).toInt(), 50, 100);
    options.pixelCoverage = static_cast<double>((percent + 5) / 10 * 10) / 100.0;
    return options;
}

void saveDmdRenderOptions(const DmdRenderOptions& options)
{
    QSettings settings;
    settings.setValue(kPixelShapeKey, options.pixelShape == DmdPixelShape::Dot
                                          ? QStringLiteral("dots")
                                          : QStringLiteral("squares"));
    settings.setValue(kPixelSizeKey, static_cast<int>(std::lround(options.pixelCoverage * 100)));
}

void applyTheme(Theme theme)
{
    (void)original();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const Qt::ColorScheme wanted = theme == Theme::Light  ? Qt::ColorScheme::Light
                                   : theme == Theme::Dark ? Qt::ColorScheme::Dark
                                                          : Qt::ColorScheme::Unknown;
    QGuiApplication::styleHints()->setColorScheme(wanted);
    // A platform that does not take the request (some Linux ones, the offscreen one) leaves
    // the scheme as it was: then Fusion with a palette of its own below.
    if (theme == Theme::System || QGuiApplication::styleHints()->colorScheme() == wanted) {
        // No palette is set here: the platform's follows the scheme, and so does Fusion's.
        const bool drawsLightOnly =
            original().styleName.compare(QStringLiteral("windowsvista"), Qt::CaseInsensitive) ==
            0;
        const QString styleName = theme != Theme::System && drawsLightOnly
                                      ? QStringLiteral("Fusion")
                                      : original().styleName;
        if (QApplication::style()->name().compare(styleName, Qt::CaseInsensitive) != 0) {
            QApplication::setStyle(QStyleFactory::create(styleName));
        }
        if (g_paletteSet) {
            QApplication::setPalette(original().palette);
            g_paletteSet = false;
        }
        return;
    }
#endif
    if (theme == Theme::System) {
        useOriginal();
    } else {
        useFusion(theme);
    }
}

}  // namespace colorizing::app
