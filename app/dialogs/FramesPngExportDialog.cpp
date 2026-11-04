#include "dialogs/FramesPngExportDialog.h"

#include <algorithm>
#include <utility>

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace colorizing::app {

FramesPngExportDialog::FramesPngExportDialog(std::vector<std::size_t> selectedFrames,
                                             std::size_t frameCount,
                                             std::size_t currentFrame,
                                             std::vector<Plane> planes,
                                             render::Resolution plane,
                                             const QString& folder,
                                             QWidget* parent)
    : QDialog(parent),
      m_selectedFrames(std::move(selectedFrames)),
      m_planes(std::move(planes))
{
    setObjectName(QStringLiteral("framesPngExportDialog"));
    setWindowTitle(tr("Export Frames PNG"));
    std::sort(m_selectedFrames.begin(), m_selectedFrames.end());
    m_selectedFrames.erase(std::unique(m_selectedFrames.begin(), m_selectedFrames.end()),
                           m_selectedFrames.end());
    auto* layout = new QVBoxLayout(this);

    auto* framesBox = new QGroupBox(tr("Frames"), this);
    auto* framesLayout = new QVBoxLayout(framesBox);
    m_selected = new QRadioButton(
        tr("The %n selected frames", nullptr, static_cast<int>(m_selectedFrames.size())),
        framesBox);
    m_selected->setEnabled(!m_selectedFrames.empty());
    m_range = new QRadioButton(tr("From frame to frame"), framesBox);
    const int lastFrame = static_cast<int>(frameCount > 0 ? frameCount - 1 : 0);
    m_first = new QSpinBox(framesBox);
    m_last = new QSpinBox(framesBox);
    for (QSpinBox* spin : {m_first, m_last}) {
        spin->setRange(0, lastFrame);
    }
    const int current = std::min(static_cast<int>(currentFrame), lastFrame);
    m_first->setValue(m_selectedFrames.empty() ? current
                                               : static_cast<int>(m_selectedFrames.front()));
    m_last->setValue(m_selectedFrames.empty() ? current
                                              : static_cast<int>(m_selectedFrames.back()));
    auto* rangeRow = new QHBoxLayout();
    rangeRow->addWidget(m_range);
    rangeRow->addWidget(m_first);
    //: Between the first and the last frame of the range to export
    rangeRow->addWidget(new QLabel(tr("to"), framesBox));
    rangeRow->addWidget(m_last);
    rangeRow->addStretch(1);
    framesLayout->addWidget(m_selected);
    framesLayout->addLayout(rangeRow);
    (m_selectedFrames.size() > 1 ? m_selected : m_range)->setChecked(true);
    layout->addWidget(framesBox);

    auto* pictureBox = new QGroupBox(tr("Picture"), this);
    auto* pictureLayout = new QVBoxLayout(pictureBox);
    m_asShown = new QRadioButton(tr("As shown, with the background"), pictureBox);
    m_asShown->setToolTip(tr("libserum's picture of the frame, as a machine shows it, in the "
                             "pixel shape and size of the settings: to show it to someone"));
    m_content = new QRadioButton(tr("Frame content only, background transparent"), pictureBox);
    m_content->setToolTip(
        tr("The frame's own picture without background and sprites, one image pixel per DMD "
           "pixel in its exact colors, lossless: as source for an external tool such as an "
           "upscaler, whose result imports back into the frames"));
    m_asShown->setChecked(true);
    pictureLayout->addWidget(m_asShown);
    pictureLayout->addWidget(m_content);
    layout->addWidget(pictureBox);

    auto* form = new QFormLayout();
    m_plane = new QComboBox(this);
    for (const Plane& entry : m_planes) {
        m_plane->addItem(entry.name);
        if (entry.resolution == plane) {
            m_plane->setCurrentIndex(m_plane->count() - 1);
        }
    }
    m_plane->setEnabled(m_planes.size() > 1);
    form->addRow(tr("Plane"), m_plane);
    m_folder = new QLineEdit(folder, this);
    auto* choose = new QPushButton(tr("Choose..."), this);
    auto* folderRow = new QHBoxLayout();
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(choose);
    form->addRow(tr("Folder"), folderRow);
    layout->addLayout(form);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_export = buttons->button(QDialogButtonBox::Ok);
    m_export->setText(tr("Export"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(choose, &QPushButton::clicked, this, [this]() {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("Export Frames PNG"), m_folder->text());
        if (!chosen.isEmpty()) {
            m_folder->setText(QDir::toNativeSeparators(chosen));
        }
    });
    for (QRadioButton* button : {m_selected, m_range}) {
        connect(button, &QRadioButton::toggled, this, [this]() { refresh(); });
    }
    for (QSpinBox* spin : {m_first, m_last}) {
        connect(spin, &QSpinBox::valueChanged, this, [this]() {
            if (!m_range->isChecked()) {
                m_range->setChecked(true);
            }
            refresh();
        });
    }
    connect(m_folder, &QLineEdit::textChanged, this, [this]() { refresh(); });
    refresh();
}

std::vector<std::size_t> FramesPngExportDialog::frames() const
{
    if (m_selected->isChecked()) {
        return m_selectedFrames;
    }
    std::vector<std::size_t> frames;
    for (int frame = m_first->value(); frame <= m_last->value(); ++frame) {
        frames.push_back(static_cast<std::size_t>(frame));
    }
    return frames;
}

bool FramesPngExportDialog::contentOnly() const
{
    return m_content->isChecked();
}

render::Resolution FramesPngExportDialog::plane() const
{
    const int index = m_plane->currentIndex();
    return index >= 0 && static_cast<std::size_t>(index) < m_planes.size()
               ? m_planes[static_cast<std::size_t>(index)].resolution
               : render::Resolution::Sd;
}

QString FramesPngExportDialog::folder() const
{
    return QDir::fromNativeSeparators(m_folder->text().trimmed());
}

void FramesPngExportDialog::setRange(std::size_t first, std::size_t last)
{
    m_first->setValue(static_cast<int>(first));
    m_last->setValue(static_cast<int>(last));
    m_range->setChecked(true);
}

void FramesPngExportDialog::setContentOnly(bool contentOnly)
{
    (contentOnly ? m_content : m_asShown)->setChecked(true);
}

void FramesPngExportDialog::setFolder(const QString& folder)
{
    m_folder->setText(folder);
}

bool FramesPngExportDialog::canExport() const
{
    return m_export->isEnabled();
}

void FramesPngExportDialog::refresh()
{
    const std::size_t count = frames().size();
    m_summary->setText(count == 0 ? tr("The range is empty: the first frame is after the last.")
                                  : tr("%n PNG files", nullptr, static_cast<int>(count)));
    m_export->setEnabled(count > 0 && !folder().isEmpty());
}

}  // namespace colorizing::app
