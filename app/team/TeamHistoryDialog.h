#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/TeamState.h"
#include "team/TeamChanges.h"
#include "team/TeamHistory.h"
#include "team/TeamSession.h"

class QLabel;

namespace colorizing::app {

// One shared change as Team -> History and the merge show it.
struct HistoryItem {
    QString author;
    int64_t createdMs = 0;
    QString comment;
    // Shared, but not merged into this copy yet.
    bool pending = false;
    // A pending change shared as a file this copy does not have yet.
    bool missingFile = false;
    // What it changed; nothing when this copy does not know (a pending change not downloaded
    // yet).
    std::optional<document::TeamHistoryEntry> entry;
};

// The item of a package waiting to be merged.
[[nodiscard]] HistoryItem pendingHistoryItem(const team::ChangePackage& package);

// The team's shared changes, newest first: those waiting to be merged (pending, with their
// notes, as the background sync found them; what they change when they are in the cache),
// then those this copy shared and merged (TeamState::history). A merged package the history
// has no entry of (merged by an older editor) is taken from the cache when it is there.
[[nodiscard]] std::vector<HistoryItem> teamHistoryItems(
    const document::ProjectDocument& document,
    const QString& cacheFolder,
    const std::vector<QString>& pending,
    const std::map<QString, PendingNote>& notes);

// "2360, 2362-2365": numbers, runs shortened; at most `shown` of them, then how many more.
[[nodiscard]] QString historyNumbersText(const std::vector<std::size_t>& numbers,
                                         std::size_t shown = 40);

// The shared changes as a tree: per change who shared it and when, the comment and what it
// changed. Double-clicking a line of frames selects them (onShowFrames).
class SharedChangesView final : public QTreeWidget {
    Q_DECLARE_TR_FUNCTIONS(SharedChangesView)

public:
    explicit SharedChangesView(QWidget* parent = nullptr);

    // The numbers are those of the document.
    void setItems(const std::vector<HistoryItem>& items,
                  const document::ProjectDocument& document);

    // For tests: the lines shown, a change's lines indented by two spaces.
    [[nodiscard]] QStringList texts() const;
    // The frames a line selects; empty for other lines.
    [[nodiscard]] std::vector<std::size_t> framesOf(const QTreeWidgetItem* item) const;

    std::function<void(const std::vector<std::size_t>& frames)> onShowFrames;
};

// Team -> History: the team's shared changes, newest first. Not modal, so that the author
// can go to the frames a change made while it is open.
class TeamHistoryDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(TeamHistoryDialog)

public:
    explicit TeamHistoryDialog(QWidget* parent = nullptr);

    void setItems(const std::vector<HistoryItem>& items,
                  const document::ProjectDocument& document);
    [[nodiscard]] SharedChangesView* view() const { return m_view; }

private:
    QLabel* m_emptyLabel = nullptr;
    SharedChangesView* m_view = nullptr;
};

// Team -> Merge Changes: the partners' changes about to be merged, oldest first, with their
// comments and what they change; Merge or Cancel.
class MergePreviewDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(MergePreviewDialog)

public:
    MergePreviewDialog(const std::vector<HistoryItem>& items,
                       const document::ProjectDocument& document,
                       QWidget* parent = nullptr);
    [[nodiscard]] SharedChangesView* view() const { return m_view; }

private:
    SharedChangesView* m_view = nullptr;
};

}  // namespace colorizing::app
