#include "dialogs/ImageImportDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "MovieImport.h"
#include "VideoClip.h"

namespace colorizing::app {

namespace {

const QColor kStartZoneColor(255, 64, 200);
const QColor kEndZoneColor(64, 160, 255);

QString timeText(double ms)
{
    const auto total = static_cast<long long>(std::max(0.0, ms));
    return QStringLiteral("%1:%2:%3.%4")
        .arg(total / 3600000)
        .arg((total / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((total / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 1000, 3, 10, QLatin1Char('0'));
}

QSlider* makeSlider(QWidget* parent, int minimum, int maximum)
{
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(minimum, maximum);
    slider->setValue(0);
    return slider;
}

}  // namespace

// ---- ImageZoneView ---------------------------------------------------------------------

ImageZoneView::ImageZoneView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(480, 240);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void ImageZoneView::show(const QImage& picture,
                         const QRect& zone,
                         const std::optional<QRect>& endZone,
                         const std::optional<QRect>& floating)
{
    m_picture = picture;
    m_zone = zone;
    m_endZone = endZone;
    m_floating = floating;
    update();
}

QRectF ImageZoneView::pictureRect() const
{
    if (m_picture.isNull()) {
        return {};
    }
    const QSizeF size =
        QSizeF(m_picture.size()).scaled(QSizeF(this->size()), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0),
                  size);
}

double ImageZoneView::scale() const
{
    return m_picture.isNull() ? 1.0 : pictureRect().width() / m_picture.width();
}

QPoint ImageZoneView::toPicture(const QPointF& point) const
{
    const QRectF picture = pictureRect();
    const double factor = scale();
    return QPoint(std::clamp(static_cast<int>((point.x() - picture.left()) / factor), 0,
                             std::max(0, m_picture.width() - 1)),
                  std::clamp(static_cast<int>((point.y() - picture.top()) / factor), 0,
                             std::max(0, m_picture.height() - 1)));
}

QRectF ImageZoneView::toView(const QRect& zone) const
{
    const QRectF picture = pictureRect();
    const double factor = scale();
    return QRectF(picture.left() + zone.x() * factor, picture.top() + zone.y() * factor,
                  zone.width() * factor, zone.height() * factor);
}

void ImageZoneView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_picture.isNull()) {
        return;
    }
    const QRectF target = pictureRect();
    painter.drawImage(target, m_picture);
    // Outside the start zone dimmed.
    QPainterPath outside;
    outside.addRect(target);
    QPainterPath inside;
    inside.addRect(toView(m_zone));
    painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 140));
    painter.setBrush(Qt::NoBrush);
    if (m_endZone) {
        painter.setPen(QPen(kEndZoneColor, 2));
        painter.drawRect(toView(*m_endZone));
    }
    painter.setPen(QPen(kStartZoneColor, 2));
    painter.drawRect(toView(m_zone));
    if (m_floating) {
        painter.setPen(QPen(QColor(255, 255, 255, 200), 1, Qt::DashLine));
        painter.drawRect(toView(*m_floating));
    }
}

void ImageZoneView::mouseMoveEvent(QMouseEvent* event)
{
    if (onHover && !m_picture.isNull()) {
        onHover(toPicture(event->position()));
    }
}

void ImageZoneView::mousePressEvent(QMouseEvent* event)
{
    if (!onClick || m_picture.isNull()) {
        return;
    }
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        onClick(toPicture(event->position()), event->button() == Qt::RightButton);
        event->accept();
    }
}

void ImageZoneView::wheelEvent(QWheelEvent* event)
{
    // Notches of 120; a trackpad's small steps add up.
    m_wheelRemainder += event->angleDelta().y() != 0 ? event->angleDelta().y()
                                                      : event->angleDelta().x();
    const int notches = m_wheelRemainder / 120;
    m_wheelRemainder -= notches * 120;
    if (notches != 0 && onWheel) {
        onWheel(notches, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    event->accept();
}

void ImageZoneView::leaveEvent(QEvent* event)
{
    if (onLeave) {
        onLeave();
    }
    QWidget::leaveEvent(event);
}

// ---- ImageImportDialog -------------------------------------------------------------------

ImageImportDialog::ImageImportDialog(const QImage& picture,
                                     const QString& name,
                                     ImageImportSetup setup,
                                     QWidget* parent)
    : QDialog(parent)
    , m_setup(std::move(setup))
    , m_picture(picture.convertToFormat(QImage::Format_RGB32))
{
    setWindowTitle(tr("Import Image: %1").arg(name));
    build(name);
}

ImageImportDialog::ImageImportDialog(VideoClip& clip,
                                     const QString& name,
                                     ImageImportSetup setup,
                                     QWidget* parent)
    : QDialog(parent)
    , m_clip(&clip)
    , m_setup(std::move(setup))
{
    setWindowTitle(tr("Import Movie Clip: %1").arg(name));
    if (clip.frameRate() > 0.0) {
        m_frameStepMs = 1000.0 / clip.frameRate();
    }
    m_picture = clip.frameAt(0.0).convertToFormat(QImage::Format_RGB32);
    build(name);
}

void ImageImportDialog::build(const QString&)
{
    m_view = new ImageZoneView(this);
    m_view->setToolTip(tr("The zone follows the mouse; the mouse wheel makes it larger or "
                          "smaller (with Shift ten times as fast). A click takes it (pink); a "
                          "right click takes it as the end zone of a scroll over the selected "
                          "frames (blue)."));
    m_view->onHover = [this](QPoint at) { hover(at); };
    m_view->onLeave = [this]() {
        m_hoverAt.reset();
        m_floating.reset();
        updateView();
    };
    m_view->onWheel = [this](int notches, bool fast) { wheel(notches, fast); };
    m_view->onClick = [this](QPoint at, bool end) { click(at, end); };

    m_zoneLabel = new QLabel(this);
    auto* wholeButton = new QPushButton(tr("Whole Picture"), this);
    wholeButton->setToolTip(tr("The largest zone of the selection's proportions"));
    auto* noEndButton = new QPushButton(tr("No End Zone"), this);
    noEndButton->setToolTip(tr("No scroll: every frame gets the pink zone"));
    auto* zoneRow = new QHBoxLayout;
    zoneRow->addWidget(m_zoneLabel, 1);
    zoneRow->addWidget(wholeButton);
    zoneRow->addWidget(noEndButton);

    // Saved zones, kept in the project.
    m_savedZones = m_setup.savedZones;
    m_savedZoneComboBox = new QComboBox(this);
    m_savedZoneComboBox->setMinimumContentsLength(16);
    m_savedZoneComboBox->setToolTip(tr("A saved zone: its zones and the picture's settings"));
    m_savedZoneNameEdit = new QLineEdit(this);
    m_savedZoneNameEdit->setPlaceholderText(tr("Name"));
    m_savedZoneNameEdit->setMaxLength(63);
    auto* saveZoneButton = new QPushButton(tr("Save Zone"), this);
    saveZoneButton->setToolTip(tr("Keeps the zones and the picture's settings in the project "
                                  "under the name (replacing a zone of that name)"));
    auto* deleteZoneButton = new QPushButton(tr("Delete Zone"), this);
    deleteZoneButton->setToolTip(tr("Removes the saved zone chosen in the list"));
    auto* savedRow = new QHBoxLayout;
    savedRow->addWidget(new QLabel(tr("Saved zones:"), this));
    savedRow->addWidget(m_savedZoneComboBox, 1);
    savedRow->addWidget(m_savedZoneNameEdit, 1);
    savedRow->addWidget(saveZoneButton);
    savedRow->addWidget(deleteZoneButton);
    connect(m_savedZoneComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
            this, [this](int index) {
                if (index > 0) {
                    applySavedZone(static_cast<std::size_t>(index - 1));
                }
            });
    connect(saveZoneButton, &QPushButton::clicked, this,
            [this]() { saveZone(m_savedZoneNameEdit->text()); });
    connect(deleteZoneButton, &QPushButton::clicked, this, [this]() {
        if (m_savedZoneComboBox->currentIndex() > 0) {
            deleteSavedZone(static_cast<std::size_t>(m_savedZoneComboBox->currentIndex() - 1));
        }
    });
    fillSavedZoneList();

    // Adjustments, as the Win32 editor's sliders.
    QSettings settings;
    auto* adjustGroup = new QGroupBox(tr("Picture"), this);
    m_brightnessSlider = makeSlider(adjustGroup, -50, 50);
    m_contrastSlider = makeSlider(adjustGroup, -15, 20);
    m_blurSlider = makeSlider(adjustGroup, 0, 10);
    m_brightnessLabel = new QLabel(adjustGroup);
    m_contrastLabel = new QLabel(adjustGroup);
    m_blurLabel = new QLabel(adjustGroup);
    m_filterComboBox = new QComboBox(adjustGroup);
    m_filterComboBox->addItem(tr("Nearest"), static_cast<int>(ImageScaleFilter::Nearest));
    m_filterComboBox->addItem(tr("Smooth"), static_cast<int>(ImageScaleFilter::Smooth));
    m_filterComboBox->setToolTip(tr("How the zone is scaled to the selection: Nearest takes one "
                                    "pixel of the picture each, Smooth mixes the pixels"));
    auto* adjustLayout = new QFormLayout(adjustGroup);
    const auto sliderRow = [adjustGroup](QSlider* slider, QLabel* label) {
        auto* row = new QWidget(adjustGroup);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(slider, 1);
        label->setMinimumWidth(40);
        layout->addWidget(label);
        return row;
    };
    adjustLayout->addRow(tr("Brightness"), sliderRow(m_brightnessSlider, m_brightnessLabel));
    adjustLayout->addRow(tr("Contrast"), sliderRow(m_contrastSlider, m_contrastLabel));
    adjustLayout->addRow(tr("Blur"), sliderRow(m_blurSlider, m_blurLabel));
    adjustLayout->addRow(tr("Scaling"), m_filterComboBox);
    m_brightnessSlider->setValue(settings.value(QStringLiteral("imageImport/brightness"), 0).toInt());
    m_contrastSlider->setValue(settings.value(QStringLiteral("imageImport/contrast"), 0).toInt());
    m_blurSlider->setValue(settings.value(QStringLiteral("imageImport/blur"), 0).toInt());
    m_filterComboBox->setCurrentIndex(
        std::max(0, m_filterComboBox->findData(
                        settings.value(QStringLiteral("imageImport/filter"), 0).toInt())));

    // Where it goes.
    auto* targetGroup = new QGroupBox(tr("Into"), this);
    m_framesRadio = new QRadioButton(
        m_setup.durationsMs.size() > 1
            ? tr("The selection of the %n selected frames", "",
                 static_cast<int>(m_setup.durationsMs.size()))
            : tr("The selection of the frame"),
        targetGroup);
    m_backgroundRadio = new QRadioButton(
        m_setup.backgroundName ? tr("Background %1").arg(*m_setup.backgroundName)
                               : tr("A background (none is selected)"),
        targetGroup);
    m_backgroundRadio->setEnabled(m_setup.backgroundName.has_value());
    (m_setup.toBackground && m_setup.backgroundName ? m_backgroundRadio : m_framesRadio)
        ->setChecked(true);
    m_firstPlaneCheckBox = new QCheckBox(m_setup.firstPlaneName, targetGroup);
    m_firstPlaneCheckBox->setChecked(m_setup.firstPlaneChecked);
    m_secondPlaneCheckBox = new QCheckBox(
        tr("%1 (created where it is missing)").arg(m_setup.secondPlaneName), targetGroup);
    m_secondPlaneCheckBox->setChecked(m_setup.secondPlaneChecked);
    m_secondPlaneCheckBox->setVisible(!m_setup.secondPlaneName.isEmpty());
    auto* targetLayout = new QVBoxLayout(targetGroup);
    targetLayout->addWidget(m_framesRadio);
    targetLayout->addWidget(m_backgroundRadio);
    auto* planeRow = new QHBoxLayout;
    planeRow->addWidget(m_firstPlaneCheckBox);
    planeRow->addWidget(m_secondPlaneCheckBox);
    planeRow->addStretch(1);
    targetLayout->addLayout(planeRow);

    m_resultLabel = new QLabel(this);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    m_resultLabel->setMinimumHeight(100);
    auto* resultGroup = new QGroupBox(tr("The selection gets"), this);
    auto* resultLayout = new QVBoxLayout(resultGroup);
    resultLayout->addWidget(m_resultLabel);

    auto* settingsRow = new QHBoxLayout;
    settingsRow->addWidget(adjustGroup, 1);
    settingsRow->addWidget(targetGroup, 1);
    settingsRow->addWidget(resultGroup, 1);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        QSettings stored;
        stored.setValue(QStringLiteral("imageImport/brightness"), m_brightnessSlider->value());
        stored.setValue(QStringLiteral("imageImport/contrast"), m_contrastSlider->value());
        stored.setValue(QStringLiteral("imageImport/blur"), m_blurSlider->value());
        stored.setValue(QStringLiteral("imageImport/filter"), m_filterComboBox->currentData());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addLayout(zoneRow);
    layout->addLayout(savedRow);

    if (m_clip) {
        // Where in the clip, stepped by a picture, a second, a minute or an hour.
        m_timeSlider = new QSlider(Qt::Horizontal, this);
        m_timeSlider->setRange(0, static_cast<int>(std::max(0.0, m_clip->durationMs())));
        m_timeSlider->setToolTip(tr("Where in the clip the first selected frame starts"));
        m_timeLabel = new QLabel(this);
        auto* previousButton = new QPushButton(tr("◀"), this);
        previousButton->setToolTip(tr("One step back"));
        auto* nextButton = new QPushButton(tr("▶"), this);
        nextButton->setToolTip(tr("One step on"));
        m_unitGroup = new QButtonGroup(this);
        auto* unitRow = new QHBoxLayout;
        unitRow->addWidget(new QLabel(tr("Step:"), this));
        const std::pair<StepUnit, QString> units[] = {{StepUnit::Hour, tr("Hour")},
                                                      {StepUnit::Minute, tr("Minute")},
                                                      {StepUnit::Second, tr("Second")},
                                                      {StepUnit::Frame, tr("Picture")}};
        for (const auto& [unit, text] : units) {
            auto* radio = new QRadioButton(text, this);
            m_unitGroup->addButton(radio, static_cast<int>(unit));
            unitRow->addWidget(radio);
        }
        m_unitGroup->button(static_cast<int>(StepUnit::Frame))->setChecked(true);
        auto* timeRow = new QHBoxLayout;
        timeRow->addWidget(previousButton);
        timeRow->addWidget(m_timeSlider, 1);
        timeRow->addWidget(nextButton);
        timeRow->addWidget(m_timeLabel);
        timeRow->addLayout(unitRow);
        layout->addLayout(timeRow);

        auto* batchGroup = new QGroupBox(tr("The next selected frames get"), this);
        m_playRadio = new QRadioButton(tr("The clip as the frames play (their durations)"),
                                       batchGroup);
        m_intervalRadio = new QRadioButton(tr("A picture every"), batchGroup);
        m_intervalSpinBox = new QSpinBox(batchGroup);
        m_intervalSpinBox->setRange(5, 3000);
        m_intervalSpinBox->setValue(std::max(5, static_cast<int>(std::lround(m_frameStepMs))));
        m_intervalSpinBox->setSuffix(tr(" ms"));
        m_sameRadio = new QRadioButton(tr("This picture, all of them"), batchGroup);
        m_playRadio->setChecked(true);
        auto* intervalRow = new QHBoxLayout;
        intervalRow->addWidget(m_intervalRadio);
        intervalRow->addWidget(m_intervalSpinBox);
        intervalRow->addStretch(1);
        auto* batchLayout = new QHBoxLayout(batchGroup);
        batchLayout->addWidget(m_playRadio);
        batchLayout->addLayout(intervalRow);
        batchLayout->addWidget(m_sameRadio);
        layout->addWidget(batchGroup);

        connect(m_timeSlider, &QSlider::valueChanged, this, [this](int) { sourceChanged(); });
        connect(previousButton, &QPushButton::clicked, this, [this]() { step(-1); });
        connect(nextButton, &QPushButton::clicked, this, [this]() { step(1); });
        connect(m_unitGroup, &QButtonGroup::idClicked, this,
                [this](int id) { m_stepUnit = static_cast<StepUnit>(id); });
        for (QRadioButton* radio : {m_playRadio, m_intervalRadio, m_sameRadio}) {
            connect(radio, &QRadioButton::toggled, this, [this](bool) { updateResult(); });
        }
        connect(m_intervalSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this, [this](int) {
                    m_intervalRadio->setChecked(true);
                    updateResult();
                });
    }
    layout->addLayout(settingsRow);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(buttons);

    for (QSlider* slider : {m_brightnessSlider, m_contrastSlider, m_blurSlider}) {
        connect(slider, &QSlider::valueChanged, this, [this](int) { adjustmentsChanged(); });
    }
    connect(m_filterComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateResult(); });
    connect(m_framesRadio, &QRadioButton::toggled, this, [this](bool) { updateResult(); });
    for (QCheckBox* box : {m_firstPlaneCheckBox, m_secondPlaneCheckBox}) {
        connect(box, &QCheckBox::toggled, this, [this](bool) { updateResult(); });
    }
    connect(wholeButton, &QPushButton::clicked, this,
            [this]() { setZone(fittingZone(pictureSize(), proportions())); });
    connect(noEndButton, &QPushButton::clicked, this, [this]() { setEndZone(std::nullopt); });

    m_zone = fittingZone(pictureSize(), proportions());
    m_floatingWidth = m_zone.width();
    resize(860, 720);
    adjustmentsChanged();
}

double ImageImportDialog::proportions() const
{
    const render::Size size = m_setup.selectionSize;
    return size.height > 0 ? static_cast<double>(size.width) / size.height : 4.0;
}

QSize ImageImportDialog::pictureSize() const
{
    return m_clip ? m_clip->size() : m_picture.size();
}

void ImageImportDialog::setZone(const QRect& zone)
{
    const QRect inside = zone.intersected(QRect(QPoint(0, 0), pictureSize()));
    m_zone = inside.isEmpty() ? fittingZone(pictureSize(), proportions()) : inside;
    m_floatingWidth = m_zone.width();
    updateView();
    updateResult();
}

void ImageImportDialog::setEndZone(const std::optional<QRect>& zone)
{
    m_endZone = zone ? std::optional<QRect>(zone->intersected(QRect(QPoint(0, 0), pictureSize())))
                     : std::nullopt;
    if (m_endZone && m_endZone->isEmpty()) {
        m_endZone.reset();
    }
    updateView();
    updateResult();
}

QRect ImageImportDialog::zoneFor(std::size_t index, std::size_t count) const
{
    return m_endZone ? zoneBetween(m_zone, *m_endZone, index, count) : m_zone;
}

ImageAdjustments ImageImportDialog::adjustments() const
{
    return ImageAdjustments{m_brightnessSlider->value(), m_contrastSlider->value(),
                            m_blurSlider->value()};
}

void ImageImportDialog::setAdjustments(const ImageAdjustments& adjustments)
{
    m_brightnessSlider->setValue(adjustments.brightness);
    m_contrastSlider->setValue(adjustments.contrast);
    m_blurSlider->setValue(adjustments.blur);
}

ImageScaleFilter ImageImportDialog::filter() const
{
    return static_cast<ImageScaleFilter>(m_filterComboBox->currentData().toInt());
}

void ImageImportDialog::setFilter(ImageScaleFilter filter)
{
    m_filterComboBox->setCurrentIndex(m_filterComboBox->findData(static_cast<int>(filter)));
}

bool ImageImportDialog::toBackground() const
{
    return m_backgroundRadio->isChecked();
}

void ImageImportDialog::setToBackground(bool background)
{
    (background && m_backgroundRadio->isEnabled() ? m_backgroundRadio : m_framesRadio)
        ->setChecked(true);
}

bool ImageImportDialog::toFirstPlane() const
{
    return m_firstPlaneCheckBox->isChecked();
}

bool ImageImportDialog::toSecondPlane() const
{
    return !m_setup.secondPlaneName.isEmpty() && m_secondPlaneCheckBox->isChecked();
}

void ImageImportDialog::setPlanes(bool first, bool second)
{
    m_firstPlaneCheckBox->setChecked(first);
    m_secondPlaneCheckBox->setChecked(second);
}

double ImageImportDialog::startMs() const
{
    return m_timeSlider ? m_timeSlider->value() : 0.0;
}

void ImageImportDialog::setStartMs(double ms)
{
    if (m_timeSlider) {
        m_timeSlider->setValue(static_cast<int>(std::lround(std::max(0.0, ms))));
    }
}

void ImageImportDialog::setStepUnit(StepUnit unit)
{
    m_stepUnit = unit;
    if (m_unitGroup) {
        m_unitGroup->button(static_cast<int>(unit))->setChecked(true);
    }
}

double ImageImportDialog::stepMs() const
{
    switch (m_stepUnit) {
        case StepUnit::Hour:
            return 3600000.0;
        case StepUnit::Minute:
            return 60000.0;
        case StepUnit::Second:
            return 1000.0;
        case StepUnit::Frame:
            break;
    }
    return m_frameStepMs;
}

void ImageImportDialog::step(int direction)
{
    setStartMs(startMs() + direction * stepMs());
}

ImageImportDialog::Batch ImageImportDialog::batch() const
{
    if (!m_clip || m_playRadio->isChecked()) {
        return Batch::AsTheyPlay;
    }
    return m_intervalRadio->isChecked() ? Batch::Interval : Batch::SamePicture;
}

void ImageImportDialog::setBatch(Batch batch, int intervalMs)
{
    if (!m_clip) {
        return;
    }
    if (batch == Batch::Interval && intervalMs > 0) {
        m_intervalSpinBox->setValue(intervalMs);
    }
    (batch == Batch::AsTheyPlay ? m_playRadio
                                : batch == Batch::Interval ? m_intervalRadio : m_sameRadio)
        ->setChecked(true);
    updateResult();
}

std::vector<double> ImageImportDialog::frameTimes() const
{
    if (!m_clip) {
        return {};
    }
    switch (batch()) {
        case Batch::SamePicture:
            return std::vector<double>(m_setup.durationsMs.size(), startMs());
        case Batch::Interval:
            return movieFrameTimes(startMs(), m_setup.durationsMs, m_intervalSpinBox->value());
        case Batch::AsTheyPlay:
            break;
    }
    return movieFrameTimes(startMs(), m_setup.durationsMs, 0);
}

QImage ImageImportDialog::resultPicture() const
{
    const render::Size size = m_setup.selectionSize;
    return zonePicture(m_adjusted, m_zone, QSize(size.width, size.height), filter());
}

void ImageImportDialog::sourceChanged()
{
    if (m_clip) {
        m_picture = m_clip->frameAt(startMs()).convertToFormat(QImage::Format_RGB32);
        m_timeLabel->setText(
            tr("%1 of %2").arg(timeText(startMs()), timeText(m_clip->durationMs())));
    }
    adjustmentsChanged();
}

void ImageImportDialog::adjustmentsChanged()
{
    const ImageAdjustments values = adjustments();
    m_brightnessLabel->setText(QString::number(values.brightness));
    m_contrastLabel->setText(QString::number(1.0 + 0.05 * values.contrast, 'f', 2));
    m_blurLabel->setText(QString::number(values.blur));
    m_adjusted = adjustedImage(m_picture, values);
    updateView();
    updateResult();
}

void ImageImportDialog::updateView()
{
    m_view->show(m_adjusted, m_zone, m_endZone, m_floating);
    const QSize picture = pictureSize();
    m_zoneLabel->setText(tr("Zone: %1, %2, %3x%4 of %5x%6 pixels%7")
                             .arg(m_zone.x())
                             .arg(m_zone.y())
                             .arg(m_zone.width())
                             .arg(m_zone.height())
                             .arg(picture.width())
                             .arg(picture.height())
                             .arg(m_endZone ? tr("; scrolls to %1, %2, %3x%4")
                                                  .arg(m_endZone->x())
                                                  .arg(m_endZone->y())
                                                  .arg(m_endZone->width())
                                                  .arg(m_endZone->height())
                                            : QString()));
}

void ImageImportDialog::updateResult()
{
    QImage result = resultPicture();
    if (!result.isNull() && !m_setup.selectionMask.empty() &&
        m_setup.selectionMask.size() ==
            static_cast<std::size_t>(result.width()) * static_cast<std::size_t>(result.height())) {
        // Pixels outside the selection keep theirs: shown dark.
        for (int y = 0; y < result.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                if (m_setup.selectionMask[static_cast<std::size_t>(y) * result.width() + x] == 0) {
                    line[x] = qRgb(qRed(line[x]) / 5, qGreen(line[x]) / 5, qBlue(line[x]) / 5);
                }
            }
        }
    }
    if (result.isNull()) {
        m_resultLabel->clear();
    } else {
        const int factor = std::max(1, std::min(240 / std::max(1, result.width()),
                                                120 / std::max(1, result.height())));
        m_resultLabel->setPixmap(QPixmap::fromImage(
            result.scaled(result.size() * factor, Qt::KeepAspectRatio, Qt::FastTransformation)));
    }
    QStringList parts;
    const std::size_t frames = toBackground() ? 1 : m_setup.durationsMs.size();
    if (!toFirstPlane() && !toSecondPlane()) {
        parts << tr("Choose a plane to import into.");
    } else if (toBackground()) {
        parts << tr("The background gets the zone.");
    } else if (m_clip) {
        const std::vector<double> times = frameTimes();
        if (!times.empty()) {
            parts << (times.back() > m_clip->durationMs()
                          ? tr("%n frame(s) get the clip from %1 to %2; it ends at %3, the "
                               "frames after that are left as they are.",
                               "", static_cast<int>(times.size()))
                                .arg(timeText(times.front()), timeText(times.back()),
                                     timeText(m_clip->durationMs()))
                          : tr("%n frame(s) get the clip from %1 to %2.", "",
                               static_cast<int>(times.size()))
                                .arg(timeText(times.front()), timeText(times.back())));
        }
    } else {
        parts << tr("%n frame(s) get the zone.", "", static_cast<int>(frames));
    }
    if (m_endZone && frames > 1) {
        parts << tr("The zone scrolls from the pink one to the blue one over the frames.");
    }
    m_summaryLabel->setText(parts.join(QLatin1Char(' ')));
}

void ImageImportDialog::fillSavedZoneList()
{
    m_savedZoneComboBox->clear();
    m_savedZoneComboBox->addItem(m_savedZones.empty() ? tr("(none saved)") : tr("(choose)"));
    for (const document::SavedImageZone& zone : m_savedZones) {
        m_savedZoneComboBox->addItem(QString::fromStdString(zone.name));
    }
}

document::PictureZone ImageImportDialog::toPictureZone(const QRect& zone) const
{
    const QSize picture = pictureSize();
    const auto part = [](int value, int whole) {
        return static_cast<uint32_t>(
            std::lround(1000000.0 * value / std::max(1, whole)));
    };
    return document::PictureZone{part(zone.x(), picture.width()), part(zone.y(), picture.height()),
                                 part(zone.width(), picture.width()),
                                 part(zone.height(), picture.height())};
}

QRect ImageImportDialog::fromPictureZone(const document::PictureZone& zone) const
{
    // Its width and middle in this picture, with the selection's proportions.
    const QSize picture = pictureSize();
    const auto pixels = [](uint32_t part, int whole) { return part / 1000000.0 * whole; };
    const QPoint centre(
        static_cast<int>(std::lround(pixels(zone.x, picture.width()) +
                                     pixels(zone.width, picture.width()) / 2.0)),
        static_cast<int>(std::lround(pixels(zone.y, picture.height()) +
                                     pixels(zone.height, picture.height()) / 2.0)));
    return zoneAt(picture, proportions(),
                  static_cast<int>(std::lround(pixels(zone.width, picture.width()))), centre);
}

bool ImageImportDialog::saveZone(const QString& name)
{
    const std::string key = name.trimmed().toStdString();
    if (key.empty()) {
        m_summaryLabel->setText(tr("Give the zone a name to save it."));
        return false;
    }
    document::SavedImageZone zone;
    zone.name = key;
    zone.start = toPictureZone(m_zone);
    if (m_endZone) {
        zone.end = toPictureZone(*m_endZone);
    }
    const ImageAdjustments values = adjustments();
    zone.brightness = static_cast<int8_t>(values.brightness);
    zone.contrast = static_cast<int8_t>(values.contrast);
    zone.blur = static_cast<uint8_t>(values.blur);
    auto existing = std::find_if(m_savedZones.begin(), m_savedZones.end(),
                                 [&key](const document::SavedImageZone& z) { return z.name == key; });
    if (existing != m_savedZones.end()) {
        *existing = zone;
    } else if (m_savedZones.size() >= document::kMaxSavedImageZones) {
        m_summaryLabel->setText(tr("No more zones can be saved (%1 at most).")
                                    .arg(document::kMaxSavedImageZones));
        return false;
    } else {
        m_savedZones.push_back(zone);
    }
    m_savedZonesChanged = true;
    fillSavedZoneList();
    m_savedZoneComboBox->setCurrentText(QString::fromStdString(key));
    m_summaryLabel->setText(tr("Saved the zone %1.").arg(QString::fromStdString(key)));
    return true;
}

void ImageImportDialog::applySavedZone(std::size_t index)
{
    if (index >= m_savedZones.size()) {
        return;
    }
    const document::SavedImageZone zone = m_savedZones[index];
    m_savedZoneNameEdit->setText(QString::fromStdString(zone.name));
    setAdjustments(ImageAdjustments{zone.brightness, zone.contrast, zone.blur});
    setZone(fromPictureZone(zone.start));
    setEndZone(zone.end ? std::optional<QRect>(fromPictureZone(*zone.end)) : std::nullopt);
}

void ImageImportDialog::deleteSavedZone(std::size_t index)
{
    if (index >= m_savedZones.size()) {
        return;
    }
    m_savedZones.erase(m_savedZones.begin() + static_cast<std::ptrdiff_t>(index));
    m_savedZonesChanged = true;
    fillSavedZoneList();
}

void ImageImportDialog::hover(QPoint at)
{
    m_hoverAt = at;
    m_floating = zoneAt(pictureSize(), proportions(), m_floatingWidth, at);
    updateView();
}

void ImageImportDialog::wheel(int notches, bool fast)
{
    // One view pixel a notch, as the Win32 window (ten with Shift); up makes it larger.
    const int stepPixels =
        std::max(1, static_cast<int>(std::lround(1.0 / std::max(0.01, m_view->scale()))));
    const QRect current =
        m_floating ? *m_floating : zoneAt(pictureSize(), proportions(), m_floatingWidth,
                                          m_zone.center());
    const QRect resized = zoneAt(pictureSize(), proportions(),
                                 current.width() + notches * stepPixels * (fast ? 10 : 1),
                                 m_hoverAt ? *m_hoverAt : m_zone.center());
    m_floatingWidth = resized.width();
    m_floating = resized;
    updateView();
}

void ImageImportDialog::click(QPoint at, bool end)
{
    const QRect zone = zoneAt(pictureSize(), proportions(), m_floatingWidth, at);
    if (end) {
        setEndZone(zone);
        return;
    }
    m_zone = zone;
    updateView();
    updateResult();
}

}  // namespace colorizing::app
