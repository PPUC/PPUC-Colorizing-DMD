#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QImage>
#include <QRect>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "ImageImport.h"
#include "document/ImageZone.h"
#include "render/CanvasTypes.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSlider;
class QSpinBox;

namespace colorizing::app {

class VideoClip;

// What the import goes into, given by the caller.
struct ImageImportSetup {
    // The selection's bounds on its plane (the whole plane without a selection): the zone's
    // proportions and the size of the result.
    render::Size selectionSize;
    // The selection within those bounds (row-major, 1 selected), for the preview; empty: all.
    std::vector<uint8_t> selectionMask;
    // The planes, by size ("128x32"); the second is empty in a project with one plane.
    QString firstPlaneName;
    QString secondPlaneName;
    bool firstPlaneChecked = true;
    bool secondPlaneChecked = false;
    // The background the picture can go into instead (the selected one); none without one.
    std::optional<QString> backgroundName;
    bool toBackground = false;
    // The selected frames' durations, in their order.
    std::vector<uint32_t> durationsMs;
    // The project's saved zones.
    std::vector<document::SavedImageZone> savedZones;
};

// The picture, fitted into the widget, with the zones: the zone that follows the mouse (the
// wheel resizes it, Shift ten times as fast), the start zone a left click fixes (pink) and the
// end zone a right click fixes (blue, scroll copy).
class ImageZoneView final : public QWidget {
public:
    explicit ImageZoneView(QWidget* parent = nullptr);

    void show(const QImage& picture,
              const QRect& zone,
              const std::optional<QRect>& endZone,
              const std::optional<QRect>& floating);
    // View pixels per picture pixel.
    [[nodiscard]] double scale() const;
    // The picture pixel under a view point, inside the picture.
    [[nodiscard]] QPoint toPicture(const QPointF& point) const;
    [[nodiscard]] QRectF toView(const QRect& zone) const;

    std::function<void(QPoint at)> onHover;
    std::function<void()> onLeave;
    // notches: + larger; fast: Shift held.
    std::function<void(int notches, bool fast)> onWheel;
    std::function<void(QPoint at, bool end)> onClick;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] QRectF pictureRect() const;

    QImage m_picture;
    QRect m_zone;
    std::optional<QRect> m_endZone;
    std::optional<QRect> m_floating;
    int m_wheelRemainder = 0;
};

// Import Image / Import Movie Clip: the Win32 editor's Images window. The caller writes the
// result: for each selected frame (or the background) the picture at frameTimes(), adjusted,
// the zone of zoneFor() scaled to the selection with filter().
class ImageImportDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(ImageImportDialog)

public:
    // A still picture (a file or the clipboard).
    ImageImportDialog(const QImage& picture,
                      const QString& name,
                      ImageImportSetup setup,
                      QWidget* parent = nullptr);
    // A movie clip.
    ImageImportDialog(VideoClip& clip,
                      const QString& name,
                      ImageImportSetup setup,
                      QWidget* parent = nullptr);

    [[nodiscard]] bool isMovie() const { return m_clip != nullptr; }
    // In the picture's pixels.
    [[nodiscard]] QRect zone() const { return m_zone; }
    void setZone(const QRect& zone);
    [[nodiscard]] std::optional<QRect> endZone() const { return m_endZone; }
    void setEndZone(const std::optional<QRect>& zone);
    // The zone of selected frame `index` of `count`: the start zone, or between the start and
    // the end zone (scroll copy).
    [[nodiscard]] QRect zoneFor(std::size_t index, std::size_t count) const;
    [[nodiscard]] ImageAdjustments adjustments() const;
    void setAdjustments(const ImageAdjustments& adjustments);
    [[nodiscard]] ImageScaleFilter filter() const;
    void setFilter(ImageScaleFilter filter);
    [[nodiscard]] bool toBackground() const;
    void setToBackground(bool background);
    [[nodiscard]] bool toFirstPlane() const;
    [[nodiscard]] bool toSecondPlane() const;
    void setPlanes(bool first, bool second);

    // Movie clips: where the first selected frame starts, and how the next ones go on.
    [[nodiscard]] double startMs() const;
    void setStartMs(double ms);
    enum class StepUnit { Frame, Second, Minute, Hour };
    void setStepUnit(StepUnit unit);
    void step(int direction);
    enum class Batch { AsTheyPlay, Interval, SamePicture };
    [[nodiscard]] Batch batch() const;
    void setBatch(Batch batch, int intervalMs = 0);
    // The clip time of each selected frame; a still picture: empty.
    [[nodiscard]] std::vector<double> frameTimes() const;

    // Saved zones (the Win32 editor's crop zone saves): the zones and the adjustments under a
    // name, relative to the picture. Saving a name that exists replaces it. The list may have
    // changed also when the window is cancelled; the caller keeps it in the project.
    bool saveZone(const QString& name);
    void applySavedZone(std::size_t index);
    void deleteSavedZone(std::size_t index);
    [[nodiscard]] const std::vector<document::SavedImageZone>& savedZones() const
    {
        return m_savedZones;
    }
    [[nodiscard]] bool savedZonesChanged() const { return m_savedZonesChanged; }

    // The picture shown (the clip at the start), adjusted.
    [[nodiscard]] const QImage& adjustedPicture() const { return m_adjusted; }
    // What the selection of the first frame gets, at the selection's size.
    [[nodiscard]] QImage resultPicture() const;

    // For tests: the view.
    [[nodiscard]] ImageZoneView* view() const { return m_view; }

private:
    void build(const QString& name);
    void sourceChanged();
    void adjustmentsChanged();
    void updateView();
    void updateResult();
    void hover(QPoint at);
    void wheel(int notches, bool fast);
    void click(QPoint at, bool end);
    [[nodiscard]] double proportions() const;
    [[nodiscard]] QSize pictureSize() const;
    [[nodiscard]] double stepMs() const;
    void fillSavedZoneList();
    [[nodiscard]] document::PictureZone toPictureZone(const QRect& zone) const;
    [[nodiscard]] QRect fromPictureZone(const document::PictureZone& zone) const;

    VideoClip* m_clip = nullptr;
    ImageImportSetup m_setup;
    QImage m_picture;
    QImage m_adjusted;
    QRect m_zone;
    std::optional<QRect> m_endZone;
    std::optional<QRect> m_floating;
    std::optional<QPoint> m_hoverAt;
    int m_floatingWidth = 0;
    double m_frameStepMs = 40.0;
    StepUnit m_stepUnit = StepUnit::Frame;
    std::vector<document::SavedImageZone> m_savedZones;
    bool m_savedZonesChanged = false;

    ImageZoneView* m_view = nullptr;
    QLabel* m_zoneLabel = nullptr;
    QSlider* m_brightnessSlider = nullptr;
    QSlider* m_contrastSlider = nullptr;
    QSlider* m_blurSlider = nullptr;
    QLabel* m_brightnessLabel = nullptr;
    QLabel* m_contrastLabel = nullptr;
    QLabel* m_blurLabel = nullptr;
    QComboBox* m_filterComboBox = nullptr;
    QRadioButton* m_framesRadio = nullptr;
    QRadioButton* m_backgroundRadio = nullptr;
    QCheckBox* m_firstPlaneCheckBox = nullptr;
    QCheckBox* m_secondPlaneCheckBox = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QComboBox* m_savedZoneComboBox = nullptr;
    QLineEdit* m_savedZoneNameEdit = nullptr;
    // Movie clips.
    QSlider* m_timeSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QButtonGroup* m_unitGroup = nullptr;
    QRadioButton* m_playRadio = nullptr;
    QRadioButton* m_intervalRadio = nullptr;
    QRadioButton* m_sameRadio = nullptr;
    QSpinBox* m_intervalSpinBox = nullptr;
};

}  // namespace colorizing::app
