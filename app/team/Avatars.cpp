#include "team/Avatars.h"

#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <utility>

#include "team/GitHub.h"

namespace colorizing::app {

AvatarCache::AvatarCache(HttpTransport& transport, std::function<void()> loaded)
    : m_transport(transport),
      m_loaded(std::move(loaded))
{
}

AvatarCache::~AvatarCache()
{
    *m_alive = false;
}

QUrl AvatarCache::avatarUrl(const QString& login)
{
    // GitHub's logins: letters, digits and single hyphens, at most 39 characters.
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9](?:-?[A-Za-z0-9]){0,38}$"));
    if (!valid.match(login).hasMatch()) {
        return {};
    }
    return QUrl(QStringLiteral("https://github.com/%1.png?size=%2").arg(login).arg(kSize));
}

QImage AvatarCache::roundAvatar(const QImage& picture, int size)
{
    QImage round(size, size, QImage::Format_ARGB32_Premultiplied);
    round.fill(Qt::transparent);
    QPainter painter(&round);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(circle);
    painter.drawImage(QRect(0, 0, size, size),
                      picture.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation));
    return round;
}

QImage AvatarCache::avatar(const QString& login)
{
    const auto found = m_images.find(login);
    if (found != m_images.end()) {
        return found->second;
    }
    const QUrl url = avatarUrl(login);
    if (url.isEmpty() || !m_requested.insert(login).second) {
        return {};
    }
    HttpRequest request;
    request.method = "GET";
    request.url = url;
    m_transport.send(request, [this, alive = m_alive, login](HttpReply reply) {
        if (!*alive) {
            return;
        }
        const QImage picture = reply.status == 200 ? QImage::fromData(reply.body) : QImage();
        if (picture.isNull()) {
            return;
        }
        m_images[login] = roundAvatar(picture, kSize);
        if (m_loaded) {
            m_loaded();
        }
    });
    return {};
}

}  // namespace colorizing::app
