#pragma once

#include <functional>
#include <optional>
#include <string>

#include <QDialog>
#include <QString>

#include "legacy/LegacyProjectIo.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace colorizing::app {

// The progress of an open or a save that takes long: what is being done, a bar, and Cancel.
// Shown by runProjectTask(); it closes itself when the work has ended. Closing it any other
// way (Esc, the window's close button) asks to cancel, as Cancel does, and waits for the end.
class ProjectTaskDialog final : public QDialog {
public:
    ProjectTaskDialog(const QString& title,
                      legacy::LegacyIoProgress& progress,
                      std::function<bool()> finished,
                      QWidget* parent = nullptr);

    // Asks the work to stop (LegacyIoProgress::cancel).
    void requestCancel();
    // The text and bar as the progress is now; also what the timer calls.
    void updateProgress();

    [[nodiscard]] QString statusText() const;
    [[nodiscard]] bool isCancelEnabled() const;

    // Esc and the window's close button: asks to cancel; the dialog stays until the end.
    void reject() override;

private:
    legacy::LegacyIoProgress& m_progress;
    std::function<bool()> m_finished;
    QLabel* m_label = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QTimer* m_timer = nullptr;
};

// Runs `work` on a worker thread and keeps the UI from changing anything until it has ended.
// `work` must be Qt-free and use only what it owns, what it is given, and `progress`; it runs
// the core's load or save, which report to `progress` and stop when its cancel is set.
//
// The UI thread waits for up to projectTaskDialogDelayMs() without handling any event, so a
// quick task behaves exactly like a synchronous call. A task that takes longer gets an
// application-modal ProjectTaskDialog, which blocks all input to the other windows (and so
// every edit, menu and shortcut) until the task has ended. Returns the message of an
// exception `work` threw, or nothing.
[[nodiscard]] std::optional<std::string> runProjectTask(QWidget* parent,
                                                        const QString& title,
                                                        legacy::LegacyIoProgress& progress,
                                                        const std::function<void()>& work);

// The wait before the dialog appears, 300 ms by default. Tests set 0 to see the dialog
// at once.
[[nodiscard]] int projectTaskDialogDelayMs();
void setProjectTaskDialogDelayMs(int milliseconds);
// For tests: called with the dialog right after it is shown (before the work has to end).
void setProjectTaskDialogShownHook(std::function<void(ProjectTaskDialog&)> hook);
// For tests: run on the worker thread before the work, e.g. to hold it until the dialog is
// up, which makes what a test sees independent of timing. It may only use atomics.
void setProjectTaskWorkerStartHook(std::function<void()> hook);

}  // namespace colorizing::app
