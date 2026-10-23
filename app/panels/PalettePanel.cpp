#include "panels/PalettePanel.h"

#include <algorithm>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "panels/InspectorForm.h"
#include "ColorGridWidget.h"
#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

using edit::PaletteGradientDirection;
using edit::PaletteGradientPattern;

QImage renderPalettePreviewImage(const document::PaletteRecord& palette)
{
    QImage image(static_cast<int>(document::kPaletteColorCount), 8, QImage::Format_ARGB32);
    image.fill(Qt::black);
    const std::size_t colorCount =
        std::min<std::size_t>(palette.colors.size(), document::kPaletteColorCount);
    for (std::size_t colorIndex = 0; colorIndex < colorCount; ++colorIndex) {
        const QColor color = rgb565ToColor(palette.colors[colorIndex]);
        for (int y = 0; y < image.height(); ++y) {
            image.setPixelColor(static_cast<int>(colorIndex), y, color);
        }
    }
    return image;
}

QIcon previewIcon(const QImage& image, QSize targetSize)
{
    return QIcon(QPixmap::fromImage(image.scaled(targetSize,
                                                 Qt::KeepAspectRatio,
                                                 Qt::FastTransformation)));
}

}  // namespace

PalettePanel::PalettePanel(QWidget* parent)
    : QWidget(parent)
{
    m_paletteComboBox = new QComboBox(this);
    m_paletteComboBox->setToolTip(tr("Selected palette"));
    m_paletteComboBox->setIconSize(QSize(96, 18));
    m_paletteComboBox->setEnabled(false);
    m_nameLineEdit = new QLineEdit(this);
    m_nameLineEdit->setToolTip(tr("Selected palette name"));
    m_nameLineEdit->setMaxLength(63);
    m_nameLineEdit->setEnabled(false);
    m_gradientStartSpinBox = new QSpinBox(this);
    m_gradientStartSpinBox->setToolTip(
        tr("First palette color of a gradient; Ctrl+drag across the colors below sets both "
           "(Shift reverses them)"));
    m_gradientStartSpinBox->setRange(1, static_cast<int>(document::kPaletteColorCount));
    m_gradientStartSpinBox->setEnabled(false);
    m_gradientEndSpinBox = new QSpinBox(this);
    m_gradientEndSpinBox->setToolTip(
        tr("Last palette color of a gradient; Ctrl+drag across the colors below sets both "
           "(Shift reverses them)"));
    m_gradientEndSpinBox->setRange(1, static_cast<int>(document::kPaletteColorCount));
    m_gradientEndSpinBox->setValue(static_cast<int>(document::kPaletteColorCount));
    m_gradientEndSpinBox->setEnabled(false);
    m_gradientShapeComboBox = new QComboBox(this);
    m_gradientShapeComboBox->setToolTip(tr("Shape of the Grad and Sel tools"));
    m_gradientShapeComboBox->addItem(tr("Rectangle"),
                                     static_cast<int>(PaletteGradientShape::Rectangle));
    m_gradientShapeComboBox->addItem(tr("Circle"),
                                     static_cast<int>(PaletteGradientShape::Circle));
    m_gradientShapeComboBox->addItem(tr("Ellipse"),
                                     static_cast<int>(PaletteGradientShape::Ellipse));
    m_gradientShapeComboBox->setEnabled(false);
    m_gradientPatternComboBox = new QComboBox(this);
    m_gradientPatternComboBox->setToolTip(
        tr("Color distribution used by the Grad drawing tool"));
    m_gradientPatternComboBox->addItem(tr("Linear"),
                                       static_cast<int>(PaletteGradientPattern::Linear));
    m_gradientPatternComboBox->addItem(tr("Circle"),
                                       static_cast<int>(PaletteGradientPattern::Circle));
    m_gradientPatternComboBox->addItem(tr("Ellipse"),
                                       static_cast<int>(PaletteGradientPattern::Ellipse));
    m_gradientPatternComboBox->setEnabled(false);
    m_gradientDirectionComboBox = new QComboBox(this);
    m_gradientDirectionComboBox->setToolTip(
        tr("Direction used by the Grad drawing tool"));
    m_gradientDirectionComboBox->addItem(tr("Free"),
                                         static_cast<int>(PaletteGradientDirection::Free));
    m_gradientDirectionComboBox->addItem(
        tr("Left -> Right"),
        static_cast<int>(PaletteGradientDirection::LeftToRight));
    m_gradientDirectionComboBox->addItem(
        tr("Right -> Left"),
        static_cast<int>(PaletteGradientDirection::RightToLeft));
    m_gradientDirectionComboBox->addItem(
        tr("Top -> Bottom"),
        static_cast<int>(PaletteGradientDirection::TopToBottom));
    m_gradientDirectionComboBox->addItem(
        tr("Bottom -> Top"),
        static_cast<int>(PaletteGradientDirection::BottomToTop));
    m_gradientDirectionComboBox->addItem(
        tr("TL -> BR"),
        static_cast<int>(PaletteGradientDirection::TopLeftToBottomRight));
    m_gradientDirectionComboBox->addItem(
        tr("BR -> TL"),
        static_cast<int>(PaletteGradientDirection::BottomRightToTopLeft));
    m_gradientDirectionComboBox->addItem(
        tr("TR -> BL"),
        static_cast<int>(PaletteGradientDirection::TopRightToBottomLeft));
    m_gradientDirectionComboBox->addItem(
        tr("BL -> TR"),
        static_cast<int>(PaletteGradientDirection::BottomLeftToTopRight));
    m_gradientDirectionComboBox->addItem(
        tr("Center -> Edge"),
        static_cast<int>(PaletteGradientDirection::CenterToEdge));
    m_gradientDirectionComboBox->addItem(
        tr("Edge -> Center"),
        static_cast<int>(PaletteGradientDirection::EdgeToCenter));
    m_gradientDirectionComboBox->setEnabled(false);
    m_grid = new ColorGridWidget(document::kPaletteColorCount, 8, this);
    m_grid->setDisplayIndexOffset(1);
    m_grid->setToolTip(tr("Colors in the selected palette"));
    // The Win32 editor's way to pick a gradient's colors: Ctrl+drag across them.
    m_grid->setRangeDraggedHandler([this](std::size_t first, std::size_t last) {
        if (!m_gradientStartSpinBox->isEnabled()) {
            return;
        }
        m_gradientStartSpinBox->setValue(static_cast<int>(first) + 1);
        m_gradientEndSpinBox->setValue(static_cast<int>(last) + 1);
    });
    m_grid->setColorClickedHandler([this](std::size_t colorIndex) {
        if (onColorClicked) {
            onColorClicked(colorIndex);
        }
    });
    m_grid->setColorDoubleClickedHandler([this](std::size_t colorIndex) {
        if (onColorDoubleClicked) {
            onColorDoubleClicked(colorIndex);
        }
    });
    m_applyGradientButton = new QToolButton(this);
    m_applyGradientButton->setText(tr("Apply Gradient"));
    m_applyGradientButton->setToolTip(
        tr("Fill the selected palette range between the endpoint colors"));
    m_applyGradientButton->setEnabled(false);
    m_fillSelectionButton = new QToolButton(this);
    m_fillSelectionButton->setText(tr("Grad Sel"));
    m_fillSelectionButton->setToolTip(
        tr("Fill the current canvas selection with the palette gradient"));
    m_fillSelectionButton->setEnabled(false);
    m_selectionDirectionButton = new QToolButton(this);
    m_selectionDirectionButton->setText(tr("Set Dir"));
    m_selectionDirectionButton->setToolTip(
        tr("Draw the free gradient direction line for the current selection"));
    m_selectionDirectionButton->setCheckable(true);
    m_selectionDirectionButton->setEnabled(false);
    m_clearSelectionButton = new QToolButton(this);
    m_clearSelectionButton->setText(tr("Clear Sel"));
    m_clearSelectionButton->setToolTip(tr("Clear the current canvas selection"));
    m_clearSelectionButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_selectedPaletteLabel = new QLabel(tr("Selected: None"), this);
    m_selectedPaletteLabel->setWordWrap(true);
    pageLayout->addWidget(m_selectedPaletteLabel);
    auto* form = makeInspectorForm();
    form->addRow(tr("Palette"), m_paletteComboBox);
    form->addRow(tr("Name"), m_nameLineEdit);
    form->addRow(tr("From"), m_gradientStartSpinBox);
    form->addRow(tr("To"), m_gradientEndSpinBox);
    form->addRow(tr("Shape"), m_gradientShapeComboBox);
    form->addRow(tr("Pattern"), m_gradientPatternComboBox);
    form->addRow(tr("Direction"), m_gradientDirectionComboBox);
    pageLayout->addLayout(form);
    pageLayout->addWidget(m_grid);
    pageLayout->addWidget(m_applyGradientButton);
    auto* selectionFillLayout = new QHBoxLayout();
    selectionFillLayout->addWidget(m_fillSelectionButton);
    selectionFillLayout->addWidget(m_selectionDirectionButton);
    selectionFillLayout->addWidget(m_clearSelectionButton);
    pageLayout->addLayout(selectionFillLayout);
    pageLayout->addStretch(1);

    connect(m_paletteComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onPaletteSelectionChanged) {
                    onPaletteSelectionChanged();
                }
            });
    for (QComboBox* comboBox : {m_gradientShapeComboBox,
                                m_gradientPatternComboBox,
                                m_gradientDirectionComboBox}) {
        connect(comboBox,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this,
                [this](int) {
                    if (onGradientSettingsChanged) {
                        onGradientSettingsChanged();
                    }
                });
    }
    connect(m_nameLineEdit, &QLineEdit::editingFinished, this, [this]() {
        if (onNameEdited) {
            onNameEdited(m_nameLineEdit->text());
        }
    });
    connect(m_applyGradientButton, &QToolButton::clicked, this, [this]() {
        if (onApplyGradientRequested) {
            onApplyGradientRequested();
        }
    });
    connect(m_fillSelectionButton, &QToolButton::clicked, this, [this]() {
        if (onFillSelectionRequested) {
            onFillSelectionRequested();
        }
    });
    connect(m_selectionDirectionButton, &QToolButton::clicked, this, [this]() {
        if (onSelectionDirectionClicked) {
            onSelectionDirectionClicked();
        }
    });
    connect(m_clearSelectionButton, &QToolButton::clicked, this, [this]() {
        if (onClearSelectionRequested) {
            onClearSelectionRequested();
        }
    });
}

void PalettePanel::setPalettes(const document::ProjectDocument* document)
{
    const std::optional<std::size_t> previousPaletteIndex = selectedPaletteIndex(document);
    QSignalBlocker blocker(m_paletteComboBox);
    m_paletteComboBox->clear();
    if (!document) {
        return;
    }
    for (std::size_t paletteIndex = 0; paletteIndex < document->paletteCount(); ++paletteIndex) {
        const document::PaletteRecord& palette = document->palette(paletteIndex);
        const QString paletteLabel =
            palette.name.empty()
                ? QString::number(static_cast<int>(paletteIndex))
                : QStringLiteral("%1 - %2")
                      .arg(static_cast<int>(paletteIndex))
                      .arg(QString::fromStdString(palette.name));
        m_paletteComboBox->addItem(previewIcon(renderPalettePreviewImage(palette), QSize(96, 18)),
                                   paletteLabel,
                                   static_cast<int>(paletteIndex));
        if (previousPaletteIndex && *previousPaletteIndex == paletteIndex) {
            m_paletteComboBox->setCurrentIndex(m_paletteComboBox->count() - 1);
        }
    }
}

void PalettePanel::ensurePaletteSelected(const document::ProjectDocument* document)
{
    if (!document) {
        return;
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex(document);
    QSignalBlocker blocker(m_paletteComboBox);
    if (!paletteIndex) {
        m_paletteComboBox->setCurrentIndex(m_paletteComboBox->count() > 0 ? 0 : -1);
        return;
    }
    for (int row = 0; row < m_paletteComboBox->count(); ++row) {
        if (m_paletteComboBox->itemData(row).toInt() == static_cast<int>(*paletteIndex)) {
            m_paletteComboBox->setCurrentIndex(row);
            return;
        }
    }
    m_paletteComboBox->setCurrentIndex(-1);
}

void PalettePanel::refresh(const document::ProjectDocument* document,
                           bool hasCanvasSelection,
                           bool selectionDirectionMode)
{
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex(document);
    QSignalBlocker nameBlocker(m_nameLineEdit);
    QSignalBlocker startBlocker(m_gradientStartSpinBox);
    QSignalBlocker endBlocker(m_gradientEndSpinBox);
    if (!document || !paletteIndex) {
        m_selectedPaletteLabel->setText(tr("Selected: None"));
        m_paletteComboBox->setEnabled(false);
        m_nameLineEdit->clear();
        m_nameLineEdit->setEnabled(false);
        m_gradientStartSpinBox->setEnabled(false);
        m_gradientEndSpinBox->setEnabled(false);
        // The Sel tool draws with the shape too, palette or not.
        m_gradientShapeComboBox->setEnabled(document != nullptr);
        m_gradientPatternComboBox->setEnabled(false);
        m_gradientDirectionComboBox->setEnabled(false);
        m_applyGradientButton->setEnabled(false);
        m_fillSelectionButton->setEnabled(false);
        m_selectionDirectionButton->setEnabled(false);
        setSelectionDirectionChecked(false);
        m_clearSelectionButton->setEnabled(false);
        m_grid->setColors({}, 0);
        return;
    }

    const document::PaletteRecord& palette = document->palette(*paletteIndex);
    m_selectedPaletteLabel->setText(
        palette.name.empty()
            ? tr("Selected: %1").arg(static_cast<int>(*paletteIndex))
            : tr("Selected: %1 - %2")
                  .arg(static_cast<int>(*paletteIndex))
                  .arg(QString::fromStdString(palette.name)));
    m_paletteComboBox->setEnabled(true);
    m_nameLineEdit->setText(QString::fromStdString(palette.name));
    m_nameLineEdit->setEnabled(true);
    m_gradientStartSpinBox->setEnabled(true);
    m_gradientEndSpinBox->setEnabled(true);
    m_gradientShapeComboBox->setEnabled(true);
    m_gradientPatternComboBox->setEnabled(true);
    m_gradientDirectionComboBox->setEnabled(true);
    m_applyGradientButton->setEnabled(palette.colors.size() >= 2);
    m_fillSelectionButton->setEnabled(hasCanvasSelection);
    m_selectionDirectionButton->setEnabled(hasCanvasSelection);
    setSelectionDirectionChecked(selectionDirectionMode && hasCanvasSelection);
    m_clearSelectionButton->setEnabled(hasCanvasSelection);
    m_grid->setColors(palette.colors, palette.colors.size());
}

void PalettePanel::setSelectionDirectionChecked(bool checked)
{
    QSignalBlocker blocker(m_selectionDirectionButton);
    m_selectionDirectionButton->setChecked(checked);
}

std::optional<std::size_t> PalettePanel::selectedPaletteIndex(
    const document::ProjectDocument* document) const
{
    if (!document || m_paletteComboBox->currentIndex() < 0) {
        return std::nullopt;
    }
    const int paletteIndex = m_paletteComboBox->currentData().toInt();
    if (paletteIndex < 0 ||
        static_cast<std::size_t>(paletteIndex) >= document->paletteCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(paletteIndex);
}

std::pair<int, int> PalettePanel::gradientEndpoints() const
{
    m_gradientStartSpinBox->interpretText();
    m_gradientEndSpinBox->interpretText();
    return {m_gradientStartSpinBox->value() - 1, m_gradientEndSpinBox->value() - 1};
}

PaletteGradientShape PalettePanel::gradientShape() const
{
    if (m_gradientShapeComboBox->currentIndex() < 0) {
        return PaletteGradientShape::Rectangle;
    }
    const int shape = m_gradientShapeComboBox->currentData().toInt();
    if (shape == static_cast<int>(PaletteGradientShape::Circle)) {
        return PaletteGradientShape::Circle;
    }
    if (shape == static_cast<int>(PaletteGradientShape::Ellipse)) {
        return PaletteGradientShape::Ellipse;
    }
    return PaletteGradientShape::Rectangle;
}

PaletteGradientDirection PalettePanel::gradientDirection() const
{
    if (m_gradientDirectionComboBox->currentIndex() < 0) {
        return PaletteGradientDirection::LeftToRight;
    }
    const int direction = m_gradientDirectionComboBox->currentData().toInt();
    switch (static_cast<PaletteGradientDirection>(direction)) {
        case PaletteGradientDirection::Free:
        case PaletteGradientDirection::RightToLeft:
        case PaletteGradientDirection::TopToBottom:
        case PaletteGradientDirection::BottomToTop:
        case PaletteGradientDirection::TopLeftToBottomRight:
        case PaletteGradientDirection::BottomRightToTopLeft:
        case PaletteGradientDirection::TopRightToBottomLeft:
        case PaletteGradientDirection::BottomLeftToTopRight:
        case PaletteGradientDirection::CenterToEdge:
        case PaletteGradientDirection::EdgeToCenter:
            return static_cast<PaletteGradientDirection>(direction);
        case PaletteGradientDirection::LeftToRight:
            break;
    }
    return PaletteGradientDirection::LeftToRight;
}

PaletteGradientPattern PalettePanel::gradientPattern() const
{
    if (m_gradientPatternComboBox->currentIndex() < 0) {
        return PaletteGradientPattern::Linear;
    }
    const int pattern = m_gradientPatternComboBox->currentData().toInt();
    if (pattern == static_cast<int>(PaletteGradientPattern::Circle)) {
        return PaletteGradientPattern::Circle;
    }
    if (pattern == static_cast<int>(PaletteGradientPattern::Ellipse)) {
        return PaletteGradientPattern::Ellipse;
    }
    return PaletteGradientPattern::Linear;
}

}  // namespace colorizing::app
