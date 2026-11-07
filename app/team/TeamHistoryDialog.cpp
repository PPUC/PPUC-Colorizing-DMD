#include "team/TeamHistoryDialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <set>

#include "team/TeamSync.h"

namespace colorizing::app {

namespace {

using document::ComponentKind;
using What = team::HistoryChange::What;

constexpr int kFramesRole = Qt::UserRole + 1;

QString kindChangeLabel(ComponentKind kind, What what)
{
    switch (kind) {
    case ComponentKind::Frame:
        return what == What::Added     ? SharedChangesView::tr("Frames added")
               : what == What::Changed ? SharedChangesView::tr("Frames changed")
                                       : SharedChangesView::tr("Frames deleted");
    case ComponentKind::ComparisonMask:
        return what == What::Added     ? SharedChangesView::tr("Comparison masks added")
               : what == What::Changed ? SharedChangesView::tr("Comparison masks changed")
                                       : SharedChangesView::tr("Comparison masks deleted");
    case ComponentKind::Background:
        return what == What::Added     ? SharedChangesView::tr("Backgrounds added")
               : what == What::Changed ? SharedChangesView::tr("Backgrounds changed")
                                       : SharedChangesView::tr("Backgrounds deleted");
    case ComponentKind::Sprite:
        return what == What::Added     ? SharedChangesView::tr("Sprites added")
               : what == What::Changed ? SharedChangesView::tr("Sprites changed")
                                       : SharedChangesView::tr("Sprites deleted");
    case ComponentKind::Palette:
        break;
    }
    return what == What::Added     ? SharedChangesView::tr("Palettes added")
           : what == What::Changed ? SharedChangesView::tr("Palettes changed")
                                   : SharedChangesView::tr("Palettes deleted");
}

QString changeText(const team::HistoryChange& change, bool pending)
{
    QString value;
    if (change.what == What::Deleted) {
        // Gone, as a rule: how many.
        value = QString::number(change.count());
    } else {
        QStringList parts;
        if (!change.numbers.empty()) {
            parts << historyNumbersText(change.numbers);
        }
        if (change.missing > 0) {
            const int missing = static_cast<int>(change.missing);
            parts << (pending ? SharedChangesView::tr("%n new", "", missing)
                              : SharedChangesView::tr("%n no longer in the project", "",
                                                      missing));
        }
        value = parts.join(QStringLiteral(", "));
    }
    return SharedChangesView::tr("%1: %2", "a kind of change and the numbers")
        .arg(kindChangeLabel(change.kind, change.what), value);
}

QString headText(const HistoryItem& item)
{
    const QString when = QLocale().toString(QDateTime::fromMSecsSinceEpoch(item.createdMs),
                                            QLocale::ShortFormat);
    return item.pending ? SharedChangesView::tr("%1, %2: waiting to be merged")
                              .arg(item.author, when)
                        : SharedChangesView::tr("%1, %2").arg(item.author, when);
}

}  // namespace

HistoryItem pendingHistoryItem(const team::ChangePackage& package)
{
    HistoryItem item;
    item.author = memberDisplayName(QString::fromStdString(package.author));
    item.createdMs = package.createdMs;
    item.comment = QString::fromStdString(package.comment);
    item.pending = true;
    item.entry = team::historyEntryOf(package);
    return item;
}

std::vector<HistoryItem> teamHistoryItems(const document::ProjectDocument& document,
                                          const QString& cacheFolder,
                                          const std::vector<QString>& pending,
                                          const std::map<QString, PendingNote>& notes)
{
    std::vector<HistoryItem> items;
    if (!document.team()) {
        return items;
    }
    const document::TeamState& team = *document.team();
    for (auto name = pending.rbegin(); name != pending.rend(); ++name) {
        HistoryItem item;
        item.pending = true;
        if (const std::optional<PackageName> parsed = parsePackageName(*name)) {
            item.author = memberDisplayName(parsed->login);
            item.createdMs = parsed->createdMs;
        } else {
            item.author = *name;
        }
        if (const auto note = notes.find(*name); note != notes.end()) {
            item.comment = note->second.comment;
            item.missingFile = note->second.asFile && !note->second.available;
        }
        if (!cacheFolder.isEmpty()) {
            if (const std::optional<QByteArray> bytes =
                    TeamSync::cachedPackage(cacheFolder, packageFileName(*name))) {
                QString error;
                if (const std::optional<team::ChangePackage> package =
                        changePackageFrom(*bytes, error)) {
                    item = pendingHistoryItem(*package);
                }
            }
        }
        items.push_back(std::move(item));
    }

    std::vector<HistoryItem> done;
    std::set<document::ComponentId> recorded;
    for (const document::TeamHistoryEntry& entry : team.history) {
        recorded.insert(entry.packageId);
        HistoryItem item;
        item.author = memberDisplayName(QString::fromStdString(entry.author));
        item.createdMs = entry.createdMs;
        item.comment = QString::fromStdString(entry.comment);
        item.entry = entry;
        done.push_back(std::move(item));
    }
    // Merged before the history was kept: the cache has the package, as a rule.
    const std::set<document::ComponentId> merged(team.mergedPackages.begin(),
                                                 team.mergedPackages.end());
    const bool unrecorded =
        std::any_of(merged.begin(), merged.end(), [&recorded](const document::ComponentId& id) {
            return recorded.count(id) == 0;
        });
    if (!cacheFolder.isEmpty() && unrecorded) {
        const QStringList files =
            QDir(cacheFolder)
                .entryList({QStringLiteral("*.cdmdchanges")}, QDir::Files, QDir::Name);
        for (const QString& file : files) {
            const std::optional<PackageName> parsed = parsePackageName(file);
            if (!parsed || merged.count(parsed->packageId) == 0 ||
                recorded.count(parsed->packageId) > 0) {
                continue;
            }
            const std::optional<QByteArray> bytes = TeamSync::cachedPackage(cacheFolder, file);
            QString error;
            const std::optional<team::ChangePackage> package =
                bytes ? changePackageFrom(*bytes, error) : std::nullopt;
            if (!package) {
                continue;
            }
            HistoryItem item = pendingHistoryItem(*package);
            item.pending = false;
            recorded.insert(parsed->packageId);
            done.push_back(std::move(item));
        }
    }
    std::stable_sort(done.begin(), done.end(), [](const HistoryItem& a, const HistoryItem& b) {
        return a.createdMs > b.createdMs;
    });
    items.insert(items.end(), std::make_move_iterator(done.begin()),
                 std::make_move_iterator(done.end()));
    return items;
}

QString historyNumbersText(const std::vector<std::size_t>& numbers, std::size_t shown)
{
    QStringList parts;
    std::size_t listed = 0;
    std::size_t i = 0;
    while (i < numbers.size() && listed < shown) {
        std::size_t last = i;
        while (last + 1 < numbers.size() && numbers[last + 1] == numbers[last] + 1) {
            ++last;
        }
        if (last > i) {
            parts << QStringLiteral("%1-%2").arg(numbers[i]).arg(numbers[last]);
        } else {
            parts << QString::number(numbers[i]);
        }
        listed += last - i + 1;
        i = last + 1;
    }
    QString text = parts.join(QStringLiteral(", "));
    if (i < numbers.size()) {
        text = SharedChangesView::tr("%1 and %n more", "", static_cast<int>(numbers.size() - i))
                   .arg(text);
    }
    return text;
}

SharedChangesView::SharedChangesView(QWidget* parent)
    : QTreeWidget(parent)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setWordWrap(true);
    setUniformRowHeights(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    header()->setStretchLastSection(true);
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const std::vector<std::size_t> frames = framesOf(item);
        if (!frames.empty() && onShowFrames) {
            onShowFrames(frames);
        }
    });
}

void SharedChangesView::setItems(const std::vector<HistoryItem>& items,
                                 const document::ProjectDocument& document)
{
    clear();
    for (const HistoryItem& item : items) {
        auto* head = new QTreeWidgetItem(this, {headText(item)});
        QFont bold = head->font(0);
        bold.setBold(true);
        head->setFont(0, bold);
        auto* comment = new QTreeWidgetItem(
            head, {item.comment.isEmpty() ? tr("No comment") : item.comment});
        QFont italic = comment->font(0);
        italic.setItalic(item.comment.isEmpty());
        comment->setFont(0, italic);
        if (!item.entry) {
            new QTreeWidgetItem(
                head, {item.missingFile
                           ? tr("Shared as a file: what it changes shows once you add the file "
                                "in the Team section")
                           : tr("Not downloaded yet: what it changes shows once it is")});
            head->setExpanded(true);
            continue;
        }
        for (const team::HistoryChange& change : team::describeHistoryEntry(*item.entry,
                                                                            document)) {
            auto* line = new QTreeWidgetItem(head, {changeText(change, item.pending)});
            if (change.kind == ComponentKind::Frame && change.what != What::Deleted &&
                !change.numbers.empty()) {
                QVariantList frames;
                for (const std::size_t frame : change.numbers) {
                    frames << QVariant::fromValue(static_cast<qulonglong>(frame));
                }
                line->setData(0, kFramesRole, frames);
                line->setToolTip(0, tr("Double-click to select these frames"));
            }
        }
        if (item.entry->orderChanged) {
            new QTreeWidgetItem(head, {tr("The frame order changed")});
        }
        if (item.entry->settingsChanged) {
            new QTreeWidgetItem(
                head, {tr("The settings changed (license, scenes, lights, helper lines, "
                          "sections)")});
        }
        head->setExpanded(true);
    }
}

QStringList SharedChangesView::texts() const
{
    QStringList lines;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        const QTreeWidgetItem* head = topLevelItem(i);
        lines << head->text(0);
        for (int c = 0; c < head->childCount(); ++c) {
            lines << QStringLiteral("  ") + head->child(c)->text(0);
        }
    }
    return lines;
}

std::vector<std::size_t> SharedChangesView::framesOf(const QTreeWidgetItem* item) const
{
    std::vector<std::size_t> frames;
    if (!item) {
        return frames;
    }
    for (const QVariant& frame : item->data(0, kFramesRole).toList()) {
        frames.push_back(static_cast<std::size_t>(frame.toULongLong()));
    }
    return frames;
}

TeamHistoryDialog::TeamHistoryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Team History"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("The changes shared in this team project, newest first: who shared them, when, "
           "their comment and what they changed. Double-click a line of frames to select "
           "them."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_emptyLabel = new QLabel(tr("No changes shared yet."), this);
    layout->addWidget(m_emptyLabel);
    m_view = new SharedChangesView(this);
    layout->addWidget(m_view, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(620, 520);
}

void TeamHistoryDialog::setItems(const std::vector<HistoryItem>& items,
                                 const document::ProjectDocument& document)
{
    m_view->setItems(items, document);
    m_emptyLabel->setVisible(items.empty());
}

MergePreviewDialog::MergePreviewDialog(const std::vector<HistoryItem>& items,
                                       const document::ProjectDocument& document,
                                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Merge Changes"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("%n shared change(s) from the partners, merged in this order. Read what they "
           "change before you merge them.",
           "", static_cast<int>(items.size())),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_view = new SharedChangesView(this);
    m_view->setItems(items, document);
    layout->addWidget(m_view, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* merge = buttons->addButton(tr("Merge"), QDialogButtonBox::AcceptRole);
    merge->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(620, 480);
}

}  // namespace colorizing::app
