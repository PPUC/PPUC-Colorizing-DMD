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
#include "team/TeamSync.h"

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
    // refusal(), and for an edit that may be kept: what it changes is claimed from now on, as
    // the partners see a little later, without a save (early claims).
    [[nodiscard]] std::optional<QString> admitEdit(const edit::EditCommand& command,
                                                   document::ProjectDocument& document);
    // After a save: the saved changes claim what the edits did until then.
    void projectSaved();
    // The partners' shared changes not merged yet, as the background sync last found them.
    [[nodiscard]] std::size_t pendingPackageCount() const { return m_pending.size(); }
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
    // What an edit changes: components, and whether the frame order and the settings.
    struct Touched {
        std::vector<team::Reservation> components;
        bool order = false;
        bool settings = false;
    };
    [[nodiscard]] Touched touchedBy(const edit::EditCommand& command,
                                    document::ProjectDocument& document) const;
    [[nodiscard]] std::optional<QString> refusalFor(const Touched& touched) const;
    // Gives the background sync what this copy claims now; report: the next result's
    // problems are shown, not only put in the status bar (a step the user asked for).
    void updateSync(bool report = false);
    void syncDelivered(TeamSync::Result result);
    [[nodiscard]] QString cacheFolder() const;
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
    // Edited since the last save (early claims).
    Touched m_touched;
    std::vector<QString> m_pending;
    // The input the last result must answer; older answers are dropped.
    uint64_t m_syncInput = 0;
    bool m_reportNext = false;
    // The background sync calls back through this object; it goes after the sync.
    std::unique_ptr<QObject> m_context;
    std::unique_ptr<TeamSync> m_sync;
    // Edits come in bursts: the sync hears of them a moment later.
    QTimer m_inputDelay;
    std::vector<team::Reservation> m_reserved;
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
