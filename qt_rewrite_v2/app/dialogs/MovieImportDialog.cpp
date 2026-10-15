#include "dialogs/MovieImportDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

#include "Appearance.h"
#include "MovieImport.h"
#include "VideoClip.h"

namespace colorizing::app {

namespace {

QString timeText(double ms)
{
    const auto total = static_cast<long long>(std::max(0.0, ms));
    return QStringLiteral("%1:%2.%3")
        .arg(total / 60000)
        .arg((total / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 1000, 3, 10, QLatin1Char('0'));
}

}  // namespace

// The clip's picture, fitted into the widget, with the crop zone; dragging draws a new zone.
class MoviePictureView final : public QWidget {
public:
    explicit MoviePictureView(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumSize(480, 240);
        setCursor(Qt::CrossCursor);
    }

    void show(const QImage& picture, const QRect& crop)
    {
        m_picture = picture;
        m_crop = crop;
        update();
    }

    std::function<void(QRect zone)> onCropDrawn;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (m_picture.isNull()) {
            return;
        }
        const QRectF target = pictureRect();
        painter.drawImage(target, m_picture);
        // Outside the crop zone dimmed, the zone framed.
        const QRectF zone = toWidget(m_crop);
        QPainterPath outside;
        outside.addRect(target);
        QPainterPath inside;
        inside.addRect(zone);
        painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 150));
        painter.setPen(QPen(comparisonMaskColor(), 2));
        painter.drawRect(zone);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        m_dragStart = toPicture(event->position());
        m_dragging = true;
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging && onCropDrawn) {
            onCropDrawn(QRect(m_dragStart, toPicture(event->position())).normalized());
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        mouseMoveEvent(event);
        m_dragging = false;
    }

private:
    [[nodiscard]] QRectF pictureRect() const
    {
        const QSizeF size = QSizeF(m_picture.size()).scaled(QSizeF(this->size()), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0),
                      size);
    }

    [[nodiscard]] double scale() const
    {
        return m_picture.isNull() ? 1.0 : pictureRect().width() / m_picture.width();
    }

    [[nodiscard]] QRectF toWidget(const QRect& zone) const
    {
        const QRectF picture = pictureRect();
        const double factor = scale();
        return QRectF(picture.left() + zone.x() * factor, picture.top() + zone.y() * factor,
                      zone.width() * factor, zone.height() * factor);
    }

    [[nodiscard]] QPoint toPicture(const QPointF& point) const
    {
        const QRectF picture = pictureRect();
        const double factor = scale();
        return QPoint(std::clamp(static_cast<int>((point.x() - picture.left()) / factor), 0,
                                 std::max(0, m_picture.width())),
                      std::clamp(static_cast<int>((point.y() - picture.top()) / factor), 0,
                                 std::max(0, m_picture.height())));
    }

    QImage m_picture;
    QRect m_crop;
    QPoint m_dragStart;
    bool m_dragging = false;
};

MovieImportDialog::MovieImportDialog(VideoClip& clip,
                                     const QString& fileName,
                                     render::Size planeSize,
                                     std::vector<uint32_t> durationsMs,
                                     QWidget* parent)
    : QDialog(parent)
    , m_clip(clip)
    , m_planeSize(planeSize)
    , m_durationsMs(std::move(durationsMs))
{
    setWindowTitle(tr("Import Movie Clip: %1").arg(fileName));
    if (clip.frameRate() > 0.0) {
        m_frameStepMs = 1000.0 / clip.frameRate();
    }
    m_crop = QRect(QPoint(0, 0), clip.size());

    m_view = new MoviePictureView(this);
    m_view->setToolTip(tr("Drag over the picture to choose the part that goes into the frames"));
    m_view->onCropDrawn = [this](QRect zone) { cropDrawn(zone); };

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, static_cast<int>(std::max(0.0, clip.durationMs())));
    m_slider->setToolTip(tr("Where in the clip the first selected frame starts"));
    auto* previousButton = new QPushButton(tr("◀"), this);
    previousButton->setToolTip(tr("One picture back"));
    auto* nextButton = new QPushButton(tr("▶"), this);
    nextButton->setToolTip(tr("One picture on"));
    m_timeLabel = new QLabel(this);
    auto* positionRow = new QHBoxLayout;
    positionRow->addWidget(previousButton);
    positionRow->addWidget(m_slider, 1);
    positionRow->addWidget(nextButton);
    positionRow->addWidget(m_timeLabel);

    m_cropLabel = new QLabel(this);
    auto* wholeButton = new QPushButton(tr("Whole Picture"), this);
    m_proportionsCheckBox = new QCheckBox(tr("Keep the DMD's proportions"), this);
    m_proportionsCheckBox->setChecked(true);
    m_proportionsCheckBox->setToolTip(
        tr("The crop zone has the frame's width to height (%1x%2), so nothing is stretched")
            .arg(planeSize.width)
            .arg(planeSize.height));
    auto* cropRow = new QHBoxLayout;
    cropRow->addWidget(m_cropLabel, 1);
    cropRow->addWidget(m_proportionsCheckBox);
    cropRow->addWidget(wholeButton);

    m_resultLabel = new QLabel(this);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    auto* resultGroup = new QGroupBox(tr("The first selected frame gets"), this);
    auto* resultLayout = new QVBoxLayout(resultGroup);
    resultLayout->addWidget(m_resultLabel);

    auto* timingGroup = new QGroupBox(tr("The next frames get"), this);
    m_followRadio = new QRadioButton(tr("The clip as the frames play (their durations)"),
                                     timingGroup);
    m_intervalRadio = new QRadioButton(tr("A picture every"), timingGroup);
    m_intervalSpinBox = new QSpinBox(timingGroup);
    m_intervalSpinBox->setRange(5, 3000);
    m_intervalSpinBox->setValue(std::max(5, static_cast<int>(std::lround(m_frameStepMs))));
    m_intervalSpinBox->setSuffix(tr(" ms"));
    m_followRadio->setChecked(true);
    auto* intervalRow = new QHBoxLayout;
    intervalRow->addWidget(m_intervalRadio);
    intervalRow->addWidget(m_intervalSpinBox);
    intervalRow->addStretch(1);
    auto* timingLayout = new QVBoxLayout(timingGroup);
    timingLayout->addWidget(m_followRadio);
    timingLayout->addLayout(intervalRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(resultGroup, 1);
    bottom->addWidget(timingGroup, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addLayout(positionRow);
    layout->addLayout(cropRow);
    layout->addLayout(bottom);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(buttons);

    connect(m_slider, &QSlider::valueChanged, this, [this](int) { showPosition(); });
    connect(previousButton, &QPushButton::clicked, this,
            [this]() { setStartMs(startMs() - m_frameStepMs); });
    connect(nextButton, &QPushButton::clicked, this,
            [this]() { setStartMs(startMs() + m_frameStepMs); });
    connect(wholeButton, &QPushButton::clicked, this,
            [this]() { setCrop(QRect(QPoint(0, 0), m_clip.size())); });
    connect(m_followRadio, &QRadioButton::toggled, this, [this](bool) { updateResult(); });
    connect(m_intervalSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, [this](int) {
                m_intervalRadio->setChecked(true);
                updateResult();
            });
    resize(760, 640);
    showPosition();
}

double MovieImportDialog::startMs() const
{
    return m_slider->value();
}

void MovieImportDialog::setStartMs(double ms)
{
    m_slider->setValue(static_cast<int>(std::lround(std::max(0.0, ms))));
    showPosition();
}

QRect MovieImportDialog::crop() const
{
    return m_crop;
}

void MovieImportDialog::setCrop(const QRect& crop)
{
    const QRect zone = crop.intersected(QRect(QPoint(0, 0), m_clip.size()));
    m_crop = zone.isEmpty() ? QRect(QPoint(0, 0), m_clip.size()) : zone;
    updateResult();
}

int MovieImportDialog::intervalMs() const
{
    return m_intervalRadio->isChecked() ? m_intervalSpinBox->value() : 0;
}

void MovieImportDialog::setIntervalMs(int ms)
{
    if (ms > 0) {
        m_intervalSpinBox->setValue(ms);
        m_intervalRadio->setChecked(true);
    } else {
        m_followRadio->setChecked(true);
    }
    updateResult();
}

bool MovieImportDialog::keepsProportions() const
{
    return m_proportionsCheckBox->isChecked();
}

std::vector<double> MovieImportDialog::frameTimes() const
{
    return movieFrameTimes(startMs(), m_durationsMs, intervalMs());
}

QImage MovieImportDialog::firstFramePicture() const
{
    if (m_picture.isNull()) {
        return {};
    }
    return croppedMoviePicture(m_picture, m_crop)
        .convertToFormat(QImage::Format_RGB32)
        .scaled(m_planeSize.width, m_planeSize.height, Qt::IgnoreAspectRatio,
                Qt::FastTransformation);
}

void MovieImportDialog::cropDrawn(QRect zone)
{
    if (keepsProportions() && m_planeSize.height > 0 && zone.height() > 0) {
        // The zone's height follows its width.
        const double ratio = static_cast<double>(m_planeSize.width) / m_planeSize.height;
        zone.setHeight(std::max(1, static_cast<int>(std::lround(zone.width() / ratio))));
    }
    setCrop(zone);
}

void MovieImportDialog::showPosition()
{
    m_picture = m_clip.frameAt(startMs());
    m_timeLabel->setText(
        tr("%1 of %2").arg(timeText(startMs()), timeText(m_clip.durationMs())));
    updateResult();
}

void MovieImportDialog::updateResult()
{
    m_view->show(m_picture, m_crop);
    m_cropLabel->setText(tr("Crop zone: %1, %2, %3x%4 pixels of %5x%6")
                             .arg(m_crop.x())
                             .arg(m_crop.y())
                             .arg(m_crop.width())
                             .arg(m_crop.height())
                             .arg(m_clip.size().width())
                             .arg(m_clip.size().height()));
    const QImage result = firstFramePicture();
    m_resultLabel->setPixmap(result.isNull()
                                 ? QPixmap()
                                 : QPixmap::fromImage(result.scaled(
                                       result.size() * 3, Qt::KeepAspectRatio,
                                       Qt::FastTransformation)));
    const std::vector<double> times = frameTimes();
    if (times.empty()) {
        m_summaryLabel->clear();
        return;
    }
    const double end = times.back();
    m_summaryLabel->setText(
        end > m_clip.durationMs()
            ? tr("%1 frame(s) get the clip from %2 to %3; it ends at %4, the frames after that "
                 "are left as they are.")
                  .arg(times.size())
                  .arg(timeText(times.front()), timeText(end), timeText(m_clip.durationMs()))
            : tr("%1 frame(s) get the clip from %2 to %3.")
                  .arg(times.size())
                  .arg(timeText(times.front()), timeText(end)));
}

}  // namespace colorizing::app
