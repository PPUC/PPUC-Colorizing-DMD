#pragma once

#include <QCoreApplication>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::vpx {
class PupHost;
}

namespace colorizing::app {

class PupScreenView;

// View -> PuP Tester: plays the project's PuP pack with VPX's PUP plugin as a machine does
// (vpx::PupHost). The editor is the game and its colorization: it sends a frame's trigger ID
// when the frame is shown (Follow the editor) or when asked, and PUP shows the pack's media on
// its topper, backglass and DMD screens, drawn here.
class PupTesterWindow final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PupTesterWindow)

public:
    explicit PupTesterWindow(QWidget* parent = nullptr);
    ~PupTesterWindow() override;

    // The project's game: the PuP pack name it names, or its ROM name. nullptr without one.
    void setProject(const document::ProjectDocument* document);
    // The editor shows another frame: its trigger goes to PUP when the tester follows the
    // editor.
    void frameShown(std::optional<uint32_t> triggerId);

    [[nodiscard]] bool isRunning() const;
    bool start();
    void stop();
    void sendTrigger(uint32_t id);

    // The game PUP is asked for, from the project (or what the field was changed to).
    [[nodiscard]] QString game() const;
    // Whether the PUP folder has a pack for the game.
    [[nodiscard]] bool packFound() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void updateState();
    void renderScreens();

    std::unique_ptr<vpx::PupHost> m_host;
    QLineEdit* m_folderEdit = nullptr;
    QLineEdit* m_gameEdit = nullptr;
    QLabel* m_packLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QCheckBox* m_followCheckBox = nullptr;
    QSpinBox* m_triggerSpinBox = nullptr;
    QPushButton* m_sendButton = nullptr;
    QListWidget* m_sentList = nullptr;
    PupScreenView* m_topper = nullptr;
    PupScreenView* m_backglass = nullptr;
    PupScreenView* m_dmd = nullptr;
    QTimer* m_timer = nullptr;
};

}  // namespace colorizing::app
