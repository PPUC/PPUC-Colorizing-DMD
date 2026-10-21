#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "FrameImageUtils.h"
#include "document/ProjectDocument.h"
#include "runtime/FrameTester.h"
#include "runtime/RuntimePreview.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace colorizing::app {

class SpeakerLightPair;

// The Win32 editor's Frame Tester: ROM frames (the selected frames of the project, or every
// frame of a dump) are fed one after the other to libserum with the whole project loaded, as
// on a machine, and libserum's planes are shown with the ROM frame. libserum carries its
// state (which frame it identified last, rotations) from one fed frame to the next, also when
// the position jumps.
//
// Modal: libserum holds one colorization per process, and the project must not change while
// it is loaded (adding a dump frame reloads it).
class FramePictureView;

class FrameTesterDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(FrameTesterDialog)

public:
    FrameTesterDialog(const document::ProjectDocument& document,
                      std::vector<std::size_t> selectedFrames,
                      QWidget* parent = nullptr);
    ~FrameTesterDialog() override;

    // Adds the dump frame to the project; returns whether it was added. The project is loaded
    // again afterwards.
    std::function<bool(const runtime::TesterFrame& frame)> onAddFrameRequested;
    // After the dialog closed through Show In Editor: the frame to show.
    [[nodiscard]] std::optional<std::size_t> frameToShow() const;

    // What the buttons do, also for tests.
    bool loadDumpFile(const QString& path);
    void useSelectedFrames();
    void showPosition(std::size_t position);
    // Feeds the frames after the current one until libserum identifies none; stays where it
    // was when every frame up to the end is identified. Returns whether one was found.
    bool nextUnidentified();
    // Selected frames: goes to the project frame if it is one of them. Dump: goes to the next
    // dump frame after the current one that is that project frame (under its comparison mask
    // when underMask), going round. Returns whether it was found.
    bool findProjectFrame(std::size_t frameIndex, bool underMask);
    void setPlaying(int direction);
    // Whether libserum plays the project's scenes (on by default); the project is loaded again.
    void setPlayScenes(bool play);
    // The scene the frame libserum shows belongs to, as the info line names it; empty for a
    // frame of the game.
    [[nodiscard]] QString sceneText() const;
    // One frame back (-1) or forward (+1), also while playing, which goes on from there;
    // round the ends with Loop.
    void stepBy(int delta);

    [[nodiscard]] bool isDumpMode() const;
    [[nodiscard]] std::size_t frameCount() const;
    [[nodiscard]] std::size_t position() const;
    [[nodiscard]] const runtime::RuntimeFrame& lastResult() const;
    [[nodiscard]] QString infoText() const;
    [[nodiscard]] QString errorText() const;
    // libserum's picture of the current frame: the 64-row plane when asked for and present,
    // otherwise the other one; nothing before a frame was fed.
    [[nodiscard]] const document::Rgb565Frame* currentPicture(bool preferRows64) const;

    // The speaker light preview: what libserum starts (Settings -> Appearance).
    // Beside the 32-row plane, and beside the 64-row plane.
    [[nodiscard]] SpeakerLightPair* speakerLights() const { return m_speakerLights; }
    [[nodiscard]] SpeakerLightPair* speakerLightsHd() const { return m_speakerLightsHd; }

private:
    bool loadProject();
    void setFrames(std::vector<runtime::TesterFrame> frames, const QString& source);
    void feed(std::size_t position);
    void showResult();
    void playStep();
    void rotateStep();
    void updateControls();
    double speed() const;

    const document::ProjectDocument& m_document;
    std::vector<std::size_t> m_selectedFrames;
    std::vector<runtime::TesterFrame> m_frames;
    bool m_dumpMode = false;
    QString m_source;
    std::size_t m_position = 0;
    runtime::RuntimeFrame m_result;
    QString m_error;
    // 1 forward, -1 backward, 0 paused.
    int m_playDirection = 0;
    std::optional<std::size_t> m_frameToShow;
    QTimer m_playTimer;
    QTimer m_rotationTimer;

    QLabel* m_sourceLabel = nullptr;
    FramePictureView* m_rows32View = nullptr;
    FramePictureView* m_rows64View = nullptr;
    FramePictureView* m_originalView = nullptr;
    SpeakerLightPair* m_speakerLights = nullptr;
    SpeakerLightPair* m_speakerLightsHd = nullptr;
    // The canvas' pixel shape and size (Settings -> Appearance).
    DmdRenderOptions m_renderOptions;
    QLabel* m_infoLabel = nullptr;
    QSlider* m_slider = nullptr;
    QSpinBox* m_positionSpinBox = nullptr;
    QPushButton* m_backButton = nullptr;
    QPushButton* m_stepBackButton = nullptr;
    QCheckBox* m_playScenesCheckBox = nullptr;
    QPushButton* m_stepForwardButton = nullptr;
    QPushButton* m_playButton = nullptr;
    QComboBox* m_speedComboBox = nullptr;
    QCheckBox* m_loopCheckBox = nullptr;
    QPushButton* m_nextUnidentifiedButton = nullptr;
    QSpinBox* m_findSpinBox = nullptr;
    QCheckBox* m_underMaskCheckBox = nullptr;
    QPushButton* m_findButton = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_showButton = nullptr;
    QPushButton* m_selectedButton = nullptr;
};

}  // namespace colorizing::app
