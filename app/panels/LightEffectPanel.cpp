#include "panels/LightEffectPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include "SpeakerLightView.h"
#include "panels/InspectorForm.h"
#include "runtime/SpeakerLightPlayer.h"

namespace colorizing::app {

namespace {

constexpr int kPreviewLeds = 16;
constexpr int kFrameMs = 33;

}  // namespace

LightEffectPanel::LightEffectPanel(QWidget* parent)
    : QWidget(parent),
      m_player(std::make_unique<runtime::SpeakerLightPlayer>(kPreviewLeds))
{
    m_titleLabel = new QLabel(this);
    m_titleLabel->setWordWrap(true);
    m_name = new QLineEdit(this);
    m_mode = new QComboBox(this);
    for (std::size_t mode = 0; mode < runtime::lightModeCount(); ++mode) {
        m_mode->addItem(QString::fromStdString(runtime::lightModeName(static_cast<uint8_t>(mode))),
                        static_cast<int>(mode));
    }
    m_mode->setToolTip(tr("The WS2812FX effect"));
    m_mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_mode->setMinimumContentsLength(8);
    auto* form = makeInspectorForm();
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Effect"), m_mode);
    for (std::size_t i = 0; i < m_colorButtons.size(); ++i) {
        m_colorButtons[i] = new QToolButton(this);
        m_colorButtons[i]->setToolTip(tr("Color %1 of the effect").arg(i + 1));
        m_white[i] = new QSpinBox(this);
        m_white[i]->setRange(0, 255);
        m_white[i]->setPrefix(tr("W "));
        m_white[i]->setToolTip(tr("The white of an RGBW strip"));
        auto* row = new QHBoxLayout;
        row->addWidget(m_colorButtons[i]);
        row->addWidget(m_white[i], 1);
        form->addRow(tr("Color %1").arg(i + 1), row);
        connect(m_colorButtons[i], &QToolButton::clicked, this, [this, i]() {
            const uint32_t rgb = m_colors[i];
            const QColor picked = QColorDialog::getColor(
                QColor((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff), this,
                tr("Color %1").arg(i + 1));
            if (picked.isValid()) {
                setColor(i, (static_cast<uint32_t>(picked.red()) << 16) |
                                (static_cast<uint32_t>(picked.green()) << 8) |
                                static_cast<uint32_t>(picked.blue()));
            }
        });
        connect(m_white[i], static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
                [this]() { changed(); });
    }
    m_speed = new QSpinBox(this);
    m_speed->setRange(2, 65535);
    m_speed->setSuffix(tr(" ms"));
    m_speed->setToolTip(tr("The length of one cycle of the effect: lower is faster"));
    form->addRow(tr("Speed"), m_speed);
    m_reverse = new QCheckBox(tr("Reverse"), this);
    m_gamma = new QCheckBox(tr("Gamma"), this);
    m_gamma->setToolTip(tr("Gamma-correct the colors"));
    auto* checks = new QHBoxLayout;
    checks->addWidget(m_reverse);
    checks->addWidget(m_gamma);
    form->addRow(tr("Options"), checks);
    m_fade = new QComboBox(this);
    m_fade->addItem(tr("Default"), 0);
    m_fade->addItem(tr("Extra fast"), 0x10);
    m_fade->addItem(tr("Fast"), 0x20);
    m_fade->addItem(tr("Medium"), 0x30);
    m_fade->addItem(tr("Slow"), 0x40);
    m_fade->addItem(tr("Extra slow"), 0x50);
    m_fade->addItem(tr("Very slow"), 0x60);
    m_fade->addItem(tr("Glacial"), 0x70);
    m_fade->setToolTip(tr("How fast the effects that fade do"));
    form->addRow(tr("Fade"), m_fade);
    m_size = new QComboBox(this);
    m_size->addItem(tr("Small"), 0);
    m_size->addItem(tr("Medium"), 0x02);
    m_size->addItem(tr("Large"), 0x04);
    m_size->addItem(tr("Extra large"), 0x06);
    m_size->setToolTip(tr("The size of the moving parts of the effects that have them"));
    form->addRow(tr("Size"), m_size);
    m_brightness = new QSpinBox(this);
    m_brightness->setRange(0, 100);
    m_brightness->setSuffix(tr(" %"));
    m_brightness->setToolTip(tr("Of the brightness set on the DMD"));
    form->addRow(tr("Brightness"), m_brightness);
    m_duration = new QSpinBox(this);
    // The DMD takes 16 bits of ms (libzedmd's SetLightEffect).
    m_duration->setRange(0, 65535);
    m_duration->setSingleStep(100);
    m_duration->setSuffix(tr(" ms"));
    m_duration->setSpecialValueText(tr("Until the next effect"));
    m_duration->setToolTip(
        tr("How long the effect plays before the channel returns to its default"));
    form->addRow(tr("Duration"), m_duration);

    m_preview = new SpeakerLightView(this);
    m_preview->setToolTip(tr("The effect as the fields say, on %1 LEDs").arg(kPreviewLeds));
    m_preview->setMinimumHeight(96);
    m_applyButton = new QToolButton(this);
    m_applyButton->setText(tr("Apply"));
    m_applyButton->setToolTip(tr("Change the effect as the fields say (Undo goes back)"));
    m_revertButton = new QToolButton(this);
    m_revertButton->setText(tr("Revert"));
    m_revertButton->setToolTip(tr("Show the effect's settings again, without the changes"));
    connect(m_applyButton, &QToolButton::clicked, this, [this]() {
        if (m_effect && onApplyRequested) {
            onApplyRequested(effect());
        }
    });
    connect(m_revertButton, &QToolButton::clicked, this, [this]() { setEffect(m_effect); });
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_applyButton);
    buttons->addWidget(m_revertButton);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_titleLabel);
    layout->addLayout(form);
    layout->addWidget(m_preview);
    layout->addLayout(buttons);
    layout->addStretch(1);
    fillButtonRows(layout);

    connect(m_name, &QLineEdit::textEdited, this, [this]() { changed(); });
    const auto comboChanged = static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged);
    for (QComboBox* box : {m_mode, m_fade, m_size}) {
        connect(box, comboChanged, this, [this]() { changed(); });
    }
    for (QCheckBox* box : {m_reverse, m_gamma}) {
        connect(box, &QCheckBox::toggled, this, [this]() { changed(); });
    }
    for (QSpinBox* box : {m_speed, m_brightness, m_duration}) {
        connect(box, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
                [this]() { changed(); });
    }
    m_clock.start();
    m_timer = new QTimer(this);
    m_timer->setInterval(kFrameMs);
    connect(m_timer, &QTimer::timeout, this, [this]() { advancePreview(0); });
    setEffect(std::nullopt);
}

LightEffectPanel::~LightEffectPanel() = default;

void LightEffectPanel::setEffect(const std::optional<document::LightEffect>& effect)
{
    m_effect = effect;
    const document::LightEffect shown = effect.value_or(document::LightEffect{});
    m_filling = true;
    m_name->setText(QString::fromStdString(shown.name));
    m_mode->setCurrentIndex(std::max(0, m_mode->findData(static_cast<int>(shown.mode))));
    for (std::size_t i = 0; i < m_colors.size(); ++i) {
        m_colors[i] = shown.colors[i] & 0x00ffffffu;
        m_white[i]->setValue(static_cast<int>(shown.colors[i] >> 24));
    }
    m_speed->setValue(shown.speed);
    m_reverse->setChecked((shown.options & document::kLightReverse) != 0);
    m_gamma->setChecked((shown.options & document::kLightGamma) != 0);
    m_fade->setCurrentIndex(
        std::max(0, m_fade->findData(static_cast<int>(shown.options & document::kLightFadeMask))));
    m_size->setCurrentIndex(
        std::max(0, m_size->findData(static_cast<int>(shown.options & document::kLightSizeMask))));
    m_brightness->setValue(static_cast<int>(std::lround(shown.brightness * 100.0 / 255.0)));
    m_duration->setValue(static_cast<int>(std::min<uint32_t>(shown.durationMs, 65535)));
    m_filling = false;
    m_changed = false;
    m_titleLabel->setText(effect ? tr("Effect %1").arg(QString::fromStdString(effect->name))
                                 : tr("No effect selected"));
    updateColorButtons();
    updateState();
    restartPreview();
}

std::optional<uint16_t> LightEffectPanel::effectId() const
{
    return m_effect ? std::optional<uint16_t>(m_effect->id) : std::nullopt;
}

document::LightEffect LightEffectPanel::effect() const
{
    document::LightEffect effect = m_effect.value_or(document::LightEffect{});
    effect.name = m_name->text().trimmed().toStdString();
    effect.mode = static_cast<uint8_t>(m_mode->currentData().toInt());
    for (std::size_t i = 0; i < m_colors.size(); ++i) {
        effect.colors[i] =
            (static_cast<uint32_t>(m_white[i]->value()) << 24) | (m_colors[i] & 0x00ffffffu);
    }
    effect.speed = static_cast<uint16_t>(m_speed->value());
    effect.options = static_cast<uint8_t>((m_reverse->isChecked() ? document::kLightReverse : 0) |
                                          (m_gamma->isChecked() ? document::kLightGamma : 0) |
                                          m_fade->currentData().toInt() |
                                          m_size->currentData().toInt());
    effect.brightness =
        static_cast<uint8_t>(std::lround(m_brightness->value() * 255.0 / 100.0));
    effect.durationMs = static_cast<uint32_t>(m_duration->value());
    return effect;
}

void LightEffectPanel::setColor(std::size_t index, uint32_t color)
{
    m_colors[index] = color & 0x00ffffffu;
    updateColorButtons();
    changed();
}

void LightEffectPanel::changed()
{
    if (m_filling) {
        return;
    }
    m_changed = true;
    updateState();
    restartPreview();
}

void LightEffectPanel::restartPreview()
{
    if (m_effect) {
        m_player->play(effect(), static_cast<uint32_t>(m_clock.elapsed()) + m_offsetMs);
    } else {
        m_player->setDefault(std::nullopt);
        m_player->playDefault(static_cast<uint32_t>(m_clock.elapsed()) + m_offsetMs);
    }
    advancePreview(0);
}

void LightEffectPanel::advancePreview(uint32_t ms)
{
    m_offsetMs += ms;
    const uint32_t now = static_cast<uint32_t>(m_clock.elapsed()) + m_offsetMs;
    m_player->update(now);
    // The preview repeats an effect with a duration instead of going dark.
    if (m_effect && m_player->playing() != effect()) {
        m_player->play(effect(), now);
    }
    m_preview->setColors(m_player->colors());
}

void LightEffectPanel::updateState()
{
    for (QWidget* field : std::initializer_list<QWidget*>{
             m_name, m_mode, m_speed, m_reverse, m_gamma, m_fade, m_size, m_brightness,
             m_duration, m_colorButtons[0], m_colorButtons[1], m_colorButtons[2], m_white[0],
             m_white[1], m_white[2]}) {
        field->setEnabled(m_effect.has_value());
    }
    m_applyButton->setEnabled(m_effect && m_changed && !m_name->text().trimmed().isEmpty());
    m_revertButton->setEnabled(m_effect && m_changed);
}

void LightEffectPanel::updateColorButtons()
{
    for (std::size_t i = 0; i < m_colorButtons.size(); ++i) {
        QPixmap swatch(16, 16);
        const uint32_t rgb = m_colors[i];
        swatch.fill(QColor((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff));
        m_colorButtons[i]->setIcon(QIcon(swatch));
    }
}

void LightEffectPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_timer->start();
}

void LightEffectPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

}  // namespace colorizing::app
