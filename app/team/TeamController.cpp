#include "team/TeamController.h"

#include <QAction>
#include <QDateTime>
#include <QEventLoop>
#include <QInputDialog>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <utility>

#include "edit/DocumentStateCommand.h"
#include "edit/EditBatch.h"
#include "edit/FrameMove.h"
#include "edit/LicenseEdits.h"
#include "edit/SceneEdits.h"
#include "edit/SpeakerLightEdits.h"
#include "edit/StructureCommand.h"
#include "team/TeamDialogs.h"
#include "team/TeamMerge.h"
#include "team/TeamSession.h"

#ifndef COLORIZINGDMD_GITHUB_CLIENT_ID
#define COLORIZINGDMD_GITHUB_CLIENT_ID ""
#endif

namespace colorizing::app {

namespace {

const QString kTokenKey = QStringLiteral("github/token");
const QString kLoginKey = QStringLiteral("github/login");
// How often the background sync writes and reads the claims and looks for shared changes.
constexpr std::chrono::seconds kSyncInterval{15};
constexpr int kInputDelayMs = 2000;

int64_t nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

document::ComponentKind kindOf(edit::RecordKind kind)
{
    switch (kind) {
    case edit::RecordKind::ComparisonMask:
        return document::ComponentKind::ComparisonMask;
    case edit::RecordKind::Background:
        return document::ComponentKind::Background;
    case edit::RecordKind::Sprite:
        return document::ComponentKind::Sprite;
    case edit::RecordKind::Frame:
        break;
    }
    return document::ComponentKind::Frame;
}

document::ComponentId idOf(const document::ProjectDocument& document,
                           document::ComponentKind kind,
                           std::size_t index)
{
    switch (kind) {
    case document::ComponentKind::Frame:
        return index < document.frameCount() ? document.frameMeta(index).id
                                             : document::ComponentId{};
    case document::ComponentKind::ComparisonMask:
        return index < document.comparisonMaskCount() ? document.comparisonMaskMeta(index).id
                                                      : document::ComponentId{};
    case document::ComponentKind::Background:
        return index < document.backgroundCount() ? document.backgroundMeta(index).id
                                                  : document::ComponentId{};
    case document::ComponentKind::Sprite:
        return index < document.spriteCount() ? document.spriteMeta(index).id
                                              : document::ComponentId{};
    case document::ComponentKind::Palette:
        return index < document.paletteCount() ? document.paletteMeta(index).id
                                               : document::ComponentId{};
    }
    return {};
}

}  // namespace

QString githubClientId()
{
    const QString fromEnvironment = qEnvironmentVariable("COLORIZINGDMD_GITHUB_CLIENT_ID");
    return fromEnvironment.isEmpty() ? QStringLiteral(COLORIZINGDMD_GITHUB_CLIENT_ID)
                                     : fromEnvironment;
}

TeamController::TeamController(QWidget* window, Host host)
    : m_window(window),
      m_host(std::move(host)),
      m_client(m_transport, githubClientId())
{
    QSettings settings;
    m_client.setToken(settings.value(kTokenKey).toString());
    m_login = m_client.token().isEmpty() ? QString() : settings.value(kLoginKey).toString();
    m_context = std::make_unique<QObject>();
    m_sync = std::make_unique<TeamSync>(
        m_context.get(), [this](TeamSync::Result result) { syncDelivered(std::move(result)); },
        kSyncInterval);
    m_avatars = std::make_unique<AvatarCache>(m_transport, [this]() { notifyTeamChanged(); });
    m_inputDelay.setSingleShot(true);
    m_inputDelay.setInterval(kInputDelayMs);
    QObject::connect(&m_inputDelay, &QTimer::timeout, m_context.get(),
                     [this]() { updateSync(); });
}

TeamController::~TeamController()
{
    // The thread stops before the context its answers go to.
    m_sync.reset();
    m_avatars.reset();
}

void TeamController::createMenu(QMenuBar* menuBar)
{
    QMenu* menu = menuBar->addMenu(tr("&Team"));
    menu->setToolTipsVisible(true);
    m_signInAction = menu->addAction(tr("Sign In To &GitHub..."));
    m_signInAction->setToolTip(tr("Sign in with your GitHub account, for team projects"));
    QObject::connect(m_signInAction, &QAction::triggered, m_window, [this]() { signIn(); });
    m_signOutAction = menu->addAction(tr("Sign &Out"));
    QObject::connect(m_signOutAction, &QAction::triggered, m_window, [this]() { signOut(); });
    menu->addSeparator();
    m_startAction = menu->addAction(tr("&Start Team Project..."));
    m_startAction->setToolTip(
        tr("Make this project a team project: a GitHub repository for the claims and the "
           "shared changes, and the partners invited"));
    QObject::connect(m_startAction, &QAction::triggered, m_window, [this]() { startTeam(); });
    m_shareAction = menu->addAction(tr("S&hare My Changes"));
    m_shareAction->setToolTip(
        tr("Save, then send what you changed since the last merge to the partners"));
    QObject::connect(m_shareAction, &QAction::triggered, m_window, [this]() { share(); });
    m_mergeAction = menu->addAction(tr("&Merge Changes..."));
    m_mergeAction->setToolTip(tr("Take the changes the partners shared into this project"));
    QObject::connect(m_mergeAction, &QAction::triggered, m_window, [this]() { merge(); });
    menu->addSeparator();
    m_reserveAction = menu->addAction(tr("&Reserve Selected Frames"));
    m_reserveAction->setToolTip(
        tr("Claim the selected frames before changing them, so that the partners leave them "
           "to you"));
    QObject::connect(m_reserveAction, &QAction::triggered, m_window,
                     [this]() { reserveSelected(); });
    m_releaseAction = menu->addAction(tr("Release My Reser&vations"));
    QObject::connect(m_releaseAction, &QAction::triggered, m_window,
                     [this]() { releaseReservations(); });
    m_refreshAction = menu->addAction(tr("Re&fresh Claims"));
    QObject::connect(m_refreshAction, &QAction::triggered, m_window,
                     [this]() { updateSync(true); });
    menu->addSeparator();
    m_breakLockAction = menu->addAction(tr("&Break The Lock..."));
    m_breakLockAction->setToolTip(
        tr("Go on alone from this copy: the partners start again from a new snapshot"));
    QObject::connect(m_breakLockAction, &QAction::triggered, m_window, [this]() { breakLock(); });
    QObject::connect(menu, &QMenu::aboutToShow, m_window, [this]() { updateActions(); });
    updateActions();
}

bool TeamController::isTeamProject() const
{
    const document::ProjectDocument* document = m_host.document();
    return document && document->team().has_value();
}

void TeamController::updateActions()
{
    if (!m_signInAction) {
        return;
    }
    const document::ProjectDocument* document = m_host.document();
    const bool signedIn = !m_login.isEmpty();
    const bool team = isTeamProject();
    m_signInAction->setText(signedIn ? tr("Signed In As %1").arg(m_login)
                                     : tr("Sign In To &GitHub..."));
    m_signInAction->setEnabled(!signedIn);
    m_signOutAction->setEnabled(signedIn);
    m_startAction->setEnabled(document && !team);
    for (QAction* action : {m_shareAction, m_mergeAction, m_reserveAction, m_releaseAction,
                            m_breakLockAction, m_refreshAction}) {
        action->setEnabled(team);
    }
}

void TeamController::projectChanged()
{
    m_claims.reset();
    m_reserved.clear();
    m_touched = {};
    m_pending.clear();
    m_checked.reset();
    updateActions();
    notifyTeamChanged();
    updateSync();
    if (!isTeamProject()) {
        return;
    }
    if (m_login.isEmpty()) {
        m_host.status(tr("A team project: sign in to GitHub (Team menu) to see the claims and "
                         "share changes"),
                      8000);
        return;
    }
    const document::TeamState& team = *m_host.document()->team();
    if (std::find(team.members.begin(), team.members.end(), m_login.toStdString()) ==
        team.members.end()) {
        m_host.status(tr("You (%1) are not one of this team project's members").arg(m_login),
                      8000);
    }
}

bool TeamController::ensureSignedIn()
{
    if (!m_login.isEmpty()) {
        return true;
    }
    signIn();
    return !m_login.isEmpty();
}

void TeamController::signIn()
{
    if (m_client.clientId().isEmpty()) {
        QMessageBox::information(
            m_window, tr("Sign In To GitHub"),
            tr("This build has no GitHub app to sign in through. Builds of the PPUC project have "
               "one; for another build, set COLORIZINGDMD_GITHUB_CLIENT_ID to the client ID of "
               "a GitHub OAuth app with the device flow enabled."));
        return;
    }
    SignInDialog dialog(m_client, m_window);
    if (dialog.exec() != QDialog::Accepted) {
        m_client.setToken(QSettings().value(kTokenKey).toString());
        return;
    }
    m_login = dialog.login();
    QSettings settings;
    settings.setValue(kTokenKey, dialog.token());
    settings.setValue(kLoginKey, m_login);
    m_host.status(tr("Signed in to GitHub as %1").arg(m_login), 5000);
    projectChanged();
}

void TeamController::signOut()
{
    QSettings settings;
    settings.remove(kTokenKey);
    settings.remove(kLoginKey);
    m_client.setToken(QString());
    m_login.clear();
    m_claims.reset();
    m_pending.clear();
    m_checked.reset();
    updateSync();
    updateActions();
    notifyTeamChanged();
    if (m_host.claimsChanged) {
        m_host.claimsChanged();
    }
    m_host.status(tr("Signed out of GitHub"), 4000);
}

void TeamController::runModal(const QString& title,
                              const std::function<void(std::function<void()>)>& start)
{
    QProgressDialog dialog(title, QString(), 0, 0, m_window);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setCancelButton(nullptr);
    dialog.setMinimumDuration(300);
    QEventLoop loop;
    bool finished = false;
    start([&finished, &loop]() {
        finished = true;
        loop.quit();
    });
    if (!finished) {
        loop.exec();
    }
}

std::unique_ptr<TeamRemote> TeamController::remote() const
{
    return std::make_unique<GitHubRemote>(
        const_cast<GitHubClient&>(m_client),
        QString::fromStdString(m_host.document()->team()->repository));
}

QString TeamController::teamProblem() const
{
    if (!isTeamProject()) {
        return tr("The project is no team project.");
    }
    if (m_claims && m_claims->lockBreak &&
        m_claims->lockBreak->snapshotId != m_host.document()->team()->snapshotId &&
        m_claims->lockBreak->login != m_login.toStdString()) {
        return tr("%1 broke the lock on %2 and goes on alone. Start again from their new "
                  "snapshot of the project; to give them what you changed meanwhile, export "
                  "it as a package (File -> Export Selected Frames As Package).")
            .arg(QString::fromStdString(m_claims->lockBreak->login),
                 QLocale().toString(QDateTime::fromMSecsSinceEpoch(m_claims->lockBreak->sinceMs),
                                    QLocale::ShortFormat));
    }
    return {};
}

void TeamController::startTeam()
{
    document::ProjectDocument* document = m_host.document();
    if (!document || document->team() || !ensureSignedIn()) {
        return;
    }
    if ((document->isDirty() || m_host.projectPath().isEmpty()) && !m_host.save()) {
        return;
    }
    StartTeamDialog dialog(QString::fromStdString(document->romName()), m_login, m_window);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString repository;
    QString error;
    QStringList notInvited;
    runModal(tr("Creating the team's repository…"), [&](std::function<void()> finished) {
        m_client.createRepository(
            dialog.owner(), dialog.repositoryName(),
            tr("Claims and shared changes of the colorization %1")
                .arg(QString::fromStdString(document->romName())),
            [&, finished](QString fullName, QString createError) {
                repository = fullName;
                error = createError;
                finished();
            });
    });
    if (repository.isEmpty()) {
        QMessageBox::warning(m_window, tr("Start Team Project"),
                             tr("The repository could not be created: %1").arg(error));
        return;
    }
    for (const QString& partner : dialog.partners()) {
        runModal(tr("Inviting %1…").arg(partner), [&](std::function<void()> finished) {
            m_client.inviteCollaborator(repository, partner,
                                        [&, partner, finished](QString inviteError) {
                                            if (!inviteError.isEmpty()) {
                                                notInvited << QStringLiteral("%1 (%2)").arg(
                                                    partner, inviteError);
                                            }
                                            finished();
                                        });
        });
    }
    std::vector<std::string> members;
    for (const QString& partner : dialog.partners()) {
        members.push_back(partner.toStdString());
    }
    try {
        document->setTeam(team::startTeam(*document, repository.toStdString(),
                                          m_login.toStdString(), members));
    } catch (const std::exception& ex) {
        QMessageBox::warning(m_window, tr("Start Team Project"), QString::fromUtf8(ex.what()));
        return;
    }
    if (!m_host.save()) {
        return;
    }
    projectChanged();
    QMessageBox::information(
        m_window, tr("Start Team Project"),
        tr("The team project is started in %1. Now give the partners the project file once, "
           "for example through Google Drive: %2. They open it, sign in, and share and merge "
           "from the Team menu.%3")
            .arg(repository, m_host.projectPath(),
                 notInvited.isEmpty()
                     ? QString()
                     : tr(" Not invited: %1.").arg(notInvited.join(QStringLiteral(", ")))));
}

void TeamController::share()
{
    document::ProjectDocument* document = m_host.document();
    if (!document || !ensureSignedIn()) {
        return;
    }
    if (const QString problem = teamProblem(); !problem.isEmpty()) {
        QMessageBox::warning(m_window, tr("Share My Changes"), problem);
        return;
    }
    if ((document->isDirty() || m_host.projectPath().isEmpty()) && !m_host.save()) {
        return;
    }
    bool changed = true;
    try {
        changed = !team::changesSinceCommon(*document).empty();
    } catch (const std::exception&) {
        // The share says why.
    }
    if (!changed) {
        m_host.status(tr("Nothing to share: nothing changed since the last merge"), 5000);
        return;
    }
    bool accepted = false;
    const QString comment = QInputDialog::getMultiLineText(
        m_window, tr("Share My Changes"),
        tr("A short comment for the partners: what you changed, what is still open "
           "(optional)."),
        QString(), &accepted);
    if (!accepted) {
        return;
    }
    const std::unique_ptr<TeamRemote> teamRemote = remote();
    TeamSession session(*teamRemote, m_login);
    QString error;
    bool nothing = false;
    runModal(tr("Sharing your changes…"), [&](std::function<void()> finished) {
        session.share(*document, nowMs(), comment, [&, finished](QString shareError,
                                                                 bool none) {
            error = shareError;
            nothing = none;
            finished();
        });
    });
    if (nothing) {
        m_host.status(tr("Nothing to share: nothing changed since the last merge"), 5000);
        return;
    }
    if (document->isDirty()) {
        // The package is recorded as sent.
        (void)m_host.save();
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(m_window, tr("Share My Changes"), error);
        return;
    }
    updateSync();
    m_host.status(tr("Shared your changes with the partners"), 5000);
}

void TeamController::merge()
{
    document::ProjectDocument* document = m_host.document();
    if (!document || !ensureSignedIn()) {
        return;
    }
    if (const QString problem = teamProblem(); !problem.isEmpty()) {
        QMessageBox::warning(m_window, tr("Merge Changes"), problem);
        return;
    }
    if ((document->isDirty() || m_host.projectPath().isEmpty()) && !m_host.save()) {
        return;
    }
    const std::unique_ptr<TeamRemote> teamRemote = remote();
    TeamSession session(*teamRemote, m_login);
    std::vector<QString> pending;
    QString error;
    runModal(tr("Looking for the partners' changes…"), [&](std::function<void()> finished) {
        session.pendingPackages(*document, [&, finished](std::vector<QString> names,
                                                         QString listError) {
            pending = std::move(names);
            error = listError;
            finished();
        });
    });
    if (!error.isEmpty()) {
        QMessageBox::warning(m_window, tr("Merge Changes"), error);
        return;
    }
    if (pending.empty()) {
        m_host.status(tr("No changes from the partners to merge"), 5000);
        return;
    }
    std::size_t merged = 0;
    team::TeamMergeCommand::Counts total;
    for (const QString& name : pending) {
        std::optional<team::ChangePackage> package;
        // Downloaded in the background already, as a rule.
        const std::optional<QByteArray> cached = TeamSync::cachedPackage(cacheFolder(), name);
        if (cached) {
            package = changePackageFrom(*cached, error);
        }
        if (!package) {
            runModal(tr("Fetching the partners' changes…"), [&](std::function<void()> finished) {
                session.fetchPackage(
                    name, [&, finished](std::optional<team::ChangePackage> answer,
                                        QString fetchError) {
                        package = std::move(answer);
                        error = fetchError;
                        finished();
                    });
            });
        }
        if (!package) {
            QMessageBox::warning(m_window, tr("Merge Changes"),
                                 tr("A package could not be read: %1").arg(error));
            break;
        }
        const team::MergePlan plan = team::planMerge(*document, *package);
        if (!plan.problem.empty()) {
            QMessageBox::warning(m_window, tr("Merge Changes"),
                                 QString::fromStdString(plan.problem));
            break;
        }
        team::MergeChoices choices;
        if (plan.conflictCount() > 0) {
            MergeDialog dialog(*document, *package, plan, m_host.projectPath(), m_window);
            if (dialog.exec() != QDialog::Accepted) {
                break;
            }
            choices = dialog.choices();
        }
        try {
            auto command =
                team::mergeChangePackage(*document, *package, choices, m_login.toStdString());
            const team::TeamMergeCommand::Counts counts = command->counts();
            total.taken += counts.taken;
            total.added += counts.added;
            total.deleted += counts.deleted;
            total.conflicts += counts.conflicts;
            m_host.record(std::move(command));
            ++merged;
        } catch (const std::exception& ex) {
            QMessageBox::warning(m_window, tr("Merge Changes"), QString::fromUtf8(ex.what()));
            break;
        }
    }
    if (merged == 0) {
        return;
    }
    // Merged: no longer pending, before the next sync says so.
    m_pending.erase(m_pending.begin(),
                    m_pending.begin() + static_cast<std::ptrdiff_t>(
                                            std::min(merged, m_pending.size())));
    notifyTeamChanged();
    // What was merged gets this copy's revisions (the partners' own), and the claims follow.
    (void)m_host.save();
    updateSync();
    m_host.status(tr("Merged %1 package(s): %2 taken, %3 added, %4 deleted, %5 conflict(s)")
                      .arg(static_cast<qulonglong>(merged))
                      .arg(static_cast<qulonglong>(total.taken))
                      .arg(static_cast<qulonglong>(total.added))
                      .arg(static_cast<qulonglong>(total.deleted))
                      .arg(static_cast<qulonglong>(total.conflicts)),
                  8000);
}

void TeamController::reserveSelected()
{
    document::ProjectDocument* document = m_host.document();
    if (!document || !isTeamProject() || !ensureSignedIn()) {
        return;
    }
    std::size_t count = 0;
    for (const std::size_t frame : m_host.selectedFrames()) {
        const team::Reservation reservation{document::ComponentKind::Frame,
                                            document->frameMeta(frame).id};
        if (m_claims && team::partnerClaim(*m_claims, reservation.first, reservation.second,
                                           m_login.toStdString())) {
            continue;
        }
        if (std::find(m_reserved.begin(), m_reserved.end(), reservation) == m_reserved.end()) {
            m_reserved.push_back(reservation);
            ++count;
        }
    }
    updateSync(true);
    m_host.status(tr("Reserved %n frame(s)", "", static_cast<int>(count)), 4000);
}

void TeamController::releaseReservations()
{
    m_reserved.clear();
    updateSync(true);
}

void TeamController::breakLock()
{
    document::ProjectDocument* document = m_host.document();
    if (!document || !isTeamProject() || !ensureSignedIn()) {
        return;
    }
    const document::TeamState team = *document->team();
    const bool owner = team.ownerLogin == m_login.toStdString();
    QMessageBox question(m_window);
    question.setIcon(QMessageBox::Warning);
    question.setWindowTitle(tr("Break The Lock"));
    question.setText(owner ? tr("Go on from this copy, with a new snapshot?")
                           : tr("Go on alone from this copy, in a fork of the repository?"));
    question.setInformativeText(
        owner ? tr("The partners' claims stop counting, and their shared changes that you have "
                   "not merged are not merged any more. They start again from the project file "
                   "you give them after this.")
              : tr("The editor makes a fork of the team's repository in your account, and you "
                   "go on there alone, as its owner. The team's repository stays as it is."));
    QPushButton* breakButton =
        question.addButton(tr("Break The Lock"), QMessageBox::DestructiveRole);
    question.addButton(QMessageBox::Cancel);
    question.exec();
    if (question.clickedButton() != breakButton) {
        return;
    }
    if ((document->isDirty() || m_host.projectPath().isEmpty()) && !m_host.save()) {
        return;
    }
    QString repository = QString::fromStdString(team.repository);
    QString error;
    if (!owner) {
        runModal(tr("Forking the repository…"), [&](std::function<void()> finished) {
            m_client.forkRepository(repository, [&, finished](QString fullName,
                                                              QString forkError) {
                repository = fullName;
                error = forkError;
                finished();
            });
        });
        if (repository.isEmpty()) {
            QMessageBox::warning(m_window, tr("Break The Lock"),
                                 tr("The repository could not be forked: %1").arg(error));
            return;
        }
    }
    std::vector<std::string> members;
    if (owner) {
        for (const std::string& member : team.members) {
            if (member != team.ownerLogin) {
                members.push_back(member);
            }
        }
    }
    document::TeamState renewed;
    try {
        renewed = team::startTeam(*document, repository.toStdString(), m_login.toStdString(),
                                  members);
    } catch (const std::exception& ex) {
        QMessageBox::warning(m_window, tr("Break The Lock"), QString::fromUtf8(ex.what()));
        return;
    }
    // The claims start anew, with the break for those who still look at them.
    team::Claims claims;
    claims.lockBreak = team::LockBreak{m_login.toStdString(), nowMs(), renewed.snapshotId};
    GitHubRemote target(m_client, repository);
    runModal(tr("Writing the claims…"), [&](std::function<void()> finished) {
        target.readClaims([&, finished](std::optional<QByteArray>, QString version,
                                        QString readError) {
            if (!readError.isEmpty()) {
                error = readError;
                finished();
                return;
            }
            target.writeClaims(QByteArray::fromStdString(team::writeClaims(claims)), version,
                               [&, finished](QString writeError, bool) {
                                   error = writeError;
                                   finished();
                               });
        });
    });
    if (!error.isEmpty()) {
        QMessageBox::warning(m_window, tr("Break The Lock"),
                             tr("The claims could not be written: %1").arg(error));
        return;
    }
    document->setTeam(std::move(renewed));
    if (!m_host.save()) {
        return;
    }
    projectChanged();
    QMessageBox::information(
        m_window, tr("Break The Lock"),
        owner ? tr("The lock is broken. Give the partners the saved project file again: they "
                   "start from it.")
              : tr("You go on alone in %1.").arg(repository));
}

QString TeamController::cacheFolder() const
{
    const document::ProjectDocument* document = m_host.document();
    if (!document || !document->team()) {
        return {};
    }
    QString repository = QString::fromStdString(document->team()->repository);
    repository.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("team/") + repository);
}

void TeamController::updateSync(bool report)
{
    m_inputDelay.stop();
    TeamSync::Input input;
    const document::ProjectDocument* document = m_host.document();
    input.active = document && document->team() && !m_login.isEmpty() &&
                   !m_client.clientId().isEmpty();
    if (input.active) {
        const document::TeamState& team = *document->team();
        input.clientId = m_client.clientId();
        input.token = m_client.token();
        input.login = m_login;
        input.repository = QString::fromStdString(team.repository);
        try {
            input.claimed = team::changesSinceCommon(*document);
        } catch (const std::exception&) {
            input.active = false;
        }
        // What was edited since the last save counts as changed: claimed early.
        for (const auto& [kind, id] : m_touched.components) {
            input.claimed.changed[document::kindIndex(kind)].emplace(id, document::ComponentId{});
        }
        input.claimed.orderChanged = input.claimed.orderChanged || m_touched.order;
        input.claimed.settingsChanged = input.claimed.settingsChanged || m_touched.settings;
        input.reserved = m_reserved;
        input.mergedPackages = team.mergedPackages;
        input.cacheFolder = cacheFolder();
    }
    m_reportNext = m_reportNext || report;
    m_syncInput = m_sync->setInput(std::move(input));
}

void TeamController::syncDelivered(TeamSync::Result result)
{
    if (result.input != m_syncInput || !isTeamProject()) {
        return;
    }
    const bool report = m_reportNext;
    m_reportNext = false;
    if (!result.claims) {
        if (report) {
            QMessageBox::warning(m_window, tr("Team"),
                                 tr("The team's claims could not be read: %1").arg(result.error));
        } else {
            m_host.status(tr("The team's claims could not be read: %1").arg(result.error), 6000);
        }
        return;
    }
    m_claims = std::move(result.claims);
    const std::size_t before = m_pending.size();
    m_pending = std::move(result.pendingPackages);
    m_comments = std::move(result.comments);
    m_checked = QDateTime::currentDateTime();
    notifyTeamChanged();
    if (m_pending.size() > before) {
        m_host.status(tr("%n shared change(s) from the partners to merge (Team section or "
                         "Team -> Merge Changes)",
                         "", static_cast<int>(m_pending.size())),
                      10000);
    }
    if (m_host.claimsChanged) {
        m_host.claimsChanged();
    }
    if (const QString problem = teamProblem(); !problem.isEmpty()) {
        if (report) {
            QMessageBox::warning(m_window, tr("Team"), problem);
        } else {
            m_host.status(problem, 10000);
        }
    } else if (report) {
        m_host.status(tr("The claims are up to date"), 3000);
    }
}

void TeamController::notifyTeamChanged()
{
    if (m_mergeAction) {
        m_mergeAction->setText(m_pending.empty()
                                   ? tr("&Merge Changes...")
                                   : tr("&Merge Changes (%1)...").arg(m_pending.size()));
    }
    if (m_host.teamChanged) {
        m_host.teamChanged();
    }
}

TeamPanel::State TeamController::panelState() const
{
    TeamPanel::State state;
    if (!isTeamProject()) {
        return state;
    }
    state.login = m_login;
    state.repository = QString::fromStdString(m_host.document()->team()->repository);
    state.pendingPackages = m_pending;
    state.comments = m_comments;
    for (const QString& name : m_pending) {
        if (const std::optional<PackageName> parsed = parsePackageName(name)) {
            const QImage avatar = m_avatars->avatar(parsed->login);
            if (!avatar.isNull()) {
                state.avatars[parsed->login] = avatar;
            }
        }
    }
    state.checked = m_checked;
    state.problem = teamProblem();
    return state;
}

void TeamController::deliverSyncResultForTest(TeamSync::Result result)
{
    result.input = m_syncInput;
    syncDelivered(std::move(result));
}

void TeamController::projectSaved()
{
    if (!isTeamProject()) {
        return;
    }
    m_touched = {};
    updateSync();
}

QString TeamController::claimText(const team::Claim& claim) const
{
    return tr("%1 works on it since %2")
        .arg(QString::fromStdString(claim.login),
             QLocale().toString(QDateTime::fromMSecsSinceEpoch(claim.sinceMs),
                                QLocale::ShortFormat));
}

QString TeamController::frameClaimText(std::size_t frameIndex) const
{
    const document::ProjectDocument* document = m_host.document();
    if (!m_claims || !document || frameIndex >= document->frameCount()) {
        return {};
    }
    const team::Claim* claim =
        team::partnerClaim(*m_claims, document::ComponentKind::Frame,
                           document->frameMeta(frameIndex).id, m_login.toStdString());
    return claim ? claimText(*claim) : QString();
}

TeamController::Touched TeamController::touchedBy(const edit::EditCommand& command,
                                                  document::ProjectDocument& document) const
{
    Touched touched;
    if (dynamic_cast<const edit::DocumentStateCommand*>(&command)) {
        // A package import or a merge: it adds what it brings, and a merge is no claim.
        return touched;
    }
    const auto add = [&](document::ComponentKind kind, std::size_t index) {
        const document::ComponentId id = idOf(document, kind, index);
        if (!id.isNull()) {
            touched.components.emplace_back(kind, id);
        }
    };
    const edit::EditTarget target = command.target();
    for (const std::size_t frame : target.frameIndices) {
        add(document::ComponentKind::Frame, frame);
    }
    switch (target.kind) {
    case edit::EditKind::FramePixels:
    case edit::EditKind::DynamicMask:
    case edit::EditKind::DynamicColor:
    case edit::EditKind::DynamicShadow:
    case edit::EditKind::BackgroundMask:
    case edit::EditKind::FrameAttributes:
        add(document::ComponentKind::Frame, target.index);
        break;
    case edit::EditKind::ComparisonMask:
        add(document::ComponentKind::ComparisonMask, target.index);
        break;
    case edit::EditKind::BackgroundImage:
        add(document::ComponentKind::Background, target.index);
        break;
    case edit::EditKind::SpriteImage:
    case edit::EditKind::SpriteAttributes:
        add(document::ComponentKind::Sprite, target.index);
        break;
    case edit::EditKind::Palette:
        add(document::ComponentKind::Palette, target.index);
        break;
    case edit::EditKind::EditorColors:
    case edit::EditKind::Sections:
    case edit::EditKind::HelperLines:
        touched.settings = true;
        break;
    case edit::EditKind::ProjectStructure:
        if (const auto* structure = dynamic_cast<const edit::StructureCommand*>(&command)) {
            if (!structure->addsRecords()) {
                // The deleted records, by the IDs they had: back for a moment.
                touched.order = structure->recordKind() == edit::RecordKind::Frame;
                command.revert(document);
                for (const std::size_t index : structure->recordIndices()) {
                    add(kindOf(structure->recordKind()), index);
                }
                command.apply(document);
            }
        } else if (dynamic_cast<const edit::FrameMoveCommand*>(&command)) {
            touched.order = true;
        } else if (const auto* batch = dynamic_cast<const edit::EditBatchCommand*>(&command)) {
            touched.order = batch->changesStructure();
        } else if (dynamic_cast<const edit::SceneEditCommand*>(&command) ||
                   dynamic_cast<const edit::SpeakerLightsCommand*>(&command) ||
                   dynamic_cast<const edit::LicenseCommand*>(&command)) {
            touched.settings = true;
        }
        break;
    }
    return touched;
}

std::optional<QString> TeamController::refusalFor(const Touched& touched) const
{
    if (!m_claims) {
        return std::nullopt;
    }
    const std::string me = m_login.toStdString();
    for (const auto& [kind, id] : touched.components) {
        if (const team::Claim* claim = team::partnerClaim(*m_claims, kind, id, me)) {
            return claimText(*claim);
        }
    }
    if (touched.order && m_claims->order && m_claims->order->login != me) {
        return tr("%1 is moving or deleting frames").arg(
            QString::fromStdString(m_claims->order->login));
    }
    if (touched.settings && m_claims->settings && m_claims->settings->login != me) {
        return tr("%1 is changing the project's settings").arg(
            QString::fromStdString(m_claims->settings->login));
    }
    return std::nullopt;
}

std::optional<QString> TeamController::refusal(const edit::EditCommand& command,
                                               document::ProjectDocument& document) const
{
    if (!m_claims || !document.team()) {
        return std::nullopt;
    }
    return refusalFor(touchedBy(command, document));
}

std::optional<QString> TeamController::admitEdit(const edit::EditCommand& command,
                                                 document::ProjectDocument& document)
{
    if (!document.team()) {
        return std::nullopt;
    }
    const Touched touched = touchedBy(command, document);
    if (std::optional<QString> refused = refusalFor(touched)) {
        return refused;
    }
    bool more = (touched.order && !m_touched.order) || (touched.settings && !m_touched.settings);
    m_touched.order = m_touched.order || touched.order;
    m_touched.settings = m_touched.settings || touched.settings;
    for (const team::Reservation& component : touched.components) {
        if (std::find(m_touched.components.begin(), m_touched.components.end(), component) ==
            m_touched.components.end()) {
            m_touched.components.push_back(component);
            more = true;
        }
    }
    // Something not claimed yet: the partners hear of it after the burst of edits.
    if (more && !m_inputDelay.isActive()) {
        m_inputDelay.start();
    }
    return std::nullopt;
}

}  // namespace colorizing::app
