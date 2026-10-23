#pragma once

#include <QImage>
#include <QString>
#include <QUrl>

#include <functional>
#include <map>
#include <memory>
#include <set>

namespace colorizing::app {

class HttpTransport;

// The GitHub avatars of the team's members, for the Team section: each login's picture is
// loaded once, in the background, and kept round and small for the editor's run.
class AvatarCache {
public:
    // loaded is called after an avatar came in.
    AvatarCache(HttpTransport& transport, std::function<void()> loaded);
    ~AvatarCache();
    AvatarCache(const AvatarCache&) = delete;
    AvatarCache& operator=(const AvatarCache&) = delete;

    // The avatar; a null image while it loads, when it could not be loaded, and for a text
    // that is no GitHub login.
    [[nodiscard]] QImage avatar(const QString& login);

    // Where GitHub serves a login's avatar; empty for a text that is no login.
    [[nodiscard]] static QUrl avatarUrl(const QString& login);
    // The picture cut to a circle, size pixels wide and high.
    [[nodiscard]] static QImage roundAvatar(const QImage& picture, int size);
    static constexpr int kSize = 64;

private:
    HttpTransport& m_transport;
    std::function<void()> m_loaded;
    std::map<QString, QImage> m_images;
    std::set<QString> m_requested;
    // Answers that come after the cache went are dropped.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}  // namespace colorizing::app
