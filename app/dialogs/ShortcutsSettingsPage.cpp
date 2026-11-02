#include "dialogs/ShortcutsSettingsPage.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>
#include <optional>
#include <utility>

namespace colorizing::app {

namespace {

// Takes what is pressed as a key for a shortcut, instead of typing it or acting on it.
class KeyField final : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

    std::function<void(const QKeySequence&)> onKey;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::KeyPress && take(*static_cast<QKeyEvent*>(event))) {
            return true;
        }
        return QLineEdit::event(event);
    }

private:
    bool take(const QKeyEvent& event)
    {
        const int key = event.key();
        const Qt::KeyboardModifiers command =
            event.modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        // Tab goes on to the next control; the modifiers alone are no key yet.
        if (((key == Qt::Key_Tab || key == Qt::Key_Backtab) && command == Qt::NoModifier) ||
            key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
            key == Qt::Key_AltGr || key == Qt::Key_Meta || key == Qt::Key_CapsLock ||
            key == Qt::Key_NumLock || key == Qt::Key_ScrollLock || key == Qt::Key_unknown) {
            return false;
        }
        Qt::KeyboardModifiers modifiers = command;
        // Shift counts unless it is what types the symbol: Ctrl+Shift+1 arrives as Ctrl+Shift+!
        // on one keyboard, and "!" is the key then.
        const QString text = event.text();
        if (event.modifiers().testFlag(Qt::ShiftModifier) &&
            (text.isEmpty() || !text.at(0).isPrint() || text.at(0).isLetterOrNumber() ||
             text.at(0).isSpace())) {
            modifiers |= Qt::ShiftModifier;
        }
        if (onKey) {
            onKey(QKeySequence(QKeyCombination(modifiers, static_cast<Qt::Key>(key))));
        }
        return true;
    }
};

}  // namespace

ShortcutsSettingsPage::ShortcutsSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search the entries and keys"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, [this]() { applyFilter(); });

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({tr("Menu Entry"), tr("Keys")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this]() {
        m_note->clear();
        updateControls();
    });

    auto* keyField = new KeyField(this);
    keyField->setObjectName(QStringLiteral("shortcutKeyField"));
    keyField->setReadOnly(true);
    keyField->setPlaceholderText(tr("Click here and press the keys"));
    keyField->onKey = [this](const QKeySequence& key) { keyPressed(key); };
    m_keyField = keyField;
    m_noneButton = new QToolButton(this);
    m_noneButton->setText(tr("None"));
    m_noneButton->setToolTip(tr("The entry goes without a key"));
    connect(m_noneButton, &QToolButton::clicked, this, [this]() { setCurrentKeys({}); });
    m_defaultButton = new QToolButton(this);
    m_defaultButton->setText(tr("Default"));
    m_defaultButton->setToolTip(tr("The key the entry comes with"));
    connect(m_defaultButton, &QToolButton::clicked, this, [this]() {
        const int entry = selectedEntry();
        if (entry < 0) {
            return;
        }
        // Only what is still free of the entry's own keys.
        QList<QKeySequence> keys;
        QStringList taken;
        const auto index = static_cast<std::size_t>(entry);
        for (const QKeySequence& key : m_entries[index].defaults) {
            if (const std::optional<std::size_t> other = entryWithKey(m_entries, key, index)) {
                taken << m_entries[*other].title;
            } else {
                keys << key;
            }
        }
        setCurrentKeys(keys);
        if (!taken.isEmpty()) {
            m_note->setText(
                tr("Its default key stays with %1.").arg(taken.join(QStringLiteral(", "))));
        }
    });
    auto* restoreAll = new QToolButton(this);
    restoreAll->setText(tr("Restore All Defaults"));
    connect(restoreAll, &QToolButton::clicked, this, [this]() {
        for (ShortcutEntry& entry : m_entries) {
            entry.current = entry.defaults;
        }
        for (std::size_t index = 0; index < m_entries.size(); ++index) {
            showRow(index);
        }
        m_note->clear();
        updateControls();
    });
    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(tr("Keys"), this));
    keyRow->addWidget(m_keyField, 1);
    keyRow->addWidget(m_noneButton);
    keyRow->addWidget(m_defaultButton);
    keyRow->addStretch(0);
    keyRow->addWidget(restoreAll);

    m_note = new QLabel(this);
    m_note->setObjectName(QStringLiteral("shortcutNote"));
    m_note->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_list, 1);
    layout->addLayout(keyRow);
    layout->addWidget(m_note);
    updateControls();
}

void ShortcutsSettingsPage::setEntries(std::vector<ShortcutEntry> entries)
{
    m_entries = std::move(entries);
    m_list->clear();
    for (std::size_t index = 0; index < m_entries.size(); ++index) {
        new QTreeWidgetItem(m_list);
        showRow(index);
    }
    applyFilter();
    updateControls();
}

void ShortcutsSettingsPage::showRow(std::size_t index)
{
    QTreeWidgetItem* item = m_list->topLevelItem(static_cast<int>(index));
    item->setText(0, m_entries[index].title);
    item->setText(1, shortcutsText(m_entries[index].current));
}

int ShortcutsSettingsPage::selectedEntry() const
{
    const QList<QTreeWidgetItem*> selected = m_list->selectedItems();
    return selected.isEmpty() ? -1 : m_list->indexOfTopLevelItem(selected.front());
}

void ShortcutsSettingsPage::keyPressed(const QKeySequence& key)
{
    const int entry = selectedEntry();
    if (entry < 0) {
        m_note->setText(tr("Select an entry in the list first."));
        return;
    }
    const auto index = static_cast<std::size_t>(entry);
    if (const std::optional<QString> problem = shortcutKeyProblem(key, m_entries[index].defaults)) {
        m_note->setText(*problem);
        return;
    }
    if (const std::optional<std::size_t> other = entryWithKey(m_entries, key, index)) {
        //: %1: a key, %2: a menu entry ("Edit -> Copy")
        m_note->setText(tr("%1 is already used by %2. Change that entry first.")
                            .arg(key.toString(QKeySequence::NativeText), m_entries[*other].title));
        return;
    }
    setCurrentKeys({key});
}

void ShortcutsSettingsPage::setCurrentKeys(const QList<QKeySequence>& keys)
{
    const int entry = selectedEntry();
    if (entry < 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(entry);
    m_entries[index].current = keys;
    showRow(index);
    m_note->clear();
    updateControls();
    applyFilter();
}

void ShortcutsSettingsPage::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    for (std::size_t index = 0; index < m_entries.size(); ++index) {
        QTreeWidgetItem* item = m_list->topLevelItem(static_cast<int>(index));
        item->setHidden(!text.isEmpty() && !item->text(0).contains(text, Qt::CaseInsensitive) &&
                        !item->text(1).contains(text, Qt::CaseInsensitive));
    }
}

void ShortcutsSettingsPage::updateControls()
{
    const int entry = selectedEntry();
    const bool selected = entry >= 0;
    m_keyField->setEnabled(selected);
    m_keyField->setText(selected
                            ? shortcutsText(m_entries[static_cast<std::size_t>(entry)].current)
                            : QString());
    if (!selected) {
        m_noneButton->setEnabled(false);
        m_defaultButton->setEnabled(false);
        return;
    }
    const ShortcutEntry& shown = m_entries[static_cast<std::size_t>(entry)];
    m_noneButton->setEnabled(!shown.current.isEmpty());
    m_defaultButton->setEnabled(shown.current != shown.defaults);
}

}  // namespace colorizing::app
