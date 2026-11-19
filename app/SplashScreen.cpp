#include "SplashScreen.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSplashScreen>
#include <QString>

namespace colorizing::app {

QPixmap splashPixmap(QSize available)
{
    QPixmap picture(QStringLiteral(":/splash/splash.png"));
    if (picture.isNull() || available.isEmpty()) {
        return picture;
    }
    const QSize bounds(available.width() * 4 / 5, available.height() * 4 / 5);
    if (picture.width() <= bounds.width() && picture.height() <= bounds.height()) {
        return picture;
    }
    return picture.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

std::unique_ptr<QSplashScreen> showSplashScreen()
{
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return nullptr;
    }
    const QPixmap picture = splashPixmap(screen->availableGeometry().size());
    if (picture.isNull()) {
        return nullptr;
    }
    auto splash = std::make_unique<QSplashScreen>(picture);
    splash->show();
    QGuiApplication::processEvents();
    return splash;
}

}  // namespace colorizing::app
