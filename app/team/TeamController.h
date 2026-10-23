#pragma once

#include <QCoreApplication>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "team/Claims.h"
#include "team/GitHub.h"

class QAction;
class QMenuBar;
class QWidget;

namespace colorizing::app {

class TeamRemote;

// The GitHub OAuth app the editor signs in through: the environment's
// COLORIZINGDMD_GITHUB_CLIENT_ID, else the build's; empty when there is none.
[[nodiscard]] QString githubClientId();

// The Team menu and what it does (design: "Working as a team"): signing in to GitHub, starting
// a team project, sharing this copy's changes, merging the partners', reserving frames,
// breaking the lock, and the claims that keep a partner's components read-only here. The main
// window lends it the project and the steps it needs (Host).
class TeamController {
    Q_DECLARE_TR_FUNCTIONS(TeamController)

public:
    struct Host {
        // The open project; null without one.
        std::function<document::ProjectDocument*()> document;
        // The project file; empty while the project has none.
        std::function<QString()> projectPath;
        // Saves the project (asking for a file when it has none); false when it was not.
        std::function<bool()> save;
        // Records an applied step in the structure history and refreshes the window.
        std::function<void(std::unique_ptr<edit::EditCommand>)> record;
        std::function<void(const QString& message, int timeoutMs)> status;
        std::function<std::vector<std::size_t>()> selectedFrames;
        // After the claims changed: the window shows them again.
        std::function<void()> claimsChanged;
    };

    TeamController(QWidget* window, Host host);
    ~TeamController();

    void createMenu(QMenuBar* menuBar);
    // Another project was opened or made: its claims are read, the actions follow.
    void projectChanged();

    // The GitHub login, empty when signed out.
    [[nodiscard]] const QString& login() const { return m_login; }
    [[nodiscard]] bool isTeamProject() const;
    // Why the applied edit must not be kept: a partner works on what it changes (the claim's
    // owner and since when); nothing when it may. For a deletion it looks at the records by
    // undoing and redoing it.
    [[nodiscard]] std::optional<QString> refusal(const edit::EditCommand& command,
                                                 document::ProjectDocument& document) const;
    // Takes these claims as the team's, signed in as login: for tests and tools.
    void setClaims(team::Claims claims, QString login)
    {
        m_claims = std::move(claims);
        m_login = std::move(login);
    }
    // "enthusiast works on it since 14:02" for a frame a partner works on; empty otherwise.
    [[nodiscard]] QString frameClaimText(std::size_t frameIndex) const;

private:
    void signIn();
    void signOut();
    void startTeam();
    void share();
    void merge();
    void reserveSelected();
    void releaseReservations();
    void breakLock();
    // Quiet: in the background, the window goes on; else with a busy window and messages.
    void refreshClaims(bool quiet);
    void takeClaims(std::optional<team::Claims> claims, const QString& error, bool quiet);
    void updateActions();
    // Asks to sign in when signed out; false when the user did not.
    bool ensureSignedIn();
    // Runs a step that answers through `finished`, with a busy window after a moment; returns
    // when it has answered (every request ends within its timeout).
    void runModal(const QString& title, const std::function<void(std::function<void()>)>& start);
    // Why a team step cannot run now; empty when it can.
    [[nodiscard]] QString teamProblem() const;
    [[nodiscard]] std::unique_ptr<TeamRemote> remote() const;
    [[nodiscard]] QString claimText(const team::Claim& claim) const;

    QWidget* m_window;
    Host m_host;
    NetworkTransport m_transport;
    GitHubClient m_client;
    QString m_login;
    std::optional<team::Claims> m_claims;
    // Counts the claims requests; an answer to an older one is dropped.
    uint64_t m_claimsRequest = 0;
    std::vector<team::Reservation> m_reserved;
    QTimer m_poll;
    QAction* m_signInAction = nullptr;
    QAction* m_signOutAction = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_shareAction = nullptr;
    QAction* m_mergeAction = nullptr;
    QAction* m_reserveAction = nullptr;
    QAction* m_releaseAction = nullptr;
    QAction* m_breakLockAction = nullptr;
    QAction* m_refreshAction = nullptr;
};

}  // namespace colorizing::app
