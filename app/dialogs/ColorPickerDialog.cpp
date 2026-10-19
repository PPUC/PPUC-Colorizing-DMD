#include "dialogs/ColorPickerDialog.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace colorizing::app {

namespace {

constexpr char kRecentColorsKey[] = "colorPicker/recentColors";

QColor toQColor(edit::Rgb888 color)
{
    return QColor(color.red, color.green, color.blue);
}

QString swatchStyle(edit::Rgb888 color)
{
    return QStringLiteral("background: %1; border: 1px solid #666;").arg(toQColor(color).name());
}

}  // namespace

// Hue from left to right, saturation from 100 at the top to 0 at the bottom, at full value.
class HsvSquareWidget final : public QWidget {
public:
    explicit HsvSquareWidget(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(kWidth, kHeight);
        setToolTip(QCoreApplication::translate(
            "ColorPickerDialog", "Hue (left to right) and saturation (top to bottom)"));
        m_image = QImage(kWidth, kHeight, QImage::Format_RGB32);
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const edit::Rgb888 color = edit::hsvToRgb888(edit::Hsv{
                    x * 360.0 / kWidth, 100.0 - y * 100.0 / (kHeight - 1), 100.0});
                m_image.setPixelColor(x, y, toQColor(color));
            }
        }
    }

    void setHueSaturation(double hue, double saturation)
    {
        m_hue = hue;
        m_saturation = saturation;
        update();
    }

    std::function<void(double hue, double saturation)> onPicked;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.drawImage(0, 0, m_image);
        const int x = static_cast<int>(std::lround(m_hue / 360.0 * kWidth));
        const int y = static_cast<int>(std::lround((100.0 - m_saturation) / 100.0 * (kHeight - 1)));
        painter.setPen(Qt::black);
        painter.drawEllipse(QPoint(x, y), 5, 5);
        painter.setPen(Qt::white);
        painter.drawEllipse(QPoint(x, y), 4, 4);
    }

    void mousePressEvent(QMouseEvent* event) override { pick(event->position().toPoint()); }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            pick(event->position().toPoint());
        }
    }

private:
    static constexpr int kWidth = 240;
    static constexpr int kHeight = 160;

    void pick(QPoint position)
    {
        const int x = std::clamp(position.x(), 0, kWidth - 1);
        const int y = std::clamp(position.y(), 0, kHeight - 1);
        if (onPicked) {
            onPicked(x * 360.0 / kWidth, 100.0 - y * 100.0 / (kHeight - 1));
        }
    }

    QImage m_image;
    double m_hue = 0.0;
    double m_saturation = 0.0;
};

// Value from 100 at the top to 0 at the bottom, for the hue and saturation of the square.
class ValueBarWidget final : public QWidget {
public:
    explicit ValueBarWidget(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(24, kHeight);
        setToolTip(QCoreApplication::translate("ColorPickerDialog", "Value (brightness)"));
    }

    void setHsv(edit::Hsv hsv)
    {
        m_hsv = hsv;
        update();
    }

    std::function<void(double value)> onPicked;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        QLinearGradient gradient(0, 0, 0, kHeight - 1);
        gradient.setColorAt(0.0,
                            toQColor(edit::hsvToRgb888(
                                edit::Hsv{m_hsv.hue, m_hsv.saturation, 100.0})));
        gradient.setColorAt(1.0, Qt::black);
        painter.fillRect(rect(), gradient);
        const int y = static_cast<int>(std::lround((100.0 - m_hsv.value) / 100.0 * (kHeight - 1)));
        painter.setPen(Qt::white);
        painter.drawLine(0, y, width() - 1, y);
        painter.setPen(Qt::black);
        painter.drawRect(0, std::max(0, y - 2), width() - 1, 4);
    }

    void mousePressEvent(QMouseEvent* event) override { pick(event->position().toPoint().y()); }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            pick(event->position().toPoint().y());
        }
    }

private:
    static constexpr int kHeight = 160;

    void pick(int y)
    {
        if (onPicked) {
            onPicked(100.0 - std::clamp(y, 0, kHeight - 1) * 100.0 / (kHeight - 1));
        }
    }

    edit::Hsv m_hsv;
};

namespace {

QSpinBox* makeSpinBox(QWidget* parent, const QString& name, const QString& tip, int maximum)
{
    auto* spinBox = new QSpinBox(parent);
    spinBox->setObjectName(name);
    spinBox->setToolTip(tip);
    spinBox->setRange(0, maximum);
    spinBox->setKeyboardTracking(false);
    return spinBox;
}

QDoubleSpinBox* makeDoubleSpinBox(QWidget* parent,
                                  const QString& name,
                                  const QString& tip,
                                  double maximum)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setObjectName(name);
    spinBox->setToolTip(tip);
    spinBox->setDecimals(1);
    spinBox->setSingleStep(1.0);
    spinBox->setRange(0.0, maximum);
    spinBox->setKeyboardTracking(false);
    return spinBox;
}

}  // namespace

ColorPickerDialog::ColorPickerDialog(uint16_t initialRgb565, const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("colorPickerDialog"));
    setWindowTitle(title);

    m_square = new HsvSquareWidget(this);
    m_valueBar = new ValueBarWidget(this);
    m_square->onPicked = [this](double hue, double saturation) {
        setFromHsv(edit::Hsv{hue, saturation, m_hsv.value});
    };
    m_valueBar->onPicked = [this](double value) {
        setFromHsv(edit::Hsv{m_hsv.hue, m_hsv.saturation, value});
    };

    const edit::Rgb888 initial = edit::rgb565ToRgb888(initialRgb565);
    auto* oldSwatch = new QLabel(this);
    oldSwatch->setFixedSize(48, 24);
    oldSwatch->setToolTip(tr("Color before"));
    oldSwatch->setStyleSheet(swatchStyle(initial));
    m_newSwatch = new QLabel(this);
    m_newSwatch->setFixedSize(48, 24);
    m_newSwatch->setToolTip(tr("Picked color"));

    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setObjectName(QStringLiteral("colorPickerHex"));
    m_hexEdit->setToolTip(tr("RGB888 as six hex digits"));
    m_hexEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("#?[0-9A-Fa-f]{0,6}")), m_hexEdit));
    m_hexEdit->setMaxLength(7);
    connect(m_hexEdit, &QLineEdit::textEdited, this, [this]() { hexEdited(); });
    // A half typed value is dropped when the field is left.
    connect(m_hexEdit, &QLineEdit::editingFinished, this, [this]() {
        hexEdited();
        m_updating = true;
        m_hexEdit->setText(QString::fromStdString(edit::rgb888ToHex(m_rgb)));
        m_updating = false;
    });

    m_red8 = makeSpinBox(this, QStringLiteral("colorPickerRed8"), tr("Red, 0-255"), 255);
    m_green8 = makeSpinBox(this, QStringLiteral("colorPickerGreen8"), tr("Green, 0-255"), 255);
    m_blue8 = makeSpinBox(this, QStringLiteral("colorPickerBlue8"), tr("Blue, 0-255"), 255);
    m_rgb565Edit = makeSpinBox(this,
                               QStringLiteral("colorPickerRgb565"),
                               tr("RGB565 as a decimal number, 0-65535"),
                               65535);
    m_red5 = makeSpinBox(this, QStringLiteral("colorPickerRed5"), tr("Red, 0-31"), 31);
    m_green6 = makeSpinBox(this, QStringLiteral("colorPickerGreen6"), tr("Green, 0-63"), 63);
    m_blue5 = makeSpinBox(this, QStringLiteral("colorPickerBlue5"), tr("Blue, 0-31"), 31);
    m_hue = makeDoubleSpinBox(this, QStringLiteral("colorPickerHue"), tr("Hue in degrees"), 359.9);
    m_hue->setWrapping(true);
    m_saturation = makeDoubleSpinBox(
        this, QStringLiteral("colorPickerSaturation"), tr("Saturation in %"), 100.0);
    m_value =
        makeDoubleSpinBox(this, QStringLiteral("colorPickerValue"), tr("Value in %"), 100.0);

    const auto rgbChanged = [this]() {
        if (!m_updating) {
            setFromRgb(edit::Rgb888{static_cast<uint8_t>(m_red8->value()),
                                    static_cast<uint8_t>(m_green8->value()),
                                    static_cast<uint8_t>(m_blue8->value())});
        }
    };
    for (QSpinBox* spinBox : {m_red8, m_green8, m_blue8}) {
        connect(spinBox, &QSpinBox::valueChanged, this, rgbChanged);
    }
    connect(m_rgb565Edit, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updating) {
            setFromRgb(edit::rgb565ToRgb888(static_cast<uint16_t>(value)));
        }
    });
    // Each of R5, G6 and B5 changes its own component only.
    connect(m_red5, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updating) {
            setFromRgb(edit::Rgb888{
                edit::expand5To8(static_cast<uint8_t>(value)), m_rgb.green, m_rgb.blue});
        }
    });
    connect(m_green6, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updating) {
            setFromRgb(edit::Rgb888{
                m_rgb.red, edit::expand6To8(static_cast<uint8_t>(value)), m_rgb.blue});
        }
    });
    connect(m_blue5, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updating) {
            setFromRgb(edit::Rgb888{
                m_rgb.red, m_rgb.green, edit::expand5To8(static_cast<uint8_t>(value))});
        }
    });
    const auto hsvChanged = [this]() {
        if (!m_updating) {
            setFromHsv(edit::Hsv{m_hue->value(), m_saturation->value(), m_value->value()});
        }
    };
    for (QDoubleSpinBox* spinBox : {m_hue, m_saturation, m_value}) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, hsvChanged);
    }

    auto* pickers = new QHBoxLayout();
    pickers->addWidget(m_square);
    pickers->addWidget(m_valueBar);

    auto* swatches = new QHBoxLayout();
    swatches->addWidget(new QLabel(tr("Before"), this));
    swatches->addWidget(oldSwatch);
    swatches->addWidget(new QLabel(tr("Now"), this));
    swatches->addWidget(m_newSwatch);
    swatches->addStretch(1);

    auto* fields = new QGridLayout();
    fields->addWidget(new QLabel(QStringLiteral("RGB888"), this), 0, 0);
    fields->addWidget(m_hexEdit, 0, 1, 1, 3);
    fields->addWidget(new QLabel(QStringLiteral("R8 G8 B8"), this), 1, 0);
    fields->addWidget(m_red8, 1, 1);
    fields->addWidget(m_green8, 1, 2);
    fields->addWidget(m_blue8, 1, 3);
    fields->addWidget(new QLabel(QStringLiteral("RGB565"), this), 2, 0);
    fields->addWidget(m_rgb565Edit, 2, 1, 1, 3);
    fields->addWidget(new QLabel(QStringLiteral("R5 G6 B5"), this), 3, 0);
    fields->addWidget(m_red5, 3, 1);
    fields->addWidget(m_green6, 3, 2);
    fields->addWidget(m_blue5, 3, 3);
    fields->addWidget(new QLabel(tr("H S V"), this), 4, 0);
    fields->addWidget(m_hue, 4, 1);
    fields->addWidget(m_saturation, 4, 2);
    fields->addWidget(m_value, 4, 3);

    auto* recentRow = new QHBoxLayout();
    recentRow->addWidget(new QLabel(tr("Recent"), this));
    const std::vector<uint16_t> recent = recentColors();
    for (std::size_t index = 0; index < kRecentColorCount; ++index) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("colorPickerRecent%1").arg(index));
        button->setFixedSize(24, 20);
        if (index < recent.size()) {
            const uint16_t color = recent[index];
            button->setStyleSheet(swatchStyle(edit::rgb565ToRgb888(color)));
            button->setToolTip(tr("Recent color 0x%1").arg(color, 4, 16, QLatin1Char('0')));
            connect(button, &QToolButton::clicked, this, [this, color]() {
                setFromRgb(edit::rgb565ToRgb888(color));
            });
        } else {
            button->setEnabled(false);
        }
        m_recentButtons.push_back(button);
        recentRow->addWidget(button);
    }
    recentRow->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(pickers);
    layout->addLayout(swatches);
    layout->addLayout(fields);
    layout->addLayout(recentRow);
    layout->addWidget(buttons);

    setFromRgb(initial);
}

std::optional<uint16_t> ColorPickerDialog::getColor(uint16_t initialRgb565,
                                                    QWidget* parent,
                                                    const QString& title)
{
    ColorPickerDialog dialog(initialRgb565, title, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    const uint16_t color = dialog.rgb565();
    rememberRecentColor(color);
    return color;
}

uint16_t ColorPickerDialog::rgb565() const
{
    return edit::rgb888ToRgb565(m_rgb);
}

edit::Rgb888 ColorPickerDialog::rgb888() const
{
    return m_rgb;
}

edit::Hsv ColorPickerDialog::hsv() const
{
    return m_hsv;
}

void ColorPickerDialog::setRgb565(uint16_t rgb565)
{
    setFromRgb(edit::rgb565ToRgb888(rgb565));
}

std::vector<uint16_t> ColorPickerDialog::recentColors()
{
    std::vector<uint16_t> colors;
    const QStringList stored = QSettings().value(kRecentColorsKey).toStringList();
    for (const QString& entry : stored) {
        bool ok = false;
        const int value = entry.toInt(&ok);
        if (ok && value >= 0 && value <= 0xffff && colors.size() < kRecentColorCount) {
            colors.push_back(static_cast<uint16_t>(value));
        }
    }
    return colors;
}

void ColorPickerDialog::rememberRecentColor(uint16_t rgb565)
{
    // As the Win32 picker: the color moves to the front, the oldest falls out.
    std::vector<uint16_t> colors = recentColors();
    colors.erase(std::remove(colors.begin(), colors.end(), rgb565), colors.end());
    colors.insert(colors.begin(), rgb565);
    if (colors.size() > kRecentColorCount) {
        colors.resize(kRecentColorCount);
    }
    QStringList stored;
    for (uint16_t color : colors) {
        stored.push_back(QString::number(color));
    }
    QSettings().setValue(kRecentColorsKey, stored);
}

void ColorPickerDialog::setFromRgb(edit::Rgb888 color)
{
    m_rgb = color;
    const edit::Hsv hsv = edit::rgb888ToHsv(color);
    // A grey has no hue and black no saturation either; the square keeps where it was.
    const bool hasHue = hsv.saturation > 0.0 && hsv.value > 0.0;
    m_hsv = edit::Hsv{hasHue ? hsv.hue : m_hsv.hue,
                      hsv.value > 0.0 ? hsv.saturation : m_hsv.saturation,
                      hsv.value};
    updateFields();
}

void ColorPickerDialog::setFromHsv(edit::Hsv hsv)
{
    m_hsv = hsv;
    m_rgb = edit::hsvToRgb888(hsv);
    updateFields();
}

void ColorPickerDialog::updateFields()
{
    m_updating = true;
    const edit::Hsv shown = edit::roundedHsv(m_hsv);
    const uint16_t rgb565 = edit::rgb888ToRgb565(m_rgb);
    const QString hex = QString::fromStdString(edit::rgb888ToHex(m_rgb));
    if (m_hexEdit->text().compare(hex, Qt::CaseInsensitive) != 0 &&
        m_hexEdit->text().compare(QLatin1Char('#') + hex, Qt::CaseInsensitive) != 0) {
        m_hexEdit->setText(hex);
    }
    m_red8->setValue(m_rgb.red);
    m_green8->setValue(m_rgb.green);
    m_blue8->setValue(m_rgb.blue);
    m_rgb565Edit->setValue(rgb565);
    m_red5->setValue((rgb565 >> 11) & 0x1f);
    m_green6->setValue((rgb565 >> 5) & 0x3f);
    m_blue5->setValue(rgb565 & 0x1f);
    m_hue->setValue(shown.hue);
    m_saturation->setValue(shown.saturation);
    m_value->setValue(shown.value);
    m_square->setHueSaturation(m_hsv.hue, m_hsv.saturation);
    m_valueBar->setHsv(m_hsv);
    m_newSwatch->setStyleSheet(swatchStyle(m_rgb));
    m_updating = false;
}

void ColorPickerDialog::hexEdited()
{
    if (m_updating) {
        return;
    }
    const std::optional<edit::Rgb888> color =
        edit::rgb888FromHex(m_hexEdit->text().toStdString());
    if (color && *color != m_rgb) {
        setFromRgb(*color);
    }
}

}  // namespace colorizing::app
