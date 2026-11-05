#include "dialogs/SplitSectionsDialog.h"

#include <QPushButton>

namespace colorizing::app {

SplitSectionsDialog::SplitSectionsDialog(const QStringList& sectionNames, QWidget* parent)
    : QMessageBox(parent)
{
    const int count = static_cast<int>(sectionNames.size());
    setIcon(QMessageBox::Question);
    setWindowTitle(tr("Move Frames"));
    setText(tr("The frames start %n section(s), but not all of their frames move with them: %1.",
               nullptr, count)
                .arg(sectionNames.join(QStringLiteral(", "))));
    setInformativeText(
        count == 1 ? tr("Should the section start where its first frame goes, or stay with the "
                        "frames that stay?")
                   : tr("Should the sections start where their first frames go, or stay with "
                        "the frames that stay?"));
    m_goes = addButton(count == 1 ? tr("Move the Section With the Frames")
                                  : tr("Move the Sections With the Frames"),
                       QMessageBox::AcceptRole);
    m_goes->setObjectName(QStringLiteral("sectionsGoWithFrames"));
    m_stays = addButton(count == 1 ? tr("Keep the Section Here") : tr("Keep the Sections Here"),
                        QMessageBox::AcceptRole);
    m_stays->setObjectName(QStringLiteral("sectionsStay"));
    addButton(QMessageBox::Cancel);
    setDefaultButton(m_stays);
}

std::optional<edit::SplitSectionStart> SplitSectionsDialog::choice() const
{
    if (clickedButton() == m_goes) {
        return edit::SplitSectionStart::GoesWithMovedFrames;
    }
    if (clickedButton() == m_stays) {
        return edit::SplitSectionStart::StaysWithFrames;
    }
    return std::nullopt;
}

}  // namespace colorizing::app
