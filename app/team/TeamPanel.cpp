#include "team/TeamPanel.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include "team/TeamSession.h"

namespace colorizing::app {

TeamPanel::TeamPanel(QWidget* parent)
    : QWidget(parent)
{
    m_accountLabel = new QLabel(this);
    m_accountLabel->setWordWrap(true);
    m_accountLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_signInButton = new QPushButton(tr("Sign In To GitHub..."), this);
    m_problemLabel = new QLabel(this);
    m_problemLabel->setWordWrap(true);
    m_problemLabel->setVisible(false);
    m_pendingLabel = new QLabel(this);
    m_pendingLabel->setWordWrap(true);
    m_pendingList = new QListWidget(this);
    m_pendingList->setToolTip(tr("The partners' shared changes, oldest first; a merge takes "
                                 "them all in this order"));
    m_pendingList->setSelectionMode(QAbstractItemView::NoSelection);
    m_pendingList->setWordWrap(true);
    m_pendingList->setIconSize(QSize(28, 28));
    m_checkedLabel = new QLabel(this);
    m_checkedLabel->setWordWrap(true);
    m_mergeButton = new QPushButton(tr("Merge Changes..."), this);
    m_mergeButton->setToolTip(tr("Take the changes the partners shared into this project"));
    m_shareButton = new QPushButton(tr("Share My Changes"), this);
    m_shareButton->setToolTip(
        tr("Save, then send what you changed since the last merge to the partners"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_mergeButton);
    buttons->addWidget(m_shareButton);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_accountLabel);
    layout->addWidget(m_signInButton);
    layout->addWidget(m_problemLabel);
    layout->addWidget(m_pendingLabel);
    layout->addWidget(m_pendingList, 1);
    layout->addWidget(m_checkedLabel);
    layout->addLayout(buttons);

    connect(m_signInButton, &QPushButton::clicked, this, [this]() {
        if (onSignInRequested) {
            onSignInRequested();
        }
    });
    connect(m_mergeButton, &QPushButton::clicked, this, [this]() {
        if (onMergeRequested) {
            onMergeRequested();
        }
    });
    connect(m_pendingList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        if (onMergeRequested) {
            onMergeRequested();
        }
    });
    connect(m_shareButton, &QPushButton::clicked, this, [this]() {
        if (onShareRequested) {
            onShareRequested();
        }
    });
    setState({});
}

void TeamPanel::setState(const State& state)
{
    const bool signedIn = !state.login.isEmpty();
    m_accountLabel->setText(signedIn ? tr("Repository %1, signed in as %2")
                                           .arg(state.repository, state.login)
                                     : tr("Repository %1; sign in to GitHub to see the claims "
                                          "and the shared changes")
                                           .arg(state.repository));
    m_signInButton->setVisible(!signedIn);
    m_problemLabel->setText(state.problem);
    m_problemLabel->setVisible(!state.problem.isEmpty());
    m_pendingList->clear();
    const QLocale locale;
    for (const QString& name : state.pendingPackages) {
        const std::optional<PackageName> parsed = parsePackageName(name);
        QString text =
            parsed ? tr("From %1, %2")
                         .arg(parsed->login,
                              locale.toString(QDateTime::fromMSecsSinceEpoch(parsed->createdMs),
                                              QLocale::ShortFormat))
                   : name;
        const auto comment = state.comments.find(name);
        if (comment != state.comments.end()) {
            text += QLatin1Char('\n') + comment->second;
        }
        auto* item = new QListWidgetItem(text, m_pendingList);
        item->setToolTip(comment != state.comments.end() ? comment->second : name);
        if (parsed) {
            const auto avatar = state.avatars.find(parsed->login);
            if (avatar != state.avatars.end()) {
                item->setIcon(QIcon(QPixmap::fromImage(avatar->second)));
            }
        }
    }
    const auto count = static_cast<int>(state.pendingPackages.size());
    m_pendingLabel->setText(!signedIn   ? QString()
                            : count > 0 ? tr("%n shared change(s) to merge:", "", count)
                                        : tr("No shared changes to merge."));
    m_pendingLabel->setVisible(signedIn);
    m_pendingList->setVisible(count > 0);
    m_checkedLabel->setText(
        signedIn && state.checked
            ? tr("Checked at %1").arg(state.checked->time().toString(Qt::ISODate))
        : signedIn ? tr("Checking…")
                   : QString());
    m_checkedLabel->setVisible(signedIn);
    m_mergeButton->setText(count > 0 ? tr("Merge Changes (%1)...").arg(count)
                                     : tr("Merge Changes..."));
    m_mergeButton->setEnabled(signedIn && count > 0 && state.problem.isEmpty());
    m_shareButton->setEnabled(signedIn && state.problem.isEmpty());
}

QStringList TeamPanel::pendingTexts() const
{
    QStringList texts;
    for (int row = 0; row < m_pendingList->count(); ++row) {
        texts.append(m_pendingList->item(row)->text());
    }
    return texts;
}

QString TeamPanel::accountText() const
{
    return m_accountLabel->text();
}

bool TeamPanel::hasAvatar(int row) const
{
    const QListWidgetItem* item = m_pendingList->item(row);
    return item && !item->icon().isNull();
}

QString TeamPanel::checkedText() const
{
    return m_checkedLabel->text();
}

}  // namespace colorizing::app
