#include "WindowGeometry.h"

#include <QEvent>
#include <QObject>
#include <QSettings>
#include <QWidget>

namespace colorizing::app {

namespace {

QString settingsKey(const QString& key)
{
    return QStringLiteral("windows/") + key;
}

// Stores the window's geometry when it is hidden or closed.
class GeometryKeeper final : public QObject {
public:
    GeometryKeeper(QWidget* window, QString key)
        : QObject(window),
          m_window(window),
          m_key(std::move(key))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_window &&
            (event->type() == QEvent::Hide || event->type() == QEvent::Close) &&
            m_window->isWindow() && !m_window->isMinimized()) {
            QSettings().setValue(settingsKey(m_key), m_window->saveGeometry());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* m_window;
    QString m_key;
};

}  // namespace

void keepWindowGeometry(QWidget* window, const QString& key)
{
    if (!window) {
        return;
    }
    const QByteArray stored = QSettings().value(settingsKey(key)).toByteArray();
    if (!stored.isEmpty()) {
        window->restoreGeometry(stored);
    }
    window->installEventFilter(new GeometryKeeper(window, key));
}

}  // namespace colorizing::app
