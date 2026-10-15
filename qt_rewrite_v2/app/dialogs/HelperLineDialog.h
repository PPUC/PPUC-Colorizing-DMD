#pragma once

#include <QCoreApplication>
#include <QDialog>

#include "document/HelperLine.h"
#include "render/ProjectGeometry.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace colorizing::app {

// A helper line's name, direction and position, on the plane it is given for.
class HelperLineDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(HelperLineDialog)

public:
    // planeName: "SD", "HD" or the plane's size, as the editor names it.
    HelperLineDialog(const document::HelperLine& line,
                     render::Size planeSize,
                     const QString& planeName,
                     QWidget* parent = nullptr);

    [[nodiscard]] document::HelperLine line() const;

private:
    void updateRange();

    document::HelperLine m_line;
    render::Size m_planeSize;
    QString m_planeName;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_directionComboBox = nullptr;
    QSpinBox* m_positionSpinBox = nullptr;
    QLabel* m_positionNote = nullptr;
};

}  // namespace colorizing::app
