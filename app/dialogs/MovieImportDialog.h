#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QRect>

#include <cstdint>
#include <vector>

#include "render/ProjectGeometry.h"

class QCheckBox;
class QLabel;
class QRadioButton;
class QSlider;
class QSpinBox;

namespace colorizing::app {

class VideoClip;
class MoviePictureView;

// Import Movie Clip Into Selected Frames: where in the clip to start, the part of its picture
// to use (the crop zone, drawn on the picture), and how the clip advances from one selected
// frame to the next (as the frames play, or a fixed interval). It only collects them; the
// caller reads the clip at frameTimes() and fills the frames.
class MovieImportDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(MovieImportDialog)

public:
    // durationsMs: the selected frames' durations, in their order.
    MovieImportDialog(VideoClip& clip,
                      const QString& fileName,
                      render::Size planeSize,
                      std::vector<uint32_t> durationsMs,
                      QWidget* parent = nullptr);

    [[nodiscard]] double startMs() const;
    void setStartMs(double ms);
    // In the clip's pixels; the whole picture when nothing is drawn.
    [[nodiscard]] QRect crop() const;
    void setCrop(const QRect& crop);
    // 0: the clip advances by each frame's duration.
    [[nodiscard]] int intervalMs() const;
    void setIntervalMs(int ms);
    [[nodiscard]] bool keepsProportions() const;

    // The clip time of each selected frame.
    [[nodiscard]] std::vector<double> frameTimes() const;
    // What the first selected frame gets, at the plane's size.
    [[nodiscard]] QImage firstFramePicture() const;

private:
    void showPosition();
    void updateResult();
    // The crop zone drawn with the mouse, made to the DMD's proportions when they are kept.
    void cropDrawn(QRect zone);

    VideoClip& m_clip;
    render::Size m_planeSize;
    std::vector<uint32_t> m_durationsMs;
    double m_frameStepMs = 40.0;
    QImage m_picture;
    QRect m_crop;
    MoviePictureView* m_view = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_cropLabel = nullptr;
    QCheckBox* m_proportionsCheckBox = nullptr;
    QLabel* m_resultLabel = nullptr;
    QRadioButton* m_followRadio = nullptr;
    QRadioButton* m_intervalRadio = nullptr;
    QSpinBox* m_intervalSpinBox = nullptr;
    QLabel* m_summaryLabel = nullptr;
};

}  // namespace colorizing::app
