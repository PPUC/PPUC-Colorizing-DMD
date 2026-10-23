#include "team/TeamDialogs.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "FrameImageUtils.h"
#include "document/EditorMetadata.h"
#include "project/ProjectFile.h"

namespace colorizing::app {

SignInDialog::SignInDialog(GitHubClient& client, QWidget* parent)
    : QDialog(parent),
      m_client(client)
{
    setWindowTitle(tr("Sign In To GitHub"));
    setModal(true);
    auto* intro = new QLabel(
        tr("Enter this code on GitHub's page, which opens in your browser, and allow PPUC "
           "Colorizing DMD access. Your password stays with GitHub."),
        this);
    intro->setWordWrap(true);
    m_codeLabel = new QLabel(QStringLiteral("…"), this);
    QFont large = m_codeLabel->font();
    large.setPointSizeF(large.pointSizeF() * 2);
    large.setBold(true);
    m_codeLabel->setFont(large);
    m_codeLabel->setAlignment(Qt::AlignCenter);
    m_codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusLabel = new QLabel(tr("Asking GitHub for a code…"), this);
    m_statusLabel->setWordWrap(true);
    auto* copy = new QPushButton(tr("Copy Code And Open GitHub"), this);
    connect(copy, &QPushButton::clicked, this, [this]() {
        if (!m_code) {
            return;
        }
        QApplication::clipboard()->setText(m_code->userCode);
        QDesktopServices::openUrl(QUrl(m_code->verificationUri));
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_codeLabel);
    layout->addWidget(copy);
    layout->addWidget(m_statusLabel);
    layout->addWidget(buttons);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() { poll(); });
    QTimer::singleShot(0, this, [this]() { start(); });
}

QString SignInDialog::token() const
{
    return m_client.token();
}

void SignInDialog::start()
{
    const QPointer<SignInDialog> self(this);
    m_client.requestDeviceCode([self](std::optional<GitHubClient::DeviceCode> code,
                                      QString error) {
        if (!self) {
            return;
        }
        if (!code) {
            self->fail(error);
            return;
        }
        self->m_code = code;
        self->m_intervalSeconds = code->intervalSeconds;
        self->m_codeLabel->setText(code->userCode);
        self->m_statusLabel->setText(tr("Waiting for the code to be entered at %1…")
                                         .arg(code->verificationUri));
        QApplication::clipboard()->setText(code->userCode);
        QDesktopServices::openUrl(QUrl(code->verificationUri));
        self->m_timer->start(self->m_intervalSeconds * 1000);
    });
}

void SignInDialog::poll()
{
    const QPointer<SignInDialog> self(this);
    m_client.pollAccessToken(*m_code, [self](GitHubClient::TokenPoll result, QString error) {
        if (!self) {
            return;
        }
        switch (result) {
        case GitHubClient::TokenPoll::Pending:
            self->m_timer->start(self->m_intervalSeconds * 1000);
            return;
        case GitHubClient::TokenPoll::SlowDown:
            self->m_intervalSeconds += 5;
            self->m_timer->start(self->m_intervalSeconds * 1000);
            return;
        case GitHubClient::TokenPoll::Denied:
            self->fail(tr("Access was not allowed on GitHub."));
            return;
        case GitHubClient::TokenPoll::Expired:
            self->fail(tr("The code expired; sign in again."));
            return;
        case GitHubClient::TokenPoll::Failed:
            self->fail(error);
            return;
        case GitHubClient::TokenPoll::Granted:
            break;
        }
        self->m_statusLabel->setText(tr("Signed in; asking GitHub who you are…"));
        self->m_client.currentUser([self](QString login, QString userError) {
            if (!self) {
                return;
            }
            if (login.isEmpty()) {
                self->fail(userError);
                return;
            }
            self->m_login = login;
            self->accept();
        });
    });
}

void SignInDialog::fail(const QString& error)
{
    m_timer->stop();
    m_statusLabel->setText(tr("Signing in failed: %1").arg(error));
}

StartTeamDialog::StartTeamDialog(const QString& romName, const QString& login, QWidget* parent)
    : QDialog(parent),
      m_login(login)
{
    setWindowTitle(tr("Start Team Project"));
    setModal(true);
    m_owner = new QLineEdit(this);
    m_owner->setPlaceholderText(tr("Your account, %1").arg(login));
    m_owner->setToolTip(
        tr("A GitHub organization the repository goes to, for example the Serum team's; empty "
           "for your own account"));
    m_name = new QLineEdit(romName + QStringLiteral("-colorization"), this);
    m_name->setToolTip(tr("The repository's name; it holds the claims and the shared changes, "
                          "not the project"));
    m_partners = new QLineEdit(this);
    m_partners->setPlaceholderText(tr("GitHub logins, separated by commas"));
    auto* note = new QLabel(
        tr("The editor creates a private repository and invites the partners. Then share the "
           "saved project file with them once, for example through Google Drive: it is the "
           "snapshot everyone starts from."),
        this);
    note->setWordWrap(true);
    auto* form = new QFormLayout;
    form->addRow(tr("Organization"), m_owner);
    form->addRow(tr("Repository"), m_name);
    form->addRow(tr("Partners"), m_partners);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Start"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, [buttons](const QString& text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
    });
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    setMinimumWidth(420);
}

QString StartTeamDialog::owner() const
{
    return m_owner->text().trimmed();
}

QString StartTeamDialog::repositoryName() const
{
    return m_name->text().trimmed();
}

QStringList StartTeamDialog::partners() const
{
    QStringList logins;
    for (QString login : m_partners->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                   Qt::SkipEmptyParts)) {
        login = login.trimmed();
        if (login.startsWith(QLatin1Char('@'))) {
            login.remove(0, 1);
        }
        if (!login.isEmpty() && login.compare(m_login, Qt::CaseInsensitive) != 0 &&
            !logins.contains(login, Qt::CaseInsensitive)) {
            logins << login;
        }
    }
    return logins;
}

namespace {

QLabel* pictureLabel(const document::Rgb565Frame* frame, QWidget* parent, const QString& missing)
{
    auto* label = new QLabel(parent);
    if (!frame || !frame->isValid()) {
        label->setText(missing);
        label->setAlignment(Qt::AlignCenter);
        return label;
    }
    const int scale = frame->size.height <= 32 ? 2 : 1;
    label->setPixmap(QPixmap::fromImage(renderRgb565FrameImage(*frame, scale)));
    return label;
}

}  // namespace

MergeDialog::MergeDialog(const document::ProjectDocument& document,
                         const team::ChangePackage& package,
                         const team::MergePlan& plan,
                         const QString& projectPath,
                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Merge Changes"));
    setModal(true);
    auto* intro = new QLabel(
        tr("%1 and you both changed these. Keep one of the two for each; what you keep goes to "
           "the partners with your next shared changes.")
            .arg(QString::fromStdString(package.author)),
        this);
    intro->setWordWrap(true);
    auto* grid = new QGridLayout;
    const auto heading = [this](const QString& text) {
        auto* label = new QLabel(text, this);
        QFont bold = label->font();
        bold.setBold(true);
        label->setFont(bold);
        return label;
    };
    grid->addWidget(heading(tr("Yours")), 0, 1);
    grid->addWidget(heading(tr("%1's").arg(QString::fromStdString(package.author))), 0, 2);
    grid->addWidget(heading(tr("Before")), 0, 3);
    int row = 1;
    const document::TeamState* team = document.team() ? &*document.team() : nullptr;
    for (const team::MergeItem& item : plan.items) {
        if (item.action != team::MergeAction::Conflict &&
            item.action != team::MergeAction::DeleteConflict &&
            item.action != team::MergeAction::RestoreConflict) {
            continue;
        }
        QString name;
        switch (item.kind) {
        case document::ComponentKind::Frame:
            name = item.mine ? tr("Frame %1").arg(*item.mine) : tr("A frame you deleted");
            break;
        case document::ComponentKind::ComparisonMask:
            name = item.mine ? tr("Comparison mask %1").arg(*item.mine) : tr("A mask you deleted");
            break;
        case document::ComponentKind::Background:
            name = item.mine ? tr("Background %1").arg(*item.mine)
                             : tr("A background you deleted");
            break;
        case document::ComponentKind::Sprite:
            name = item.mine ? tr("Sprite %1").arg(QString::fromStdString(
                                   document.sprite(*item.mine).name))
                             : tr("A sprite you deleted");
            break;
        case document::ComponentKind::Palette:
            name = item.mine ? tr("Palette %1").arg(*item.mine) : tr("A palette");
            break;
        }
        QString what;
        auto* choice = new QComboBox(this);
        switch (item.action) {
        case team::MergeAction::DeleteConflict:
            what = tr("deleted by the partner, changed by you");
            choice->addItem(tr("Keep Mine"), static_cast<int>(team::MergeChoice::Mine));
            choice->addItem(tr("Delete It"), static_cast<int>(team::MergeChoice::Theirs));
            break;
        case team::MergeAction::RestoreConflict:
            what = tr("deleted by you, changed by the partner");
            choice->addItem(tr("Keep It Deleted"), static_cast<int>(team::MergeChoice::Mine));
            choice->addItem(tr("Take Theirs"), static_cast<int>(team::MergeChoice::Theirs));
            break;
        default:
            what = tr("changed by both");
            choice->addItem(tr("Keep Mine"), static_cast<int>(team::MergeChoice::Mine));
            choice->addItem(tr("Take Theirs"), static_cast<int>(team::MergeChoice::Theirs));
            break;
        }
        auto* label = new QLabel(QStringLiteral("%1<br><small>%2</small>")
                                     .arg(name.toHtmlEscaped(), what.toHtmlEscaped()),
                                 this);
        grid->addWidget(label, row, 0);
        if (item.kind == document::ComponentKind::Frame) {
            std::optional<document::Rgb565Frame> mine;
            if (item.mine) {
                mine = document.frameSnapshotOf(*item.mine).colorizedSd;
            }
            const document::Rgb565Frame* theirs =
                item.theirs ? &package.snapshot.frames[*item.theirs].colorizedSd : nullptr;
            std::optional<project::FramePictures> common;
            if (team) {
                const auto found = team->common[0].find(item.id);
                if (found != team->common[0].end()) {
                    common = project::readCommonFramePictures(projectPath.toStdString(), item.id,
                                                              found->second);
                }
            }
            grid->addWidget(pictureLabel(mine ? &*mine : nullptr, this, tr("deleted")), row, 1);
            grid->addWidget(pictureLabel(theirs, this, tr("deleted")), row, 2);
            grid->addWidget(pictureLabel(common ? &common->colorizedSd : nullptr, this,
                                         tr("not kept")),
                            row, 3);
        }
        grid->addWidget(choice, row, 4);
        m_rows.push_back({item.id, choice});
        ++row;
    }
    if (plan.settingsConflict) {
        grid->addWidget(new QLabel(tr("The settings<br><small>license, scenes, speaker lights, "
                                      "helper lines, sections</small>"),
                                   this),
                        row, 0);
        m_settings = new QComboBox(this);
        m_settings->addItem(tr("Keep Mine"), static_cast<int>(team::MergeChoice::Mine));
        m_settings->addItem(tr("Take Theirs"), static_cast<int>(team::MergeChoice::Theirs));
        grid->addWidget(m_settings, row, 4);
    }
    auto* content = new QWidget(this);
    content->setLayout(grid);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Merge"));
    QPushButton* allMine = buttons->addButton(tr("All Mine"), QDialogButtonBox::ResetRole);
    QPushButton* allTheirs = buttons->addButton(tr("All Theirs"), QDialogButtonBox::ResetRole);
    const auto setAll = [this](int index) {
        for (Row& entry : m_rows) {
            entry.choice->setCurrentIndex(index);
        }
        if (m_settings) {
            m_settings->setCurrentIndex(index);
        }
    };
    connect(allMine, &QPushButton::clicked, this, [setAll]() { setAll(0); });
    connect(allTheirs, &QPushButton::clicked, this, [setAll]() { setAll(1); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);
    resize(900, 600);
}

team::MergeChoices MergeDialog::choices() const
{
    team::MergeChoices choices;
    for (const Row& row : m_rows) {
        choices.components[row.id] =
            static_cast<team::MergeChoice>(row.choice->currentData().toInt());
    }
    if (m_settings) {
        choices.settings = static_cast<team::MergeChoice>(m_settings->currentData().toInt());
    }
    return choices;
}

}  // namespace colorizing::app
