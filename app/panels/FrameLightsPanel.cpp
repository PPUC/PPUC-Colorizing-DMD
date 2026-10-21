#include "panels/FrameLightsPanel.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

#include "document/ProjectDocument.h"
#include "panels/InspectorForm.h"

namespace colorizing::app {

namespace {

// The combo box's data: the kind, and for an effect its ID.
constexpr int kEffectIdRole = Qt::UserRole + 1;

QColor qColor(uint32_t color)
{
    return QColor((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
}

}  // namespace

LightActionEditor::LightActionEditor(bool forDefault, QWidget* parent)
    : QWidget(parent),
      m_forDefault(forDefault)
{
    m_kind = new QComboBox(this);
    m_kind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_kind->setMinimumContentsLength(8);
    m_colorButton = new QToolButton(this);
    m_colorButton->setToolTip(tr("The static color"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_kind, 1);
    layout->addWidget(m_colorButton);
    connect(m_kind, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                updateColorButton();
                if (!m_filling && onChanged) {
                    onChanged();
                }
            });
    connect(m_colorButton, &QToolButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(qColor(m_color), this, tr("Static Color"));
        if (picked.isValid()) {
            setColor((static_cast<uint32_t>(picked.red()) << 16) |
                     (static_cast<uint32_t>(picked.green()) << 8) |
                     static_cast<uint32_t>(picked.blue()));
        }
    });
    setEffects(document::SpeakerLightSettings{});
}

void LightActionEditor::setEffects(const document::SpeakerLightSettings& settings)
{
    const document::LightAction kept = action();
    m_filling = true;
    m_kind->clear();
    if (!m_forDefault) {
        m_kind->addItem(tr("Unchanged"), static_cast<int>(document::LightActionKind::Unchanged));
    }
    m_kind->addItem(tr("Off"), static_cast<int>(document::LightActionKind::Off));
    m_kind->addItem(tr("Static color"), static_cast<int>(document::LightActionKind::Color));
    for (const document::LightEffect& effect : settings.effects) {
        m_kind->addItem(QString::fromStdString(effect.name),
                        static_cast<int>(document::LightActionKind::Effect));
        m_kind->setItemData(m_kind->count() - 1, effect.id, kEffectIdRole);
    }
    m_filling = false;
    setAction(kept);
}

void LightActionEditor::setAction(const document::LightAction& action)
{
    m_filling = true;
    document::LightActionKind kind = action.kind;
    if (m_forDefault && kind == document::LightActionKind::Unchanged) {
        kind = document::LightActionKind::Off;
    }
    int index = 0;
    for (int i = 0; i < m_kind->count(); ++i) {
        if (m_kind->itemData(i).toInt() == static_cast<int>(kind) &&
            (kind != document::LightActionKind::Effect ||
             m_kind->itemData(i, kEffectIdRole).toInt() == action.effectId)) {
            index = i;
            break;
        }
    }
    m_kind->setCurrentIndex(index);
    if (kind == document::LightActionKind::Color) {
        m_color = action.color;
    }
    m_filling = false;
    updateColorButton();
}

document::LightAction LightActionEditor::action() const
{
    document::LightAction action;
    if (m_kind->currentIndex() < 0) {
        return action;
    }
    action.kind = static_cast<document::LightActionKind>(m_kind->currentData().toInt());
    if (m_forDefault && action.kind == document::LightActionKind::Off) {
        action.kind = document::LightActionKind::Unchanged;
    }
    if (action.kind == document::LightActionKind::Effect) {
        action.effectId = static_cast<uint16_t>(m_kind->currentData(kEffectIdRole).toInt());
    }
    if (action.kind == document::LightActionKind::Color) {
        action.color = m_color;
    }
    return action;
}

void LightActionEditor::setColor(uint32_t color)
{
    m_color = color;
    const int index = m_kind->findData(static_cast<int>(document::LightActionKind::Color));
    if (m_kind->currentIndex() != index) {
        m_kind->setCurrentIndex(index);  // notifies
        return;
    }
    updateColorButton();
    if (onChanged) {
        onChanged();
    }
}

void LightActionEditor::updateColorButton()
{
    const bool isColor = m_kind->currentData().toInt() ==
                         static_cast<int>(document::LightActionKind::Color);
    QPixmap swatch(16, 16);
    swatch.fill(isColor ? qColor(m_color) : QColor(Qt::transparent));
    m_colorButton->setIcon(QIcon(swatch));
    m_colorButton->setEnabled(isEnabled());
}

FrameLightsPanel::FrameLightsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* form = makeInspectorForm();
    for (std::size_t index = 0; index < m_channels.size(); ++index) {
        m_channels[index] = new LightActionEditor(false, this);
        m_channels[index]->setToolTip(
            tr("What the frame does to speaker light channel %1 when it shows").arg(index + 1));
        m_channels[index]->onChanged = [this, index]() {
            if (onChannelChanged) {
                onChannelChanged(index, m_channels[index]->action());
            }
        };
        form->addRow(tr("Channel %1").arg(index + 1), m_channels[index]);
    }
    m_suggestButton = new QToolButton(this);
    m_suggestButton->setText(tr("Suggest from Frame"));
    m_suggestButton->setToolTip(
        tr("Ambient lighting: channel 1 the color of the left part of each selected frame, channel 2 of "
           "its right part"));
    connect(m_suggestButton, &QToolButton::clicked, this, [this]() {
        if (onSuggestRequested) {
            onSuggestRequested();
        }
    });
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(form);
    layout->addWidget(m_suggestButton);
    layout->addWidget(m_note);
    layout->addStretch(1);
    fillButtonRows(layout);
    setFrame(nullptr, std::nullopt);
}

void FrameLightsPanel::setFrame(const document::ProjectDocument* document,
                                std::optional<std::size_t> frame)
{
    const bool hasFrame = document && frame && *frame < document->frameCount();
    for (std::size_t index = 0; index < m_channels.size(); ++index) {
        LightActionEditor* editor = m_channels[index];
        editor->setEffects(document ? document->speakerLights()
                                    : document::SpeakerLightSettings{});
        editor->setAction(hasFrame ? document->frameLights(*frame)[index]
                                   : document::LightAction{});
        editor->setEnabled(hasFrame);
    }
    m_suggestButton->setEnabled(hasFrame);
    m_note->setText(document && document->speakerLights().effects.empty()
                        ? tr("Effects are made in the Light Effects section of the left sidebar.")
                        : QString());
    m_note->setVisible(!m_note->text().isEmpty());
}

}  // namespace colorizing::app
