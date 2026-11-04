#include "EditorPlugins.h"

#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

#include <algorithm>

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

void EditorPlugins::recordEdit(std::unique_ptr<edit::EditCommand> command)
{
    if (m_window.recordEdit && command) {
        m_window.recordEdit(std::move(command));
    }
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
        recordEdit(std::move(command));
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
                                 std::vector<vpx::SettingField> fields,
                                 std::function<void(bool saved)> done)
{
    if (fields.empty() || settingsWindow(pluginId)) {
        return false;
    }
    std::map<std::string, std::string> values;
    for (const vpx::SettingField& field : fields) {
        values[field.key] = setting(pluginId, field.key);
    }
    auto* dialog = new PluginSettingsDialog(QString::fromStdString(title), std::move(fields),
                                            values, m_window.parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_settings[pluginId] = Settings{dialog, std::move(done)};
    QObject::connect(dialog, &QDialog::finished, [this, pluginId](int result) {
        const auto settings = m_settings.find(pluginId);
        if (settings == m_settings.end()) {
            return;
        }
        const bool saved = result == QDialog::Accepted && settings->second.dialog;
        if (saved) {
            for (const auto& [key, value] : settings->second.dialog->values()) {
                setSetting(pluginId, key, value);
            }
        }
        std::function<void(bool)> done = std::move(settings->second.done);
        m_settings.erase(settings);
        if (done) {
            done(saved);
        }
    });
    dialog->open();
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
}

}  // namespace colorizing::app
