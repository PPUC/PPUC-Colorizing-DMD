#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>

#include <QCoreApplication>
#include <QDialog>
#include <QString>

#include "document/ProjectDocument.h"
#include "import/FrameDumpImport.h"

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

namespace colorizing::app {

// What the dialog says about a dump that was read: its size and shades, and what became of
// its frames.
[[nodiscard]] QString dumpFormatText(const importing::DumpImportResult& result);
[[nodiscard]] QString dumpCountsText(const importing::DumpImportResult& result, bool addsFrames);

// The dialog of File -> New Project From Dump and File -> Add Frames From Dump: the dump
// file, the import filter, and what the import would do before anything is done. Accepting
// it changes nothing; the owner takes the frames with takeResult().
//
// The dump is read by a worker thread that runs the Qt-free importing::importFrameDumpFile()
// and nothing else. It shares with the dialog three atomics (bytes read, total bytes,
// "stop") and hands over its result when it is joined; a timer of the dialog shows the
// progress and notices the end. With a project the worker reads the document, which is why
// the dialog is modal and the document must not be changed while it exists.
class DumpImportDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(DumpImportDialog)

public:
    enum class Mode {
        NewProject,
        AddFrames,
    };

    // project: the document frames are added to; nullptr for a new project.
    DumpImportDialog(Mode mode,
                     const document::ProjectDocument* project,
                     QWidget* parent = nullptr);
    ~DumpImportDialog() override;

    // Sets the file, proposes the ROM name of a new project and starts reading.
    void setDumpPath(const QString& path);
    [[nodiscard]] QString dumpPath() const;
    void setRomName(const QString& name);
    [[nodiscard]] QString romName() const;
    // What the option controls show; changing them makes a shown result stale.
    void setOptions(const importing::DumpImportOptions& options);
    [[nodiscard]] importing::DumpImportOptions options() const;

    // Reads the dump with the options as they are now. A running analysis is stopped first.
    void startAnalysis();
    void stopAnalysis();
    // Waits for the running analysis and shows its result, without the event loop.
    void waitForAnalysis();
    [[nodiscard]] bool isAnalysisRunning() const;

    // The result of the last analysis, if it succeeded and belongs to the file and options
    // shown.
    [[nodiscard]] const importing::DumpImportResult* result() const;
    // The frames to create the project of or to add; the dialog has no result afterwards.
    [[nodiscard]] importing::DumpImportResult takeResult();
    // Why the dialog cannot be accepted; empty if it can.
    [[nodiscard]] QString acceptProblem() const;

protected:
    void done(int result) override;

private:
    void chooseFile();
    void finishAnalysis();
    void markStale();
    void updateControls();

    Mode m_mode;
    const document::ProjectDocument* m_project = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QLineEdit* m_romNameEdit = nullptr;
    QLabel* m_romNameProblemLabel = nullptr;
    QCheckBox* m_shortFramesCheckBox = nullptr;
    QSpinBox* m_shortFramesSpinBox = nullptr;
    QCheckBox* m_colorsCheckBox = nullptr;
    QSpinBox* m_colorsSpinBox = nullptr;
    QCheckBox* m_masksCheckBox = nullptr;
    QPushButton* m_analyzeButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_formatLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QTimer* m_pollTimer = nullptr;

    std::thread m_worker;
    std::atomic<uint64_t> m_bytesRead{0};
    std::atomic<uint64_t> m_totalBytes{0};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_workerDone{false};
    // Written by the worker, read after it was joined.
    importing::DumpImportResult m_workerResult;
    std::optional<importing::DumpImportResult> m_result;
    // What the last analysis ended with when it gave no result.
    QString m_failureText;
    bool m_stale = false;
};

}  // namespace colorizing::app
