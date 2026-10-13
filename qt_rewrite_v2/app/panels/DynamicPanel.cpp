#include "panels/DynamicPanel.h"

#include <array>
#include <exception>

#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

constexpr std::array<uint8_t, 8> kShadowDirectionBits{
    0x01, 0x02, 0x04, 0x80, 0x08, 0x40, 0x20, 0x10,
};
constexpr std::array<const char*, 8> kShadowDirectionLabels{
    "TL", "T", "TR", "L", "R", "BL", "B", "BR",
};
constexpr std::array<int, 8> kShadowDirectionRows{0, 0, 0, 1, 1, 2, 2, 2};
constexpr std::array<int, 8> kShadowDirectionColumns{0, 1, 2, 0, 2, 0, 1, 2};

QString colorButtonStyle(uint16_t rgb565, bool enabled = true)
{
    const QColor color = rgb565ToColor(rgb565);
    const QString border = enabled ? QStringLiteral("#666") : QStringLiteral("#333");
    return QStringLiteral("QToolButton { background: %1; border: 1px solid %2; }"
                          "QToolButton:disabled { color: #777; }")
        .arg(color.name(), border);
}

}  // namespace

DynamicPanel::DynamicPanel(QWidget* parent)
    : QWidget(parent)
{
    m_dynamicSetSpinBox = new QSpinBox(this);
    m_dynamicSetSpinBox->setToolTip(QStringLiteral("Dynamic mask set to paint"));
    m_dynamicSetSpinBox->setRange(0, static_cast<int>(document::kMaxDynamicSetsPerFrame - 1));
    m_dynamicSetSpinBox->setValue(0);
    m_dynamicSetSpinBox->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_noteLabel = new QLabel(this);
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setVisible(false);
    pageLayout->addWidget(m_noteLabel);
    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Dyn Set"), m_dynamicSetSpinBox);
    pageLayout->addLayout(form);
    auto* colorsGrid = new QGridLayout();
    colorsGrid->setHorizontalSpacing(4);
    colorsGrid->setVerticalSpacing(4);
    for (int colorIndex = 0; colorIndex < 16; ++colorIndex) {
        auto* button = new QToolButton(this);
        button->setText(QString::number(colorIndex));
        button->setToolTip(QStringLiteral("Dynamic color for original color %1").arg(colorIndex));
        button->setFixedSize(32, 24);
        button->setEnabled(false);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        colorsGrid->addWidget(button, colorIndex / 4, colorIndex % 4);
        m_colorButtons.push_back(button);
        connect(button, &QToolButton::clicked, this, [this, colorIndex]() {
            if (onColorClicked) {
                onColorClicked(static_cast<std::size_t>(colorIndex));
            }
        });
    }
    pageLayout->addLayout(colorsGrid);
    auto* shadowTitleLabel = new QLabel(QStringLiteral("Shadow"), this);
    shadowTitleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    pageLayout->addWidget(shadowTitleLabel);
    auto* shadowGrid = new QGridLayout();
    shadowGrid->setHorizontalSpacing(4);
    shadowGrid->setVerticalSpacing(4);
    for (std::size_t directionIndex = 0; directionIndex < kShadowDirectionBits.size();
         ++directionIndex) {
        auto* button = new QToolButton(this);
        button->setText(QString::fromLatin1(kShadowDirectionLabels[directionIndex]));
        button->setToolTip(QStringLiteral("Toggle dynamic shadow direction %1")
                               .arg(QString::fromLatin1(kShadowDirectionLabels[directionIndex])));
        button->setCheckable(true);
        button->setFixedSize(32, 24);
        button->setEnabled(false);
        button->setProperty("shadowBit", static_cast<int>(kShadowDirectionBits[directionIndex]));
        shadowGrid->addWidget(button,
                              kShadowDirectionRows[directionIndex],
                              kShadowDirectionColumns[directionIndex]);
        m_shadowDirectionButtons.push_back(button);
        connect(button, &QToolButton::toggled, this, [this, button](bool checked) {
            if (onShadowDirectionToggled) {
                onShadowDirectionToggled(
                    static_cast<uint8_t>(button->property("shadowBit").toInt()),
                    checked);
            }
        });
    }
    auto* shadowCenterLabel = new QLabel(QStringLiteral("S"), this);
    shadowCenterLabel->setAlignment(Qt::AlignCenter);
    shadowGrid->addWidget(shadowCenterLabel, 1, 1);
    pageLayout->addLayout(shadowGrid);
    m_shadowColorButton = new QToolButton(this);
    m_shadowColorButton->setText(QStringLiteral("Shadow Color"));
    m_shadowColorButton->setToolTip(QStringLiteral("Dynamic shadow color for active set"));
    m_shadowColorButton->setEnabled(false);
    pageLayout->addWidget(m_shadowColorButton);
    pageLayout->addStretch(1);

    connect(m_dynamicSetSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int setIndex) {
                if (onDynamicSetChanged) {
                    onDynamicSetChanged(setIndex);
                }
            });
    connect(m_shadowColorButton, &QToolButton::clicked, this, [this]() {
        if (onShadowColorClicked) {
            onShadowColorClicked();
        }
    });
}

void DynamicPanel::refreshColors(const document::ProjectDocument* document,
                                 std::optional<std::size_t> frameIndex,
                                 render::Resolution resolution)
{
    const bool hasFrame = document && frameIndex.has_value();
    const std::optional<document::FrameDynamicData>* dynamicData = nullptr;
    if (hasFrame) {
        dynamicData = &document->dynamicData(*frameIndex);
    }

    std::size_t colorCount = 0;
    if (dynamicData && dynamicData->has_value()) {
        const std::vector<uint16_t>& colors =
            resolution == render::Resolution::Hd ? (*dynamicData)->colorsHd
                                                 : (*dynamicData)->colorsSd;
        colorCount = colors.size() / document::kMaxDynamicSetsPerFrame;
    }

    for (std::size_t colorIndex = 0; colorIndex < m_colorButtons.size(); ++colorIndex) {
        QToolButton* button = m_colorButtons[colorIndex];
        const bool enabled = dynamicData && dynamicData->has_value() && colorIndex < colorCount;
        button->setEnabled(enabled);
        if (!enabled) {
            button->setToolTip(QStringLiteral("Dynamic color is not available"));
            button->setStyleSheet(colorButtonStyle(0, false));
            continue;
        }

        const uint16_t rgb565 =
            document->dynamicColor(*frameIndex, resolution, dynamicSet(), colorIndex);
        button->setProperty("rgb565", static_cast<int>(rgb565));
        button->setToolTip(QStringLiteral("Set %1 original %2: 0x%3")
                               .arg(dynamicSet())
                               .arg(static_cast<int>(colorIndex))
                               .arg(rgb565, 4, 16, QLatin1Char('0')));
        button->setStyleSheet(colorButtonStyle(rgb565));
    }
}

void DynamicPanel::refreshShadow(const document::ProjectDocument* document,
                                 std::optional<std::size_t> frameIndex,
                                 render::Resolution resolution)
{
    const bool enabled =
        document && frameIndex && document->dynamicData(*frameIndex).has_value();
    uint8_t direction = 0;
    uint16_t color = 0;
    if (enabled) {
        try {
            direction = document->dynamicShadowDirection(*frameIndex, resolution, dynamicSet());
            color = document->dynamicShadowColor(*frameIndex, resolution, dynamicSet());
        } catch (const std::exception&) {
            direction = 0;
            color = 0;
        }
    }

    for (QToolButton* button : m_shadowDirectionButtons) {
        QSignalBlocker blocker(button);
        const uint8_t bit = static_cast<uint8_t>(button->property("shadowBit").toInt());
        button->setEnabled(enabled);
        button->setChecked(enabled && (direction & bit) != 0);
    }

    m_shadowColorButton->setEnabled(enabled);
    m_shadowColorButton->setProperty("rgb565", static_cast<int>(color));
    m_shadowColorButton->setStyleSheet(colorButtonStyle(color, enabled));
    m_shadowColorButton->setToolTip(
        enabled ? QStringLiteral("Set %1 shadow color: 0x%2")
                      .arg(dynamicSet())
                      .arg(color, 4, 16, QLatin1Char('0'))
                : QStringLiteral("Dynamic shadow color is not available"));
}

void DynamicPanel::setDynamicSetEnabled(bool enabled)
{
    m_dynamicSetSpinBox->setEnabled(enabled);
}

void DynamicPanel::setNote(const QString& text)
{
    m_noteLabel->setText(text);
    m_noteLabel->setVisible(!text.isEmpty());
}

void DynamicPanel::setDynamicSet(int setIndex)
{
    QSignalBlocker blocker(m_dynamicSetSpinBox);
    m_dynamicSetSpinBox->setValue(setIndex);
}

uint8_t DynamicPanel::dynamicSet() const
{
    return static_cast<uint8_t>(m_dynamicSetSpinBox->value());
}

}  // namespace colorizing::app
