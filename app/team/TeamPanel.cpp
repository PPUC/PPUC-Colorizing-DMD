#include "team/TeamPanel.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QMimeData>
#include <QUrl>
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
    m_addFileButton = new QPushButton(tr("Add File..."), this);
    m_addFileButton->setToolTip(
        tr("Add a shared change a partner sent as a file (too large for GitHub); dropping the "
           "file here does the same"));
    auto* buttons = new QGridLayout;
    buttons->addWidget(m_mergeButton, 0, 0);
    buttons->addWidget(m_shareButton, 0, 1);
    buttons->addWidget(m_addFileButton, 1, 0, 1, 2);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_accountLabel);
    layout->addWidget(m_signInButton);
    layout->addWidget(m_problemLabel);
    layout->addWidget(m_pendingLabel);
    layout->addWidget(m_pendingList, 1);
    layout->addWidget(m_checkedLabel);
    layout->addLayout(buttons);
    m_timesLabel = new QLabel(this);
    m_timesLabel->setWordWrap(true);
    m_timesLabel->setToolTip(tr("The time each of you spent on the project, as far as your "
                                "shared changes have brought it here"));
    layout->addWidget(m_timesLabel);

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
    connect(m_addFileButton, &QPushButton::clicked, this, [this]() {
        if (onAddFileRequested) {
            onAddFileRequested(QString());
        }
    });
    setAcceptDrops(true);
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
        const auto found = state.notes.find(name);
        const PendingNote note = found != state.notes.end() ? found->second : PendingNote{};
        if (isPackageNotice(name) && found == state.notes.end()) {
            text += QLatin1Char('\n') + tr("Comes as a file");
        } else if (note.asFile && !note.available) {
            text += QLatin1Char('\n') +
                    tr("%1, comes as a file: add it with Add File")
                        .arg(locale.formattedDataSize(note.sizeBytes, 0,
                                                      QLocale::DataSizeTraditionalFormat));
        } else if (note.asFile) {
            text += QLatin1Char('\n') +
                    tr("%1, added as a file")
                        .arg(locale.formattedDataSize(note.sizeBytes, 0,
                                                      QLocale::DataSizeTraditionalFormat));
        }
        if (!note.comment.isEmpty()) {
            text += QLatin1Char('\n') + note.comment;
        }
        auto* item = new QListWidgetItem(text, m_pendingList);
        item->setToolTip(note.comment.isEmpty() ? packageFileName(name) : note.comment);
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
    m_addFileButton->setEnabled(signedIn && state.problem.isEmpty());
    m_signedIn = signedIn && state.problem.isEmpty();
}

void TeamPanel::dragEnterEvent(QDragEnterEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (m_signedIn && urls.size() == 1 && urls[0].isLocalFile() &&
        urls[0].toLocalFile().endsWith(QStringLiteral(".cdmdchanges"), Qt::CaseInsensitive)) {
        event->acceptProposedAction();
    }
}

void TeamPanel::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() == 1 && onAddFileRequested) {
        event->acceptProposedAction();
        onAddFileRequested(urls[0].toLocalFile());
    }
}

void TeamPanel::setAuthorTimes(const QStringList& authorTimes)
{
    const QString text = authorTimes.isEmpty()
                             ? QString()
                             : tr("Time spent: %1").arg(authorTimes.join(QStringLiteral(", ")));
    if (m_timesLabel->text() != text) {
        m_timesLabel->setText(text);
    }
    m_timesLabel->setVisible(!text.isEmpty());
}

QString TeamPanel::authorTimesText() const
{
    return m_timesLabel->text();
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
