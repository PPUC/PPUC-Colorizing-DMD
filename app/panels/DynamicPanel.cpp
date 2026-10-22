#include "panels/DynamicPanel.h"

#include <algorithm>
#include <array>
#include <exception>

#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QMimeData>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "panels/InspectorForm.h"
#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

// What the palette and the color grids drag (ColorGridWidget).
constexpr char kRgb565MimeType[] = "application/x-colorizing-rgb565";

// A dynamic color: clicked, it is edited; a color dragged from the palette is dropped on it.
class DynamicColorButton final : public QToolButton {
public:
    explicit DynamicColorButton(QWidget* parent)
        : QToolButton(parent)
    {
        setAcceptDrops(true);
    }

    std::function<void(uint16_t rgb565)> onDropped;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (isEnabled() && event->mimeData()->hasFormat(kRgb565MimeType)) {
            event->acceptProposedAction();
            return;
        }
        QToolButton::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        bool ok = false;
        const int value =
            QString::fromLatin1(event->mimeData()->data(kRgb565MimeType)).toInt(&ok);
        if (ok && value >= 0 && value <= 0xffff && onDropped) {
            onDropped(static_cast<uint16_t>(value));
            event->acceptProposedAction();
            return;
        }
        QToolButton::dropEvent(event);
    }
};

constexpr std::array<uint8_t, 8> kShadowDirectionBits{
    0x01, 0x02, 0x04, 0x80, 0x08, 0x40, 0x20, 0x10,
};
struct ShadowDirectionLabel {
    const char* text;
    const char* comment;
};
constexpr std::array<ShadowDirectionLabel, 8> kShadowDirectionLabels{{
    QT_TRANSLATE_NOOP3("DynamicPanel", "TL", "shadow direction top left"),
    QT_TRANSLATE_NOOP3("DynamicPanel", "T", "shadow direction top"),
    QT_TRANSLATE_NOOP3("DynamicPanel", "TR", "shadow direction top right"),
    QT_TRANSLATE_NOOP3("DynamicPanel", "L", "shadow direction left"),
    QT_TRANSLATE_NOOP3("DynamicPanel", "R", "shadow direction right"),
    QT_TRANSLATE_NOOP3("DynamicPanel", "BL", "shadow direction bottom left"),
    QT_TRANSLATE_NOOP3("DynamicPanel", "B", "shadow direction bottom"),
    QT_TRANSLATE_NOOP3("DynamicPanel", "BR", "shadow direction bottom right"),
}};
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
    m_dynamicSetSpinBox->setToolTip(tr("Dynamic mask set to paint"));
    m_dynamicSetSpinBox->setRange(0, static_cast<int>(document::kMaxDynamicSetsPerFrame - 1));
    m_dynamicSetSpinBox->setValue(0);
    m_dynamicSetSpinBox->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_noteLabel = new QLabel(this);
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setVisible(false);
    pageLayout->addWidget(m_noteLabel);
    m_setList = new QListWidget(this);
    m_setList->setToolTip(
        tr("The dynamic sets of the frame: a click selects the set to paint and edit"));
    m_setList->setIconSize(QSize(128, 32));
    m_setList->setMaximumHeight(190);
    pageLayout->addWidget(m_setList);
    auto* form = makeInspectorForm();
    form->addRow(tr("Dyn Set"), m_dynamicSetSpinBox);
    pageLayout->addLayout(form);
    auto* colorsGrid = new QGridLayout();
    colorsGrid->setHorizontalSpacing(4);
    colorsGrid->setVerticalSpacing(4);
    for (int colorIndex = 0; colorIndex < 16; ++colorIndex) {
        auto* button = new DynamicColorButton(this);
        button->onDropped = [this, colorIndex](uint16_t rgb565) {
            if (onColorDropped) {
                onColorDropped(static_cast<std::size_t>(colorIndex), rgb565);
            }
        };
        button->setText(QString::number(colorIndex));
        button->setToolTip(tr("Dynamic color for original color %1").arg(colorIndex));
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
    auto* shadowTitleLabel = new QLabel(tr("Shadow"), this);
    shadowTitleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    pageLayout->addWidget(shadowTitleLabel);
    auto* shadowGrid = new QGridLayout();
    shadowGrid->setHorizontalSpacing(4);
    shadowGrid->setVerticalSpacing(4);
    for (std::size_t directionIndex = 0; directionIndex < kShadowDirectionBits.size();
         ++directionIndex) {
        auto* button = new QToolButton(this);
        const ShadowDirectionLabel& label = kShadowDirectionLabels[directionIndex];
        const QString labelText = tr(label.text, label.comment);
        button->setText(labelText);
        button->setToolTip(tr("Toggle dynamic shadow direction %1").arg(labelText));
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
    auto* shadowCenterLabel = new QLabel(tr("S"), this);
    shadowCenterLabel->setAlignment(Qt::AlignCenter);
    shadowGrid->addWidget(shadowCenterLabel, 1, 1);
    pageLayout->addLayout(shadowGrid);
    m_shadowColorButton = new QToolButton(this);
    m_shadowColorButton->setText(tr("Shadow Color"));
    m_shadowColorButton->setToolTip(tr("Dynamic shadow color for active set"));
    m_shadowColorButton->setEnabled(false);
    pageLayout->addWidget(m_shadowColorButton);
    m_originalOnlyCheckBox = new QCheckBox(tr("Only on the SD Picture"), this);
    m_originalOnlyCheckBox->setEnabled(false);
    pageLayout->addWidget(m_originalOnlyCheckBox);
    pageLayout->addStretch(1);
    connect(m_originalOnlyCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (onOriginalOnlyToggled) {
            onOriginalOnlyToggled(checked);
        }
    });

    connect(m_dynamicSetSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int setIndex) {
                if (onDynamicSetChanged) {
                    onDynamicSetChanged(setIndex);
                }
            });
    connect(m_setList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int setIndex = item->data(Qt::UserRole).toInt();
        if (setIndex < 0 || setIndex == m_dynamicSetSpinBox->value()) {
            return;
        }
        m_dynamicSetSpinBox->setValue(setIndex);
    });
    connect(m_shadowColorButton, &QToolButton::clicked, this, [this]() {
        if (onShadowColorClicked) {
            onShadowColorClicked();
        }
    });
}

void DynamicPanel::refreshSets(const document::ProjectDocument* document,
                               std::optional<std::size_t> frameIndex,
                               render::Resolution resolution)
{
    const uint64_t revision = document ? document->revision() : 0;
    if (document == m_listDocument && revision == m_listRevision && frameIndex == m_listFrame &&
        resolution == m_listResolution) {
        selectListedSet();
        return;
    }
    m_listDocument = document;
    m_listRevision = revision;
    m_listFrame = frameIndex;
    m_listResolution = resolution;
    QSignalBlocker blocker(m_setList);
    m_setList->clear();
    const std::optional<document::FrameDynamicData>* dynamicData =
        document && frameIndex ? &document->dynamicData(*frameIndex) : nullptr;
    if (!dynamicData || !dynamicData->has_value()) {
        return;
    }
    const document::IndexedFrame& mask = resolution == render::Resolution::Hd
                                             ? (*dynamicData)->maskHd
                                             : (*dynamicData)->maskSd;
    const document::IndexedFrame& original = document->originalFrame(*frameIndex);
    const bool shadesFit = original.size == mask.size;
    std::vector<std::size_t> counts(document::kMaxDynamicSetsPerFrame, 0);
    for (const uint8_t set : mask.pixels) {
        if (set < counts.size()) {
            ++counts[set];
        }
    }
    // A picture of the frame per set: its zones in the set's colors, the rest dimmed.
    for (std::size_t set = 0; set < counts.size(); ++set) {
        if (counts[set] == 0) {
            continue;
        }
        QImage image(mask.size.width, mask.size.height, QImage::Format_RGB32);
        for (int y = 0; y < mask.size.height; ++y) {
            for (int x = 0; x < mask.size.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) *
                                              static_cast<std::size_t>(mask.size.width) +
                                          static_cast<std::size_t>(x);
                const uint8_t shade = shadesFit ? original.pixels[index] : uint8_t{0};
                QColor color;
                if (mask.pixels[index] == set) {
                    color = rgb565ToColor(
                        document->dynamicColor(*frameIndex, resolution, set, shade));
                } else {
                    color = originalIndexToColor(shade, document->colorCount()).darker(400);
                }
                image.setPixel(x, y, color.rgb());
            }
        }
        auto* item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(image.scaled(m_setList->iconSize(), Qt::KeepAspectRatio,
                                                  Qt::FastTransformation))),
            tr("%1\n%2 px").arg(set).arg(static_cast<qulonglong>(counts[set])));
        item->setSizeHint(QSize(0, m_setList->iconSize().height() + 8));
        item->setData(Qt::UserRole, static_cast<int>(set));
        item->setToolTip(tr("Dynamic set %1: %2 pixels")
                             .arg(set)
                             .arg(static_cast<qulonglong>(counts[set])));
        m_setList->addItem(item);
    }
    const auto unused = std::find(counts.begin(), counts.end(), std::size_t{0});
    if (unused != counts.end()) {
        const int set = static_cast<int>(unused - counts.begin());
        auto* item = new QListWidgetItem(tr("New set (%1)").arg(set));
        item->setSizeHint(QSize(0, 24));
        item->setData(Qt::UserRole, set);
        item->setToolTip(tr("Select set %1, which the frame does not use yet, to "
                            "paint a new zone")
                             .arg(set));
        m_setList->addItem(item);
    }
    selectListedSet();
}

void DynamicPanel::selectListedSet()
{
    QSignalBlocker blocker(m_setList);
    m_setList->clearSelection();
    for (int row = 0; row < m_setList->count(); ++row) {
        if (m_setList->item(row)->data(Qt::UserRole).toInt() == m_dynamicSetSpinBox->value()) {
            m_setList->setCurrentRow(row);
            return;
        }
    }
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
        const document::CompactArray<uint16_t>& colors =
            resolution == render::Resolution::Hd ? (*dynamicData)->colorsHd
                                                 : (*dynamicData)->colorsSd;
        colorCount = colors.size() / document::kMaxDynamicSetsPerFrame;
    }

    // As many as the project has original colors (4 or 16): a set has a color per shade.
    const std::size_t shownCount = document ? std::min<std::size_t>(document->colorCount(),
                                                                     m_colorButtons.size())
                                            : 4;
    colorCount = std::min(colorCount, shownCount);
    for (std::size_t colorIndex = 0; colorIndex < m_colorButtons.size(); ++colorIndex) {
        QToolButton* button = m_colorButtons[colorIndex];
        button->setVisible(colorIndex < shownCount);
        const bool enabled = dynamicData && dynamicData->has_value() && colorIndex < colorCount;
        button->setEnabled(enabled);
        if (!enabled) {
            button->setToolTip(tr("Dynamic color is not available"));
            button->setStyleSheet(colorButtonStyle(0, false));
            continue;
        }

        const uint16_t rgb565 =
            document->dynamicColor(*frameIndex, resolution, dynamicSet(), colorIndex);
        button->setProperty("rgb565", static_cast<int>(rgb565));
        button->setToolTip(tr("Set %1 original %2: 0x%3")
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
        enabled ? tr("Set %1 shadow color: 0x%2")
                      .arg(dynamicSet())
                      .arg(color, 4, 16, QLatin1Char('0'))
                : tr("Dynamic shadow color is not available"));

    // Only where the runtime has an HD picture made of an SD one: an ordinary project with HD.
    const bool upscaledHd = document && !document->rendersHdPlaneOnly() &&
                            !document->editsOriginalPlaneOnly();
    const bool forLibserum29 =
        document && document->libserumTarget() == document::LibserumTarget::Libserum29;
    QSignalBlocker blocker(m_originalOnlyCheckBox);
    m_originalOnlyCheckBox->setVisible(upscaledHd);
    m_originalOnlyCheckBox->setEnabled(enabled && forLibserum29);
    m_originalOnlyCheckBox->setChecked(
        enabled && dynamicSet() < document::kMaxDynamicSetsPerFrame &&
        ((document->originalOnlyDynamicSets(*frameIndex) >> dynamicSet()) & 1u) != 0);
    m_originalOnlyCheckBox->setToolTip(
        forLibserum29
            ? tr("Set %1 colors the SD picture only. On the HD picture of a frame with an HD "
                 "frame of its own, that HD frame shows in the set's zones instead of the "
                 "upscaled set: for pixel art recolored through the set on a frame whose HD "
                 "picture is imported.")
                  .arg(dynamicSet())
            : tr("Needs libserum 2.9 (Project Settings)"));
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
    selectListedSet();
}

uint8_t DynamicPanel::dynamicSet() const
{
    return static_cast<uint8_t>(m_dynamicSetSpinBox->value());
}

}  // namespace colorizing::app
