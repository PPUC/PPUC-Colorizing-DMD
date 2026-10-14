#pragma once

#include <functional>

#include <QDialog>
#include <QString>

#include "edit/PlaneSelection.h"

class QCheckBox;
class QLabel;
class QRadioButton;
class QSpinBox;

namespace colorizing::app {

// Edit -> Auto Select... (the Win32 auto selection fields under IDC_DOAUTOSEL): stripes of rows
// or columns.
class AutoSelectDialog final : public QDialog {
public:
    explicit AutoSelectDialog(QWidget* parent = nullptr);

    void setPattern(const edit::StripePattern& pattern);
    [[nodiscard]] edit::StripePattern pattern() const;

private:
    QSpinBox* m_offset = nullptr;
    QSpinBox* m_selected = nullptr;
    QSpinBox* m_skipped = nullptr;
    QCheckBox* m_columns = nullptr;
};

// Edit -> Auto Copy...: where to look, and what was found there before anything changes.
class AutoCopyDialog final : public QDialog {
public:
    // describe gives the text for "selected frames only" or all frames; it runs the scan.
    AutoCopyDialog(bool hasSelectedFrames,
                   std::function<QString(bool selectedFramesOnly)> describe,
                   QWidget* parent = nullptr);

    [[nodiscard]] bool selectedFramesOnly() const;

private:
    void refresh();

    std::function<QString(bool)> m_describe;
    QRadioButton* m_allFrames = nullptr;
    QRadioButton* m_selectedFrames = nullptr;
    QLabel* m_summary = nullptr;
};

}  // namespace colorizing::app
