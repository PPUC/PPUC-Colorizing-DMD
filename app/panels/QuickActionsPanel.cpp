#include "panels/QuickActionsPanel.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>

namespace colorizing::app {

namespace {

const QString kItemsKey = QStringLiteral("quickActions/items");

struct DirectionLabel {
    const char* text;
    const char* comment;
};

struct DirectionCell {
    const char* direction;
    DirectionLabel label;
    int row;
    int column;
};

// The square of a group: the eight directions around Stern, as on the Dynamic Sets section.
constexpr std::array<DirectionCell, 9> kDirectionCells{{
    {"top-left", QT_TRANSLATE_NOOP3("QuickActionsPanel", "TL", "direction top left"), 0, 0},
    {"top", QT_TRANSLATE_NOOP3("QuickActionsPanel", "T", "direction top"), 0, 1},
    {"top-right", QT_TRANSLATE_NOOP3("QuickActionsPanel", "TR", "direction top right"), 0, 2},
    {"left", QT_TRANSLATE_NOOP3("QuickActionsPanel", "L", "direction left"), 1, 0},
    {"all-around-stern-style",
     QT_TRANSLATE_NOOP3("QuickActionsPanel", "Stern", "the four sides, as Stern draws them"),
     1, 1},
    {"right", QT_TRANSLATE_NOOP3("QuickActionsPanel", "R", "direction right"), 1, 2},
    {"bottom-left",
     QT_TRANSLATE_NOOP3("QuickActionsPanel", "BL", "direction bottom left"), 2, 0},
    {"bottom", QT_TRANSLATE_NOOP3("QuickActionsPanel", "B", "direction bottom"), 2, 1},
    {"bottom-right",
     QT_TRANSLATE_NOOP3("QuickActionsPanel", "BR", "direction bottom right"), 2, 2},
}};

// The menu entry's text without its mnemonic.
QString plainText(const QAction* action)
{
    QString text = action->text();
    text.remove(QStringLiteral("&&")).remove(QLatin1Char('&'));
    return text;
}

QString groupPrefix(const QString& group)
{
    return group == QLatin1String(QuickActionsPanel::kShadowGroup) ? QStringLiteral("shadow-")
                                                                   : QStringLiteral("grow-");
}

}  // namespace

QuickActionsPanel::QuickActionsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    m_itemsWidget = new QWidget(this);
    m_itemsLayout = new QVBoxLayout(m_itemsWidget);
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_itemsWidget);
    m_chooseButton = new QToolButton(this);
    m_chooseButton->setText(tr("Choose..."));
    m_chooseButton->setToolTip(tr("Choose the menu entries shown here"));
    m_chooseButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_chooseButton);
    layout->addStretch(1);
    connect(m_chooseButton, &QToolButton::clicked, this, [this]() { choose(); });
}

void QuickActionsPanel::setItems(const QStringList& items)
{
    m_items = items;
    rebuild();
}

const QStringList& QuickActionsPanel::items() const
{
    return m_items;
}

std::vector<std::pair<QString, QToolButton*>> QuickActionsPanel::buttons() const
{
    return m_buttons;
}

QStringList QuickActionsPanel::defaultItems()
{
    return {QString::fromLatin1(kShadowGroup), QStringLiteral("assign-dynamic-set"),
            QStringLiteral("remove-dynamic-zones"), QStringLiteral("select-none"),
            QStringLiteral("invert-selection")};
}

QStringList QuickActionsPanel::loadItems()
{
    const QSettings settings;
    return settings.contains(kItemsKey) ? settings.value(kItemsKey).toStringList()
                                        : defaultItems();
}

void QuickActionsPanel::saveItems(const QStringList& items)
{
    QSettings().setValue(kItemsKey, items);
}

std::vector<std::pair<QString, QString>> QuickActionsPanel::choices(
    const std::vector<ShortcutEntry>& entries)
{
    std::vector<std::pair<QString, QString>> offered{
        {QString::fromLatin1(kShadowGroup),
         tr("Edit -> Shadow Next To Selection (all directions)")},
        {QString::fromLatin1(kGrowGroup), tr("Edit -> Grow Selection (all directions)")},
    };
    for (const ShortcutEntry& entry : entries) {
        if (entry.id.startsWith(QStringLiteral("shadow-")) ||
            entry.id.startsWith(QStringLiteral("grow-"))) {
            continue;
        }
        offered.emplace_back(entry.id, entry.title);
    }
    return offered;
}

QToolButton* QuickActionsPanel::actionButton(const QString& id,
                                             const QString& label,
                                             QWidget* parent)
{
    QAction* action = actionFor ? actionFor(id) : nullptr;
    if (!action) {
        return nullptr;
    }
    auto* button = new QToolButton(parent);
    button->setText(label.isEmpty() ? plainText(action) : label);
    const auto update = [button, action]() {
        button->setEnabled(action->isEnabled());
        QString toolTip = plainText(action);
        if (!action->toolTip().isEmpty() && action->toolTip() != toolTip) {
            toolTip += QStringLiteral(": ") + action->toolTip();
        }
        const QString keys = shortcutsText(action->shortcuts());
        if (!keys.isEmpty()) {
            toolTip += QStringLiteral(" (%1)").arg(keys);
        }
        button->setToolTip(toolTip);
    };
    update();
    connect(action, &QAction::changed, button, update);
    connect(button, &QToolButton::clicked, action, [action]() { action->trigger(); });
    m_buttons.emplace_back(id, button);
    return button;
}

QWidget* QuickActionsPanel::groupWidget(const QString& group)
{
    auto* widget = new QWidget(m_itemsWidget);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(group == QLatin1String(kShadowGroup) ? tr("Shadow Next To Selection")
                                                                   : tr("Grow Selection"),
                             widget);
    layout->addWidget(title);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);
    const QString prefix = groupPrefix(group);
    for (const DirectionCell& cell : kDirectionCells) {
        QToolButton* button = actionButton(prefix + QString::fromLatin1(cell.direction),
                                           tr(cell.label.text, cell.label.comment), widget);
        if (!button) {
            continue;
        }
        if (QString::fromLatin1(cell.direction) != QLatin1String("all-around-stern-style")) {
            button->setFixedSize(32, 24);
        }
        // Centred: the square keeps its size however wide the section is.
        grid->addWidget(button, cell.row, cell.column, Qt::AlignCenter);
    }
    if (QToolButton* all = actionButton(prefix + QStringLiteral("all-around"), tr("All Around"),
                                        widget)) {
        grid->addWidget(all, 3, 0, 1, 3, Qt::AlignCenter);
    }
    // A compact square in the middle of the section, however wide it is.
    auto* centred = new QHBoxLayout();
    centred->addStretch(1);
    centred->addLayout(grid);
    centred->addStretch(1);
    layout->addLayout(centred);
    return widget;
}

void QuickActionsPanel::rebuild()
{
    m_buttons.clear();
    while (QLayoutItem* item = m_itemsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Out of sight at once; deleted once the event loop runs.
            widget->hide();
            widget->deleteLater();
        } else if (QLayout* layout = item->layout()) {
            while (QLayoutItem* inner = layout->takeAt(0)) {
                delete inner->widget();
                delete inner;
            }
        }
        delete item;
    }
    // The single entries two to a row, the groups each on their own, in the author's order.
    QGridLayout* row = nullptr;
    int column = 0;
    for (const QString& item : m_items) {
        if (item == QLatin1String(kShadowGroup) || item == QLatin1String(kGrowGroup)) {
            m_itemsLayout->addWidget(groupWidget(item));
            row = nullptr;
            continue;
        }
        if (!row) {
            row = new QGridLayout();
            row->setHorizontalSpacing(4);
            row->setVerticalSpacing(4);
            m_itemsLayout->addLayout(row);
            column = 0;
        }
        QToolButton* button = actionButton(item, QString(), m_itemsWidget);
        if (!button) {
            continue;
        }
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(button, column / 2, column % 2);
        ++column;
    }
    if (m_buttons.empty()) {
        auto* note = new QLabel(tr("Choose the menu entries you use most to have them here."),
                                m_itemsWidget);
        note->setWordWrap(true);
        m_itemsLayout->addWidget(note);
    }
}

void QuickActionsPanel::choose()
{
    const std::vector<std::pair<QString, QString>> offered =
        choices(entries ? entries() : std::vector<ShortcutEntry>{});
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Quick Actions"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(tr("The menu entries shown in the Quick Actions section, in this "
                               "order:"),
                            &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* list = new QListWidget(&dialog);
    for (const auto& [id, label] : offered) {
        auto* item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_items.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
        &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog,
            [list]() {
                const QStringList defaults = defaultItems();
                for (int row = 0; row < list->count(); ++row) {
                    QListWidgetItem* item = list->item(row);
                    item->setCheckState(defaults.contains(item->data(Qt::UserRole).toString())
                                            ? Qt::Checked
                                            : Qt::Unchecked);
                }
            });
    dialog.resize(520, 560);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QStringList chosen;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->checkState() == Qt::Checked) {
            chosen << list->item(row)->data(Qt::UserRole).toString();
        }
    }
    saveItems(chosen);
    setItems(chosen);
}

}  // namespace colorizing::app
