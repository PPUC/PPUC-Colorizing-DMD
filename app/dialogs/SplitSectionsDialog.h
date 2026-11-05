#pragma once

#include <optional>

#include <QCoreApplication>
#include <QMessageBox>
#include <QStringList>

#include "edit/FrameMove.h"

class QPushButton;

namespace colorizing::app {

// The question before a frame move that takes the first frame of sections without all of
// their frames (edit::sectionsSplitByMove()): do the sections start where their first frame
// goes, or at their first frame that stays?
class SplitSectionsDialog final : public QMessageBox {
    Q_DECLARE_TR_FUNCTIONS(SplitSectionsDialog)

public:
    explicit SplitSectionsDialog(const QStringList& sectionNames, QWidget* parent = nullptr);

    // After the dialog closed: the answer, nothing when it was cancelled.
    [[nodiscard]] std::optional<edit::SplitSectionStart> choice() const;

private:
    QPushButton* m_goes = nullptr;
    QPushButton* m_stays = nullptr;
};

}  // namespace colorizing::app
