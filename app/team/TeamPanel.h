#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "team/TeamSession.h"

class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QListWidget;
class QPushButton;

namespace colorizing::app {

// The Team section of the right side bar, for a team project: the repository, who is signed
// in, the partners' shared changes waiting to be merged (as the background sync last found
// them) and the buttons to merge them and to share this copy's changes. It shows what the
// TeamController gives it; the buttons ask the controller.
class TeamPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(TeamPanel)

public:
    struct State {
        // Empty while signed out.
        QString login;
        // "owner/name".
        QString repository;
        // The packages' names in packages/ (parsePackageName()), oldest first.
        std::vector<QString> pendingPackages;
        // What is known of them, by name (TeamSync::Result::notes), and the authors' avatars,
        // by login.
        std::map<QString, PendingNote> notes;
        std::map<QString, QImage> avatars;
        // When the background sync last answered; nothing before its first answer.
        std::optional<QDateTime> checked;
        // Why the team steps cannot run (a broken lock); empty when they can.
        QString problem;
    };

    explicit TeamPanel(QWidget* parent = nullptr);

    void setState(const State& state);

    // For tests: the lines of the list of shared changes, and the texts shown.
    [[nodiscard]] QStringList pendingTexts() const;
    [[nodiscard]] QString accountText() const;
    [[nodiscard]] QString checkedText() const;
    [[nodiscard]] bool hasAvatar(int row) const;

    std::function<void()> onSignInRequested;
    std::function<void()> onMergeRequested;
    std::function<void()> onShareRequested;
    // Add File, or a file dropped on the section: a shared change a partner sent as a file.
    std::function<void(const QString& path)> onAddFileRequested;

private:
    QLabel* m_accountLabel = nullptr;
    QPushButton* m_signInButton = nullptr;
    QLabel* m_problemLabel = nullptr;
    QLabel* m_pendingLabel = nullptr;
    QListWidget* m_pendingList = nullptr;
    QLabel* m_checkedLabel = nullptr;
    QPushButton* m_mergeButton = nullptr;
    QPushButton* m_shareButton = nullptr;
    QPushButton* m_addFileButton = nullptr;
    bool m_signedIn = false;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

}  // namespace colorizing::app
