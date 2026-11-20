#include "EditorPlugins.h"

#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

#include <algorithm>

#include "Appearance.h"
#include "Translations.h"
#include "document/ProjectDocument.h"
#include "edit/EditBatch.h"
#include "edit/EditCommand.h"
#include "vpx/VpxHost.h"

namespace colorizing::app {

namespace {

const QString kEnabledKey = QStringLiteral("plugins/enabled");
// Loaded by the editor itself when it needs them, not chosen here: DMDUtil by the DMD output,
// PUP by the PuP tester.
const QString kDmdUtilId = QStringLiteral("DMDUtil");
const QString kPupId = QStringLiteral("PUP");

QString settingKey(const std::string& pluginId, const std::string& key)
{
    return QStringLiteral("plugins/%1/%2")
        .arg(QString::fromStdString(pluginId), QString::fromStdString(key));
}

}  // namespace

EditorPlugins::EditorPlugins(Window window)
    : m_window(std::move(window))
{
    vpx::setEditorBackend(this);
    // Plugins hand work to the UI thread through the bus, and learn about changes from here.
    m_timer.setInterval(20);
    QObject::connect(&m_timer, &QTimer::timeout, [this]() { tick(); });
    updateMenu();
}

EditorPlugins::~EditorPlugins()
{
    m_timer.stop();
    for (const QString& id : QStringList(m_loaded)) {
        unload(id);
    }
    while (!m_reviews.empty()) {
        closeWindows(m_reviews.begin()->first);
    }
    while (!m_settings.empty()) {
        closeWindows(m_settings.begin()->first);
    }
    vpx::setEditorBackend(nullptr);
}

std::vector<EditorPlugins::Plugin> EditorPlugins::available()
{
    std::vector<Plugin> plugins;
    if (!vpx::Host::isBuiltIn()) {
        return plugins;
    }
    for (const vpx::Host::PluginDescription& plugin : vpx::Host::instance().plugins()) {
        const QString id = QString::fromStdString(plugin.id);
        Plugin entry;
        entry.id = id;
        entry.name = QString::fromStdString(plugin.name);
        entry.description = QString::fromStdString(plugin.description);
        entry.version = QString::fromStdString(plugin.version);
        if (id == kDmdUtilId) {
            entry.builtIn = true;
            entry.builtInUse = tr("DMD output");
        } else if (id == kPupId) {
            entry.builtIn = true;
            entry.builtInUse = tr("PuP tester");
        }
        plugins.push_back(std::move(entry));
    }
    return plugins;
}

QStringList EditorPlugins::enabledInSettings()
{
    return QSettings().value(kEnabledKey).toStringList();
}

QStringList EditorPlugins::loadEnabled()
{
    QStringList errors;
    for (const QString& id : enabledInSettings()) {
        if (!m_loaded.contains(id)) {
            load(id, errors);
        }
    }
    return errors;
}

QStringList EditorPlugins::setEnabled(const QStringList& ids)
{
    QSettings().setValue(kEnabledKey, ids);
    for (const QString& id : QStringList(m_loaded)) {
        if (!ids.contains(id)) {
            unload(id);
        }
    }
    QStringList errors;
    for (const QString& id : ids) {
        if (!m_loaded.contains(id)) {
            load(id, errors);
        }
    }
    return errors;
}

QStringList EditorPlugins::loaded() const
{
    return m_loaded;
}

bool EditorPlugins::load(const QString& id, QStringList& errors)
{
    if (id == kDmdUtilId || id == kPupId) {
        return false;
    }
    std::string error;
    if (!vpx::Host::instance().load(id.toStdString(), {}, &error)) {
        errors << tr("Plugin %1: %2").arg(id, QString::fromStdString(error));
        return false;
    }
    m_loaded << id;
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    return true;
}

void EditorPlugins::unload(const QString& id)
{
    vpx::Host::instance().unload(id.toStdString());
    removeMenuEntries(id.toStdString());
    closeWindows(id.toStdString());
    m_loaded.removeAll(id);
    if (m_loaded.isEmpty()) {
        m_timer.stop();
    }
}

void EditorPlugins::tick()
{
    vpx::Host::instance().process();
    vpx::finishOpenEditBatch();
    const document::ProjectDocument* current = document();
    for (auto& [pluginId, review] : m_reviews) {
        if (review.dialog && !review.dialog->isOutdated() &&
            (current != review.document || current->revision() != review.revision)) {
            review.dialog->setOutdated(tr("The project changed since the plugin made its "
                                          "proposal; it can no longer be applied."));
        }
    }
    const uint64_t revision = current ? current->revision() : 0;
    if (current != m_toldDocument || revision != m_toldRevision) {
        m_toldDocument = current;
        m_toldRevision = revision;
        vpx::notifyProjectChanged();
    }
    const std::optional<std::size_t> frame = currentFrame();
    std::vector<std::size_t> selection = selectedFrames();
    if (frame != m_toldCurrent || selection != m_toldSelection) {
        m_toldCurrent = frame;
        m_toldSelection = std::move(selection);
        vpx::notifySelectionChanged();
    }
}

const document::ProjectDocument* EditorPlugins::document() const
{
    return m_window.document ? m_window.document() : nullptr;
}

document::ProjectDocument* EditorPlugins::editableDocument()
{
    return m_window.editableDocument ? m_window.editableDocument() : nullptr;
}

bool EditorPlugins::recordEdit(std::unique_ptr<edit::EditCommand> command)
{
    return m_window.recordEdit && command && m_window.recordEdit(std::move(command));
}

std::optional<std::size_t> EditorPlugins::currentFrame() const
{
    return m_window.currentFrame ? m_window.currentFrame() : std::nullopt;
}

std::vector<std::size_t> EditorPlugins::selectedFrames() const
{
    return m_window.selectedFrames ? m_window.selectedFrames() : std::vector<std::size_t>{};
}

void EditorPlugins::showFrames(std::size_t current, const std::vector<std::size_t>& selected)
{
    if (m_window.showFrames) {
        m_window.showFrames(current, selected);
    }
}

std::optional<std::size_t> EditorPlugins::currentBackground() const
{
    return m_window.currentBackground ? m_window.currentBackground() : std::nullopt;
}

std::optional<std::size_t> EditorPlugins::currentSprite() const
{
    return m_window.currentSprite ? m_window.currentSprite() : std::nullopt;
}

void EditorPlugins::addMenuEntry(const std::string& pluginId,
                                 const std::string& text,
                                 std::function<void()> action)
{
    if (!m_window.menu) {
        return;
    }
    QAction* entry = m_window.menu->addAction(QString::fromStdString(text));
    QObject::connect(entry, &QAction::triggered, [action = std::move(action)]() { action(); });
    m_menuEntries[pluginId].push_back(entry);
    updateMenu();
}

void EditorPlugins::removeMenuEntries(const std::string& pluginId)
{
    const auto entries = m_menuEntries.find(pluginId);
    if (entries == m_menuEntries.end()) {
        return;
    }
    for (QAction* entry : entries->second) {
        delete entry;
    }
    m_menuEntries.erase(entries);
    updateMenu();
}

void EditorPlugins::updateMenu()
{
    if (m_window.menu) {
        m_window.menu->menuAction()->setVisible(!m_window.menu->actions().isEmpty());
    }
}

void EditorPlugins::showMessage(const std::string&,
                                const std::string& title,
                                const std::string& text)
{
    // Not modal: the plugin's call returns at once, and the text can be selected and copied.
    auto* box = new QMessageBox(QMessageBox::Information, QString::fromStdString(title),
                                QString::fromStdString(text), QMessageBox::Close,
                                m_window.parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextInteractionFlags(Qt::TextSelectableByMouse);
    box->setModal(false);
    box->show();
}

void EditorPlugins::showStatus(const std::string&, const std::string& text)
{
    if (m_window.showStatus) {
        m_window.showStatus(QString::fromStdString(text));
    }
}

std::string EditorPlugins::setting(const std::string& pluginId, const std::string& key) const
{
    return QSettings().value(settingKey(pluginId, key)).toString().toStdString();
}

void EditorPlugins::setSetting(const std::string& pluginId,
                               const std::string& key,
                               const std::string& value)
{
    QSettings().setValue(settingKey(pluginId, key), QString::fromStdString(value));
}

bool EditorPlugins::reviewPictures(const std::string& pluginId,
                                   const std::string& title,
                                   const std::string& undoName,
                                   std::vector<vpx::ProposedPicture> pictures,
                                   std::function<void(std::size_t applied)> done)
{
    const document::ProjectDocument* project = document();
    if (!project || pictures.empty() || reviewWindow(pluginId)) {
        return false;
    }
    auto* dialog = new PictureReviewDialog(*project, QString::fromStdString(title),
                                           std::move(pictures), m_window.parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Not modal: the author can look at the frames in the editor meanwhile; an edit there
    // outdates the proposal (tick()).
    dialog->setModal(false);
    m_reviews[pluginId] = Review{dialog, project, project->revision(), undoName, std::move(done)};
    dialog->setApplyAction([this, pluginId]() { applyReview(pluginId); });
    QObject::connect(dialog, &QDialog::rejected, [this, pluginId]() {
        const auto review = m_reviews.find(pluginId);
        if (review == m_reviews.end()) {
            return;
        }
        std::function<void(std::size_t)> done = std::move(review->second.done);
        m_reviews.erase(review);
        if (done) {
            done(0);
        }
    });
    dialog->show();
    return true;
}

void EditorPlugins::applyReview(const std::string& pluginId)
{
    const auto review = m_reviews.find(pluginId);
    if (review == m_reviews.end() || !review->second.dialog) {
        return;
    }
    PictureReviewDialog* dialog = review->second.dialog;
    document::ProjectDocument* project = editableDocument();
    if (!project || project != review->second.document ||
        project->revision() != review->second.revision) {
        dialog->setOutdated(tr("The project changed since the plugin made its proposal; it "
                               "can no longer be applied."));
        return;
    }
    const std::vector<std::size_t> chosen = dialog->chosen();
    if (chosen.empty()) {
        return;
    }
    edit::EditBatch batch(review->second.undoName);
    try {
        for (std::size_t index : chosen) {
            batch.add(edit::setPicture(*project, dialog->pictures()[index].picture));
        }
    } catch (const std::exception& error) {
        batch.cancel(*project);
        dialog->setOutdated(tr("The pictures could not be applied: %1")
                                .arg(QString::fromUtf8(error.what())));
        return;
    }
    if (std::unique_ptr<edit::EditBatchCommand> command = batch.finish()) {
        // A refusal is the editor's to explain (the status bar).
        (void)recordEdit(std::move(command));
    }
    std::function<void(std::size_t)> done = std::move(review->second.done);
    m_reviews.erase(review);
    // Closed without rejecting, so that the plugin hears of it once.
    dialog->close();
    if (done) {
        done(chosen.size());
    }
}

bool EditorPlugins::editSettings(const std::string& pluginId,
                                 const std::string& title,
                                 const std::string& acceptLabel,
                                 std::vector<vpx::SettingField> fields,
                                 std::function<void(bool saved)> done)
{
    return openForm(pluginId, title, acceptLabel, {}, std::move(fields), false,
                    [done = std::move(done)](int result) {
                        if (done) {
                            done(result == vpx::kFormAccepted);
                        }
                    });
}

bool EditorPlugins::showLiveForm(const std::string& pluginId,
                                 const std::string& title,
                                 const std::string& acceptLabel,
                                 std::vector<std::string> extraButtons,
                                 std::vector<vpx::SettingField> fields,
                                 std::function<void(const std::string& key)> changed,
                                 std::function<void(int result)> closed)
{
    if (!openForm(pluginId, title, acceptLabel, std::move(extraButtons), std::move(fields), true,
                  std::move(closed))) {
        return false;
    }
    settingsWindow(pluginId)->setChangedAction(std::move(changed));
    return true;
}

bool EditorPlugins::openForm(const std::string& pluginId,
                             const std::string& title,
                             const std::string& acceptLabel,
                             std::vector<std::string> extraButtons,
                             std::vector<vpx::SettingField> fields,
                             bool live,
                             std::function<void(int result)> closed)
{
    if (fields.empty() || settingsWindow(pluginId)) {
        return false;
    }
    std::map<std::string, std::string> values;
    for (const vpx::SettingField& field : fields) {
        if (!field.key.empty()) {
            values[field.key] = setting(pluginId, field.key);
        }
    }
    auto* dialog = new PluginSettingsDialog(QString::fromStdString(title),
                                            QString::fromStdString(acceptLabel),
                                            std::move(fields), values, m_window.parent,
                                            extraButtons);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_settings[pluginId] = Settings{dialog, live, std::move(closed)};
    QObject::connect(dialog, &QDialog::finished, [this, pluginId](int result) {
        const auto settings = m_settings.find(pluginId);
        if (settings == m_settings.end()) {
            return;
        }
        QPointer<PluginSettingsDialog> form = settings->second.dialog;
        const bool live = settings->second.live;
        // An extra button keeps the values as the accept button does.
        if (result != vpx::kFormCancelled && form) {
            for (const auto& [key, value] : form->values()) {
                setSetting(pluginId, key, value);
            }
        }
        std::function<void(int)> closed = std::move(settings->second.closed);
        m_settings.erase(settings);
        if (live) {
            m_closingForms[pluginId] = form;
        }
        if (closed) {
            closed(result);
        }
        if (live) {
            m_closingForms.erase(pluginId);
        }
    });
    if (live) {
        // Beside the editor: the author can look at frames and change the selection meanwhile.
        dialog->setModal(false);
        dialog->show();
    } else {
        dialog->open();
    }
    return true;
}

std::string EditorPlugins::language() const
{
    return currentLanguage().toStdString();
}

bool EditorPlugins::darkTheme() const
{
    return isDarkThemeShown();
}

const std::vector<std::string>& EditorPlugins::pluginEditorActionIds()
{
    static const std::vector<std::string> ids = []() {
        std::vector<std::string> list{
            "copy",
            "select-all",
            "select-none",
            "invert-selection",
            "undo-selection",
            "redo-selection",
            "selection-from-background-mask",
            "mirror-selection-horizontally",
            "mirror-selection-vertically",
            "black-original-to-black",
            "remove-dynamic-zones",
            "assign-dynamic-set",
            "dynamic-to-static",
        };
        // Shadow Next To Selection and Grow Selection, in every direction.
        for (const char* group : {"shadow", "grow"}) {
            for (const char* direction :
                 {"all-around", "all-around-stern-style", "left", "bottom-left", "bottom",
                  "bottom-right", "right", "top-right", "top", "top-left"}) {
                list.push_back(std::string(group) + '-' + direction);
            }
        }
        return list;
    }();
    return ids;
}

bool EditorPlugins::editorActionEnabled(const std::string& id) const
{
    const std::vector<std::string>& ids = pluginEditorActionIds();
    if (std::find(ids.begin(), ids.end(), id) == ids.end() || !m_window.menuAction) {
        return false;
    }
    const QAction* action = m_window.menuAction(QString::fromStdString(id));
    return action && action->isEnabled();
}

bool EditorPlugins::runEditorAction(const std::string& id)
{
    if (!editorActionEnabled(id)) {
        return false;
    }
    m_window.menuAction(QString::fromStdString(id))->trigger();
    return true;
}

std::optional<std::string> EditorPlugins::formValue(const std::string& pluginId,
                                                    const std::string& key) const
{
    const PluginSettingsDialog* form = nullptr;
    if (const auto settings = m_settings.find(pluginId);
        settings != m_settings.end() && settings->second.live) {
        form = settings->second.dialog;
    } else if (const auto closing = m_closingForms.find(pluginId);
               closing != m_closingForms.end()) {
        form = closing->second;
    }
    return form ? form->value(key) : std::nullopt;
}

bool EditorPlugins::updateFormField(const std::string& pluginId,
                                    const std::string& key,
                                    const vpx::FormFieldUpdate& update)
{
    const auto settings = m_settings.find(pluginId);
    if (settings == m_settings.end() || !settings->second.live || !settings->second.dialog) {
        return false;
    }
    return settings->second.dialog->update(key, update);
}

bool EditorPlugins::closeForm(const std::string& pluginId)
{
    const auto settings = m_settings.find(pluginId);
    if (settings == m_settings.end() || !settings->second.live) {
        return false;
    }
    // Forgotten first, so that closing tells the plugin nothing.
    QPointer<PluginSettingsDialog> dialog = settings->second.dialog;
    m_settings.erase(settings);
    if (dialog) {
        dialog->close();
    }
    return true;
}

PictureReviewDialog* EditorPlugins::reviewWindow(const std::string& pluginId) const
{
    const auto review = m_reviews.find(pluginId);
    return review == m_reviews.end() ? nullptr : review->second.dialog.data();
}

PluginSettingsDialog* EditorPlugins::settingsWindow(const std::string& pluginId) const
{
    const auto settings = m_settings.find(pluginId);
    return settings == m_settings.end() ? nullptr : settings->second.dialog.data();
}

void EditorPlugins::closeWindows(const std::string& pluginId)
{
    // Forgotten first, so that closing tells the plugin nothing.
    if (const auto review = m_reviews.find(pluginId); review != m_reviews.end()) {
        QPointer<PictureReviewDialog> dialog = review->second.dialog;
        m_reviews.erase(review);
        if (dialog) {
            dialog->close();
        }
    }
    if (const auto settings = m_settings.find(pluginId); settings != m_settings.end()) {
        QPointer<PluginSettingsDialog> dialog = settings->second.dialog;
        m_settings.erase(settings);
        if (dialog) {
            dialog->close();
        }
    }
    m_closingForms.erase(pluginId);
}

}  // namespace colorizing::app
