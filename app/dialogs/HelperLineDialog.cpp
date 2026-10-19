#include "dialogs/HelperLineDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace colorizing::app {

HelperLineDialog::HelperLineDialog(const document::HelperLine& line,
                                   render::Size planeSize,
                                   const QString& planeName,
                                   QWidget* parent)
    : QDialog(parent)
    , m_line(line)
    , m_planeSize(planeSize)
    , m_planeName(planeName)
{
    setWindowTitle(tr("Helper Line"));
    m_nameEdit = new QLineEdit(QString::fromStdString(line.name), this);
    m_nameEdit->setPlaceholderText(tr("e.g. Score baseline"));
    m_directionComboBox = new QComboBox(this);
    m_directionComboBox->addItem(tr("Horizontal"), false);
    m_directionComboBox->addItem(tr("Vertical"), true);
    m_directionComboBox->setCurrentIndex(line.vertical ? 1 : 0);
    m_positionSpinBox = new QSpinBox(this);
    m_positionNote = new QLabel(this);
    m_positionNote->setWordWrap(true);
    updateRange();
    m_positionSpinBox->setValue(static_cast<int>(line.position));

    auto* form = new QFormLayout;
    form->addRow(tr("Name"), m_nameEdit);
    form->addRow(tr("Direction"), m_directionComboBox);
    form->addRow(tr("Position"), m_positionSpinBox);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_positionNote);
    layout->addWidget(buttons);
    connect(m_directionComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateRange(); });
}

void HelperLineDialog::updateRange()
{
    const bool vertical = m_directionComboBox->currentData().toBool();
    const int extent = vertical ? m_planeSize.width : m_planeSize.height;
    m_positionSpinBox->setRange(0, std::max(0, extent));
    m_positionNote->setText(
        vertical ? tr("In pixels of the %1 plane (%2 columns): the line is on the left edge of "
                      "that column, %2 is the right edge of the frame.")
                       .arg(m_planeName)
                       .arg(extent)
                 : tr("In pixels of the %1 plane (%2 rows): the line is on the top edge of that "
                      "row, %2 is the bottom edge of the frame.")
                       .arg(m_planeName)
                       .arg(extent));
}

document::HelperLine HelperLineDialog::line() const
{
    document::HelperLine line = m_line;
    line.name = m_nameEdit->text().trimmed().toStdString();
    line.vertical = m_directionComboBox->currentData().toBool();
    line.position = static_cast<uint32_t>(m_positionSpinBox->value());
    return line;
}

}  // namespace colorizing::app
