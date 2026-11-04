#pragma once

#include <cstddef>
#include <vector>

#include <QCoreApplication>
#include <QDialog>
#include <QString>

#include "render/CanvasTypes.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace colorizing::app {

// File -> Export Frames PNG...: which frames, which picture of them, which plane and where to.
class FramesPngExportDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(FramesPngExportDialog)

public:
    struct Plane {
        render::Resolution resolution = render::Resolution::Sd;
        QString name;
    };

    // selectedFrames: the frames selected in the frame list (none: only a range can be
    // exported); the range starts as the selection's span, or as the current frame.
    FramesPngExportDialog(std::vector<std::size_t> selectedFrames,
                          std::size_t frameCount,
                          std::size_t currentFrame,
                          std::vector<Plane> planes,
                          render::Resolution plane,
                          const QString& folder,
                          QWidget* parent = nullptr);

    // In ascending order.
    [[nodiscard]] std::vector<std::size_t> frames() const;
    // The frame's own picture, 1:1, background transparent; otherwise libserum's picture with
    // the background, in the DMD look of the settings.
    [[nodiscard]] bool contentOnly() const;
    [[nodiscard]] render::Resolution plane() const;
    [[nodiscard]] QString folder() const;

    // For the tests.
    void setRange(std::size_t first, std::size_t last);
    void setContentOnly(bool contentOnly);
    void setFolder(const QString& folder);
    [[nodiscard]] bool canExport() const;

private:
    void refresh();

    std::vector<std::size_t> m_selectedFrames;
    std::vector<Plane> m_planes;
    QRadioButton* m_selected = nullptr;
    QRadioButton* m_range = nullptr;
    QSpinBox* m_first = nullptr;
    QSpinBox* m_last = nullptr;
    QRadioButton* m_asShown = nullptr;
    QRadioButton* m_content = nullptr;
    QComboBox* m_plane = nullptr;
    QLineEdit* m_folder = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_export = nullptr;
};

}  // namespace colorizing::app
