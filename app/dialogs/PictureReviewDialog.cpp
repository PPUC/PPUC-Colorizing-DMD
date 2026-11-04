#include "dialogs/PictureReviewDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <utility>

#include "FramePictureView.h"
#include "PlaneLabels.h"
#include "document/ProjectDocument.h"
#include "edit/ColorModel.h"

namespace colorizing::app {

namespace {

using render::Resolution;

// The picture the proposal replaces, as the author sees it now: the record's own picture on
// the plane, or for a record without HD its SD picture doubled (what Create HD would make).
std::vector<uint16_t> currentPicture(const document::ProjectDocument& document,
                                     const edit::PictureEdit& picture,
                                     render::Size size)
{
    const std::size_t count =
        static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
    std::vector<uint16_t> pixels(count, 0);
    // Reads pixel (x, y) of the picture on the plane, or the SD pixel under it when the record
    // has nothing on the HD plane.
    auto fill = [&](auto&& read, bool doubled) {
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
                       static_cast<std::size_t>(x)] = doubled ? read(x / 2, y / 2) : read(x, y);
            }
        }
    };
    const bool hd = picture.plane == Resolution::Hd;
    try {
        switch (picture.kind) {
        case edit::PictureKind::Frame: {
            const bool doubled = hd && !document.hasHdFrame(picture.index);
            const document::Rgb565Frame& frame = document.colorizedFrame(
                picture.index, doubled ? Resolution::Sd : picture.plane);
            fill([&](int x, int y) {
                     return frame.pixels[static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(frame.size.width) +
                                         static_cast<std::size_t>(x)];
                 },
                 doubled);
            break;
        }
        case edit::PictureKind::Background: {
            const bool doubled = hd && !document.background(picture.index).colorizedHd;
            const document::Rgb565Frame& frame = document.backgroundFrame(
                picture.index, doubled ? Resolution::Sd : picture.plane);
            fill([&](int x, int y) {
                     return frame.pixels[static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(frame.size.width) +
                                         static_cast<std::size_t>(x)];
                 },
                 doubled);
            break;
        }
        case edit::PictureKind::Sprite: {
            const bool doubled = hd && !document.hasHdSprite(picture.index);
            const Resolution plane = doubled ? Resolution::Sd : picture.plane;
            fill([&](int x, int y) {
                     return document.spriteColorizedPixel(picture.index, plane, x, y);
                 },
                 doubled);
            break;
        }
        }
    } catch (const std::exception&) {
        // Nothing to show on that plane: black.
    }
    return pixels;
}

QImage pictureImage(render::Size size, const std::vector<uint16_t>& pixels, int pixelScale)
{
    QImage image(size.width, size.height, QImage::Format_RGB32);
    for (int y = 0; y < size.height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size.width; ++x) {
            const edit::Rgb888 color = edit::rgb565ToRgb888(
                pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
                       static_cast<std::size_t>(x)]);
            line[x] = qRgb(color.red, color.green, color.blue);
        }
    }
    const int scale = std::max(1, pixelScale);
    return image.scaled(size.width * scale, size.height * scale, Qt::IgnoreAspectRatio,
                        Qt::FastTransformation);
}

}  // namespace

PictureReviewDialog::PictureReviewDialog(const document::ProjectDocument& document,
                                         const QString& title,
                                         std::vector<vpx::ProposedPicture> pictures,
                                         QWidget* parent)
    : QDialog(parent)
    , m_pictures(std::move(pictures))
{
    setWindowTitle(title.isEmpty() ? tr("Review pictures") : title);

    m_list = new QListWidget(this);
    for (const vpx::ProposedPicture& proposed : m_pictures) {
        Entry entry;
        entry.size = edit::checkPictureEdit(document, proposed.picture);
        entry.before = currentPicture(document, proposed.picture, entry.size);
        entry.after = entry.before;
        for (std::size_t index = 0; index < entry.after.size(); ++index) {
            if (proposed.picture.write.empty() || proposed.picture.write[index] != 0) {
                entry.after[index] = proposed.picture.pixels[index];
            }
            if (entry.after[index] != entry.before[index]) {
                ++entry.changed;
            }
        }
        m_entries.push_back(std::move(entry));

        auto* item = new QListWidgetItem(
            proposed.label.empty() ? defaultLabel(document, proposed.picture)
                                   : QString::fromStdString(proposed.label),
            m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }

    m_before = new FramePictureView(this);
    m_after = new FramePictureView(this);
    auto* beforeLabel = new QLabel(tr("Now"), this);
    auto* afterLabel = new QLabel(tr("Proposed"), this);
    m_markSkipped = new QCheckBox(tr("Mark the pixels the proposal leaves as they are"), this);
    m_details = new QLabel(this);
    m_outdated = new QLabel(this);
    m_outdated->setWordWrap(true);
    m_outdated->setVisible(false);

    auto* pictureArea = new QWidget(this);
    auto* picturesLayout = new QVBoxLayout(pictureArea);
    picturesLayout->setContentsMargins(0, 0, 0, 0);
    picturesLayout->addWidget(beforeLabel);
    picturesLayout->addWidget(m_before, 1);
    picturesLayout->addWidget(afterLabel);
    picturesLayout->addWidget(m_after, 1);
    picturesLayout->addWidget(m_markSkipped);
    picturesLayout->addWidget(m_details);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(m_list);
    splitter->addWidget(pictureArea);
    splitter->setStretchFactor(1, 3);

    auto* checkAll = new QPushButton(tr("Check All"), this);
    auto* checkNone = new QPushButton(tr("Check None"), this);
    auto* buttons = new QDialogButtonBox(this);
    m_apply = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    QPushButton* discard = buttons->addButton(tr("Discard"), QDialogButtonBox::RejectRole);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(checkAll);
    bottom->addWidget(checkNone);
    bottom->addStretch(1);
    bottom->addWidget(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_outdated);
    layout->addLayout(bottom);
    resize(900, 560);

    connect(m_list, &QListWidget::currentRowChanged, this, [this]() { showPicture(); });
    connect(m_list, &QListWidget::itemChanged, this, [this]() { updateApply(); });
    connect(m_markSkipped, &QCheckBox::toggled, this, [this]() { showPicture(); });
    connect(checkAll, &QPushButton::clicked, this, [this]() {
        for (std::size_t index = 0; index < m_pictures.size(); ++index) {
            setChecked(index, true);
        }
    });
    connect(checkNone, &QPushButton::clicked, this, [this]() {
        for (std::size_t index = 0; index < m_pictures.size(); ++index) {
            setChecked(index, false);
        }
    });
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        if (canApply() && m_applyAction) {
            m_applyAction();
        }
    });
    connect(discard, &QPushButton::clicked, this, &QDialog::reject);

    if (!m_pictures.empty()) {
        m_list->setCurrentRow(0);
    }
    showPicture();
    updateApply();
}

const std::vector<vpx::ProposedPicture>& PictureReviewDialog::pictures() const
{
    return m_pictures;
}

std::vector<std::size_t> PictureReviewDialog::chosen() const
{
    std::vector<std::size_t> result;
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->checkState() == Qt::Checked) {
            result.push_back(static_cast<std::size_t>(row));
        }
    }
    return result;
}

void PictureReviewDialog::setChecked(std::size_t picture, bool checked)
{
    if (picture < static_cast<std::size_t>(m_list->count())) {
        m_list->item(static_cast<int>(picture))
            ->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
}

std::size_t PictureReviewDialog::current() const
{
    return m_list->currentRow() < 0 ? 0 : static_cast<std::size_t>(m_list->currentRow());
}

void PictureReviewDialog::setCurrent(std::size_t picture)
{
    if (picture < static_cast<std::size_t>(m_list->count())) {
        m_list->setCurrentRow(static_cast<int>(picture));
    }
}

std::size_t PictureReviewDialog::changedPixels() const
{
    return m_entries.empty() ? 0 : m_entries[current()].changed;
}

void PictureReviewDialog::setOutdated(const QString& reason)
{
    m_isOutdated = true;
    m_outdated->setText(reason);
    m_outdated->setVisible(true);
    updateApply();
}

bool PictureReviewDialog::canApply() const
{
    return !m_isOutdated && !chosen().empty();
}

void PictureReviewDialog::setApplyAction(std::function<void()> action)
{
    m_applyAction = std::move(action);
}

QString PictureReviewDialog::defaultLabel(const document::ProjectDocument& document,
                                          const edit::PictureEdit& picture)
{
    const int index = static_cast<int>(picture.index);
    QString name;
    switch (picture.kind) {
    case edit::PictureKind::Frame:
        name = tr("Frame %1").arg(index);
        break;
    case edit::PictureKind::Background:
        name = tr("Background %1").arg(index);
        break;
    case edit::PictureKind::Sprite:
        name = tr("Sprite %1").arg(index);
        break;
    }
    return tr("%1 (%2)").arg(name, planeLabel(document, picture.plane));
}

void PictureReviewDialog::showPicture()
{
    if (m_entries.empty()) {
        m_before->clear();
        m_after->clear();
        m_details->clear();
        return;
    }
    const Entry& entry = m_entries[current()];
    m_before->setFrame(entry.size, [this, &entry](int scale) {
        return render(entry, false, scale);
    });
    m_after->setFrame(entry.size, [this, &entry](int scale) {
        return render(entry, true, scale);
    });
    m_details->setText(tr("%n pixel(s) change", nullptr, static_cast<int>(entry.changed)));
}

QImage PictureReviewDialog::render(const Entry& entry, bool proposed, int pixelScale) const
{
    QImage image = pictureImage(entry.size, proposed ? entry.after : entry.before, pixelScale);
    const edit::PictureEdit& picture = m_pictures[static_cast<std::size_t>(
        &entry - m_entries.data())].picture;
    if (!proposed || !m_markSkipped->isChecked() || picture.write.empty()) {
        return image;
    }
    // The pixels it does not write, tinted.
    const int scale = std::max(1, pixelScale);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y / scale) * static_cast<std::size_t>(entry.size.width) +
                static_cast<std::size_t>(x / scale);
            if (picture.write[index] == 0) {
                const QRgb color = line[x];
                line[x] = qRgb((qRed(color) + 255) / 2, (qGreen(color) + 200) / 2,
                               qBlue(color) / 2);
            }
        }
    }
    return image;
}

void PictureReviewDialog::updateApply()
{
    const std::size_t count = chosen().size();
    m_apply->setText(count > 0
                         ? tr("Apply %n Picture(s)", nullptr, static_cast<int>(count))
                         : tr("Apply"));
    m_apply->setEnabled(canApply());
}

}  // namespace colorizing::app
