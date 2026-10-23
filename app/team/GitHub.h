#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QNetworkAccessManager;

namespace colorizing::app {

// One HTTP request and its answer: what the GitHub client needs of the network.
struct HttpRequest {
    QByteArray method;
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
};

struct HttpReply {
    // 0 when no answer came; networkError says why.
    int status = 0;
    QByteArray body;
    QString networkError;
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    // done is called once, on the thread of the caller's event loop.
    virtual void send(const HttpRequest& request, std::function<void(HttpReply)> done) = 0;
};

// Through Qt's network access (HTTPS with the platform's TLS backend).
class NetworkTransport final : public HttpTransport {
public:
    NetworkTransport();
    ~NetworkTransport() override;
    void send(const HttpRequest& request, std::function<void(HttpReply)> done) override;

private:
    std::unique_ptr<QNetworkAccessManager> m_manager;
};

// What the team features need of GitHub's REST API (docs.github.com/rest): signing in with
// the device flow (a code entered in the browser, no password in the editor), the team's
// repository, its files (claims.json, packages/) and forks. Every call answers through its
// callback; an error is a text for the user, empty on success.
class GitHubClient {
public:
    // clientId: the GitHub OAuth app the editor signs in through, with the device flow on.
    GitHubClient(HttpTransport& transport, QString clientId);

    [[nodiscard]] const QString& clientId() const { return m_clientId; }
    void setToken(QString token) { m_token = std::move(token); }
    [[nodiscard]] const QString& token() const { return m_token; }

    struct DeviceCode {
        QString deviceCode;
        // What the user enters at verificationUri.
        QString userCode;
        QString verificationUri;
        int intervalSeconds = 5;
        int expiresSeconds = 900;
    };
    void requestDeviceCode(std::function<void(std::optional<DeviceCode>, QString error)> done);

    enum class TokenPoll {
        // The user has not entered the code yet: ask again after the interval.
        Pending,
        // Asked too often: ask again after five seconds more.
        SlowDown,
        // The token is there (and set).
        Granted,
        Denied,
        Expired,
        Failed,
    };
    void pollAccessToken(const DeviceCode& code,
                         std::function<void(TokenPoll, QString error)> done);

    // The login of the user the token belongs to.
    void currentUser(std::function<void(QString login, QString error)> done);

    // A private repository; owner empty: the user's own, else an organization's. Answers its
    // full name, "owner/name".
    void createRepository(const QString& owner,
                          const QString& name,
                          const QString& description,
                          std::function<void(QString fullName, QString error)> done);
    void inviteCollaborator(const QString& repository,
                            const QString& login,
                            std::function<void(QString error)> done);
    // A fork in the user's account; answers its full name.
    void forkRepository(const QString& repository,
                        std::function<void(QString fullName, QString error)> done);

    struct File {
        QByteArray bytes;
        // What putFile() needs to replace it.
        QString sha;
    };
    // Nothing (and no error) for a file that does not exist.
    void getFile(const QString& repository,
                 const QString& path,
                 std::function<void(std::optional<File>, QString error)> done);
    // sha: the file's present one, empty for a new file. conflict is set when the file
    // changed meanwhile (another sha): read it again and retry.
    void putFile(const QString& repository,
                 const QString& path,
                 const QByteArray& bytes,
                 const QString& message,
                 const QString& sha,
                 std::function<void(QString newSha, QString error, bool conflict)> done);

    struct Entry {
        QString name;
        QString path;
        QString sha;
        qint64 size = 0;
    };
    // The files of a folder; none (and no error) for a folder that does not exist.
    void listFolder(const QString& repository,
                    const QString& path,
                    std::function<void(std::vector<Entry>, QString error)> done);

    // The API's address; tests and GitHub Enterprise use another.
    void setApiBase(QUrl api, QUrl web);

private:
    [[nodiscard]] HttpRequest apiRequest(const QByteArray& method, const QString& path) const;
    [[nodiscard]] static QString errorOf(const HttpReply& reply);

    HttpTransport& m_transport;
    QString m_clientId;
    QString m_token;
    QUrl m_api{QStringLiteral("https://api.github.com")};
    QUrl m_web{QStringLiteral("https://github.com")};
};

}  // namespace colorizing::app
