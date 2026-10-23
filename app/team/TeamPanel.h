#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

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

    std::function<void()> onSignInRequested;
    std::function<void()> onMergeRequested;
    std::function<void()> onShareRequested;

private:
    QLabel* m_accountLabel = nullptr;
    QPushButton* m_signInButton = nullptr;
    QLabel* m_problemLabel = nullptr;
    QLabel* m_pendingLabel = nullptr;
    QListWidget* m_pendingList = nullptr;
    QLabel* m_checkedLabel = nullptr;
    QPushButton* m_mergeButton = nullptr;
    QPushButton* m_shareButton = nullptr;
};

}  // namespace colorizing::app
