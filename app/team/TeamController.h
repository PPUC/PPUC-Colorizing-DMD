#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "team/Avatars.h"
#include "team/Claims.h"
#include "team/GitHub.h"
#include "team/TeamPanel.h"
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
        // After what the Team section shows changed (panelState()).
        std::function<void()> teamChanged;
        // Each author's time spent on the project (ProjectTime), for a shared package, and
        // the partner's of a merged one.
        std::function<std::map<std::string, uint64_t>()> authorTimes;
        std::function<void(const std::map<std::string, uint64_t>&)> mergeAuthorTimes;
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
    // What the Team section shows.
    [[nodiscard]] TeamPanel::State panelState() const;
    // Takes the sync's answer as if the background sync had delivered it: for tests.
    void deliverSyncResultForTest(TeamSync::Result result);

    // The steps of the Team menu, for the Team section's buttons.
    void signIn();
    void share();
    void merge();
    // Adds a shared change a partner sent as a file (Team section); asks for the file without
    // a path.
    void addPackageFile(const QString& path = QString());
    // Takes these claims as the team's, signed in as login: for tests and tools.
    void setClaims(team::Claims claims, QString login)
    {
        m_claims = std::move(claims);
        m_login = std::move(login);
    }
    // The frames a partner works on (blocking claims), with the partner's login.
    [[nodiscard]] std::vector<std::pair<std::size_t, QString>> claimedFrames() const;
    // A member's avatar; a null image until it is loaded (teamChanged() follows then).
    [[nodiscard]] QImage avatar(const QString& login) const { return m_avatars->avatar(login); }
    // "enthusiast works on it since 14:02" for a frame a partner works on; empty otherwise.
    [[nodiscard]] QString frameClaimText(std::size_t frameIndex) const;

private:
    void signOut();
    void startTeam();
    // Large shared changes: the file to save them as; empty to send them through GitHub after
    // all, nothing when the author cancelled.
    [[nodiscard]] std::optional<QString> askForShareFile(const team::ChangePackage& package,
                                                         qint64 sizeBytes);
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
    // The merge action's text and the Team section follow the pending packages.
    void notifyTeamChanged();
    // Asks to sign in when signed out; false when the user did not.
    bool ensureSignedIn();
    // Runs a step that answers through `finished`, with a busy window after a moment; returns
    // when it has answered (every request ends within its timeout).
    void runModal(const QString& title, const std::function<void(std::function<void()>)>& start);
    // Why a team step cannot run now; empty when it can.
    [[nodiscard]] QString teamProblem() const;
    [[nodiscard]] std::unique_ptr<TeamRemote> remote() const;
    [[nodiscard]] QString claimText(const team::Claim& claim) const;
    // A partner's claim that keeps this copy from changing the component (team::blockingClaim):
    // not one whose version this copy merged.
    [[nodiscard]] const team::Claim* blockingClaim(document::ComponentKind kind,
                                                   const document::ComponentId& id) const;

    QWidget* m_window;
    Host m_host;
    NetworkTransport m_transport;
    GitHubClient m_client;
    QString m_login;
    std::optional<team::Claims> m_claims;
    // Edited since the last save (early claims).
    Touched m_touched;
    std::vector<QString> m_pending;
    // What is known of the pending packages, by name (TeamSync::Result::notes).
    std::map<QString, PendingNote> m_notes;
    // The partners' avatars for the Team section.
    std::unique_ptr<AvatarCache> m_avatars;
    // When the background sync last answered for this project.
    std::optional<QDateTime> m_checked;
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
