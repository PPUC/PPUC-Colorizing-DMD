#include "Appearance.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QComboBox>
#include <QProxyStyle>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>

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
// The name of the style under CheckedButtonStyle.
QString g_styleName;

// The application's style with checked buttons (the drawing tool, Show BG, a section that is
// open...) in the highlight color with highlighted text: in the native and the Fusion styles,
// light or dark, a checked button is otherwise hard to tell from one that is not.
class CheckedButtonStyle final : public QProxyStyle {
public:
    explicit CheckedButtonStyle(QStyle* base) : QProxyStyle(base) {}

    // A combo box with more entries than it shows at once drops down a list that scrolls
    // (issue #23). The popup some styles use instead (Fusion, Windows) shows every entry
    // around the current one and ran past the screen, its last entries out of reach.
    int styleHint(StyleHint hint,
                  const QStyleOption* option,
                  const QWidget* widget,
                  QStyleHintReturn* returnData) const override
    {
        if (hint == SH_ComboBox_Popup) {
            const auto* box = qobject_cast<const QComboBox*>(widget);
            if (box && box->count() > box->maxVisibleItems()) {
                return 0;
            }
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    void drawComplexControl(ComplexControl control,
                            const QStyleOptionComplex* option,
                            QPainter* painter,
                            const QWidget* widget) const override
    {
        const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        if (control != CC_ToolButton || !tool || !(tool->state & State_On) ||
            (tool->features & (QStyleOptionToolButton::MenuButtonPopup |
                               QStyleOptionToolButton::HasMenu)) ||
            (widget && widget->property(kPlainCheckedProperty).toBool())) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        paintPanel(*option, painter);
        QStyleOptionToolButton label = *tool;
        label.state &= ~(State_On | State_Sunken | State_MouseOver);
        label.palette.setColor(QPalette::ButtonText, textOn(fillColor(*option)));
        label.rect = subControlRect(CC_ToolButton, tool, SC_ToolButton, widget);
        QProxyStyle::drawControl(CE_ToolButtonLabel, &label, painter, widget);
        if (tool->state & State_HasFocus) {
            QStyleOptionFocusRect focus;
            focus.QStyleOption::operator=(*option);
            focus.rect = option->rect.adjusted(3, 3, -3, -3);
            QProxyStyle::drawPrimitive(PE_FrameFocusRect, &focus, painter, widget);
        }
    }

    void drawControl(ControlElement element,
                     const QStyleOption* option,
                     QPainter* painter,
                     const QWidget* widget) const override
    {
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        if (widget && widget->property(kPlainCheckedProperty).toBool()) {
            button = nullptr;
        }
        if (button && (button->state & State_On) && element == CE_PushButtonBevel) {
            paintPanel(*option, painter);
            return;
        }
        if (button && (button->state & State_On) && element == CE_PushButtonLabel) {
            QStyleOptionButton label = *button;
            label.palette.setColor(QPalette::ButtonText, textOn(fillColor(*option)));
            QProxyStyle::drawControl(element, &label, painter, widget);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

private:
    // The system's accent color where Qt knows it (the selection color can be faint, e.g. on
    // macOS), otherwise the highlight.
    static QColor fillColor(const QStyleOption& option)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        return option.palette.color(QPalette::Active, QPalette::Accent);
#else
        return option.palette.color(QPalette::Active, QPalette::Highlight);
#endif
    }

    static QColor textOn(const QColor& fill)
    {
        const double luminance =
            0.2126 * fill.redF() + 0.7152 * fill.greenF() + 0.0722 * fill.blueF();
        return luminance > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
    }

    static void paintPanel(const QStyleOption& option, QPainter* painter)
    {
        const bool enabled = option.state & State_Enabled;
        QColor fill = fillColor(option);
        if (!enabled) {
            fill.setAlpha(110);
        } else if (option.state & State_MouseOver) {
            fill = fill.lighter(112);
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(option.rect).adjusted(1.5, 1.5, -1.5, -1.5), 4, 4);
        painter->fillPath(path, fill);
        painter->setPen(QPen(fill.lighter(140), 1));
        painter->drawPath(path);
        painter->restore();
    }
};

// Sets the style of that name, under CheckedButtonStyle.
void setStyleNamed(const QString& name)
{
    if (QStyle* base = QStyleFactory::create(name)) {
        QApplication::setStyle(createEditorStyle(base));
        g_styleName = name;
    }
}

}  // namespace

QStyle* createEditorStyle(QStyle* base)
{
    return new CheckedButtonStyle(base);
}

namespace {

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
    if (g_styleName.compare(QStringLiteral("Fusion"), Qt::CaseInsensitive) != 0) {
        setStyleNamed(QStringLiteral("Fusion"));
    }
    QApplication::setPalette(theme == Theme::Dark ? darkPalette()
                                                  : QApplication::style()->standardPalette());
    g_paletteSet = true;
}

void useOriginal()
{
    if (g_styleName.compare(original().styleName, Qt::CaseInsensitive) != 0) {
        setStyleNamed(original().styleName);
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
    // 80 % by default: the DMD grid shows, as on a real display.
    const int percent = std::clamp(settings.value(kPixelSizeKey, 80).toInt(), 50, 100);
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
        if (g_styleName.compare(styleName, Qt::CaseInsensitive) != 0) {
            setStyleNamed(styleName);
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
