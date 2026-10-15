#include "EditorPlugins.h"

#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

#include <algorithm>

#include "document/ProjectDocument.h"
#include "vpx/VpxHost.h"

namespace colorizing::app {

namespace {

const QString kEnabledKey = QStringLiteral("plugins/enabled");
// Loaded by the DMD output, not chosen here.
const QString kDmdUtilId = QStringLiteral("DMDUtil");

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
        if (id == kDmdUtilId) {
            continue;
        }
        plugins.push_back(Plugin{id, QString::fromStdString(plugin.name),
                                 QString::fromStdString(plugin.description),
                                 QString::fromStdString(plugin.version)});
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
    if (id == kDmdUtilId) {
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
    m_loaded.removeAll(id);
    if (m_loaded.isEmpty()) {
        m_timer.stop();
    }
}

void EditorPlugins::tick()
{
    vpx::Host::instance().process();
    const document::ProjectDocument* current = document();
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

}  // namespace colorizing::app
