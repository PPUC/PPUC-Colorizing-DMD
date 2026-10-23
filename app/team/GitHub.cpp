#include "team/GitHub.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrlQuery>

#include <utility>

namespace colorizing::app {

namespace {

const QByteArray kUserAgent = "PPUC-Colorizing-DMD";

void addHeader(HttpRequest& request, const char* name, const QByteArray& value)
{
    request.headers.append(qMakePair(QByteArray(name), value));
}

QJsonObject objectOf(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}

// "owner/name" and a path inside the repository, each segment encoded.
QString encodedPath(const QString& path)
{
    QStringList segments;
    for (const QString& segment : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        segments << QString::fromLatin1(QUrl::toPercentEncoding(segment));
    }
    return segments.join(QLatin1Char('/'));
}

}  // namespace

NetworkTransport::NetworkTransport()
    : m_manager(std::make_unique<QNetworkAccessManager>())
{
}

NetworkTransport::~NetworkTransport() = default;

void NetworkTransport::send(const HttpRequest& request, std::function<void(HttpReply)> done)
{
    QNetworkRequest networkRequest(request.url);
    for (const auto& [name, value] : request.headers) {
        networkRequest.setRawHeader(name, value);
    }
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply =
        m_manager->sendCustomRequest(networkRequest, request.method, request.body);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done = std::move(done)]() {
        HttpReply answer;
        answer.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        answer.body = reply->readAll();
        if (answer.status == 0) {
            answer.networkError = reply->errorString();
        }
        reply->deleteLater();
        done(std::move(answer));
    });
}

GitHubClient::GitHubClient(HttpTransport& transport, QString clientId)
    : m_transport(transport),
      m_clientId(std::move(clientId))
{
}

void GitHubClient::setApiBase(QUrl api, QUrl web)
{
    m_api = std::move(api);
    m_web = std::move(web);
}

HttpRequest GitHubClient::apiRequest(const QByteArray& method, const QString& path) const
{
    HttpRequest request;
    request.method = method;
    request.url = QUrl::fromEncoded((m_api.toString(QUrl::FullyEncoded) + path).toLatin1());
    request.headers = {{"Accept", "application/vnd.github+json"},
                       {"X-GitHub-Api-Version", "2022-11-28"},
                       {"User-Agent", kUserAgent}};
    if (!m_token.isEmpty()) {
        addHeader(request, "Authorization", "Bearer " + m_token.toUtf8());
    }
    return request;
}

QString GitHubClient::errorOf(const HttpReply& reply)
{
    if (reply.status == 0) {
        return reply.networkError.isEmpty() ? QStringLiteral("no answer from GitHub")
                                            : reply.networkError;
    }
    const QString message = objectOf(reply.body).value(QStringLiteral("message")).toString();
    return message.isEmpty()
               ? QStringLiteral("GitHub answered %1").arg(reply.status)
               : QStringLiteral("GitHub answered %1: %2").arg(reply.status).arg(message);
}

void GitHubClient::requestDeviceCode(
    std::function<void(std::optional<DeviceCode>, QString error)> done)
{
    HttpRequest request;
    request.method = "POST";
    request.url = m_web.resolved(QUrl(QStringLiteral("/login/device/code")));
    request.headers = {{"Accept", "application/json"},
                       {"Content-Type", "application/x-www-form-urlencoded"},
                       {"User-Agent", kUserAgent}};
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("scope"), QStringLiteral("repo"));
    request.body = form.toString(QUrl::FullyEncoded).toUtf8();
    m_transport.send(request, [done = std::move(done)](const HttpReply& reply) {
        const QJsonObject answer = objectOf(reply.body);
        if (reply.status != 200 || !answer.contains(QStringLiteral("device_code"))) {
            const QString error = answer.value(QStringLiteral("error_description")).toString();
            done(std::nullopt, error.isEmpty() ? errorOf(reply) : error);
            return;
        }
        DeviceCode code;
        code.deviceCode = answer.value(QStringLiteral("device_code")).toString();
        code.userCode = answer.value(QStringLiteral("user_code")).toString();
        code.verificationUri = answer.value(QStringLiteral("verification_uri")).toString();
        code.intervalSeconds = answer.value(QStringLiteral("interval")).toInt(5);
        code.expiresSeconds = answer.value(QStringLiteral("expires_in")).toInt(900);
        done(code, QString());
    });
}

void GitHubClient::pollAccessToken(const DeviceCode& code,
                                   std::function<void(TokenPoll, QString error)> done)
{
    HttpRequest request;
    request.method = "POST";
    request.url = m_web.resolved(QUrl(QStringLiteral("/login/oauth/access_token")));
    request.headers = {{"Accept", "application/json"},
                       {"Content-Type", "application/x-www-form-urlencoded"},
                       {"User-Agent", kUserAgent}};
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("device_code"), code.deviceCode);
    form.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    request.body = form.toString(QUrl::FullyEncoded).toUtf8();
    m_transport.send(request, [this, done = std::move(done)](const HttpReply& reply) {
        const QJsonObject answer = objectOf(reply.body);
        const QString token = answer.value(QStringLiteral("access_token")).toString();
        if (!token.isEmpty()) {
            m_token = token;
            done(TokenPoll::Granted, QString());
            return;
        }
        const QString error = answer.value(QStringLiteral("error")).toString();
        if (error == QStringLiteral("authorization_pending")) {
            done(TokenPoll::Pending, QString());
        } else if (error == QStringLiteral("slow_down")) {
            done(TokenPoll::SlowDown, QString());
        } else if (error == QStringLiteral("access_denied")) {
            done(TokenPoll::Denied, QString());
        } else if (error == QStringLiteral("expired_token")) {
            done(TokenPoll::Expired, QString());
        } else {
            const QString description =
                answer.value(QStringLiteral("error_description")).toString();
            done(TokenPoll::Failed, description.isEmpty() ? errorOf(reply) : description);
        }
    });
}

void GitHubClient::currentUser(std::function<void(QString login, QString error)> done)
{
    m_transport.send(apiRequest("GET", QStringLiteral("/user")),
                     [done = std::move(done)](const HttpReply& reply) {
                         if (reply.status != 200) {
                             done(QString(), errorOf(reply));
                             return;
                         }
                         done(objectOf(reply.body).value(QStringLiteral("login")).toString(),
                              QString());
                     });
}

void GitHubClient::createRepository(const QString& owner,
                                    const QString& name,
                                    const QString& description,
                                    std::function<void(QString fullName, QString error)> done)
{
    HttpRequest request =
        apiRequest("POST", owner.isEmpty()
                               ? QStringLiteral("/user/repos")
                               : QStringLiteral("/orgs/%1/repos").arg(encodedPath(owner)));
    addHeader(request, "Content-Type", "application/json");
    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("description"), description);
    body.insert(QStringLiteral("private"), true);
    body.insert(QStringLiteral("auto_init"), true);
    request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_transport.send(request, [done = std::move(done)](const HttpReply& reply) {
        if (reply.status != 201) {
            done(QString(), errorOf(reply));
            return;
        }
        done(objectOf(reply.body).value(QStringLiteral("full_name")).toString(), QString());
    });
}

void GitHubClient::inviteCollaborator(const QString& repository,
                                      const QString& login,
                                      std::function<void(QString error)> done)
{
    HttpRequest request =
        apiRequest("PUT", QStringLiteral("/repos/%1/collaborators/%2")
                              .arg(encodedPath(repository), encodedPath(login)));
    addHeader(request, "Content-Type", "application/json");
    request.body = R"({"permission":"push"})";
    m_transport.send(request, [done = std::move(done)](const HttpReply& reply) {
        done(reply.status == 201 || reply.status == 204 ? QString() : errorOf(reply));
    });
}

void GitHubClient::forkRepository(const QString& repository,
                                  std::function<void(QString fullName, QString error)> done)
{
    HttpRequest request =
        apiRequest("POST", QStringLiteral("/repos/%1/forks").arg(encodedPath(repository)));
    addHeader(request, "Content-Type", "application/json");
    request.body = "{}";
    m_transport.send(request, [done = std::move(done)](const HttpReply& reply) {
        if (reply.status != 202 && reply.status != 200) {
            done(QString(), errorOf(reply));
            return;
        }
        done(objectOf(reply.body).value(QStringLiteral("full_name")).toString(), QString());
    });
}

void GitHubClient::getFile(const QString& repository,
                           const QString& path,
                           std::function<void(std::optional<File>, QString error)> done)
{
    const QString address =
        QStringLiteral("/repos/%1/contents/%2").arg(encodedPath(repository), encodedPath(path));
    m_transport.send(apiRequest("GET", address), [this, address, done = std::move(done)](
                                                     const HttpReply& reply) {
        if (reply.status == 404) {
            done(std::nullopt, QString());
            return;
        }
        const QJsonObject answer = objectOf(reply.body);
        if (reply.status != 200) {
            done(std::nullopt, errorOf(reply));
            return;
        }
        File file;
        file.sha = answer.value(QStringLiteral("sha")).toString();
        if (answer.value(QStringLiteral("encoding")).toString() == QStringLiteral("base64")) {
            file.bytes = QByteArray::fromBase64(
                answer.value(QStringLiteral("content")).toString().toLatin1());
            done(std::move(file), QString());
            return;
        }
        // A file of more than a megabyte comes as it is, in a request of its own.
        HttpRequest raw = apiRequest("GET", address);
        raw.headers[0].second = "application/vnd.github.raw+json";
        m_transport.send(raw, [file, done](const HttpReply& rawReply) mutable {
            if (rawReply.status != 200) {
                done(std::nullopt, errorOf(rawReply));
                return;
            }
            file.bytes = rawReply.body;
            done(std::move(file), QString());
        });
    });
}

void GitHubClient::putFile(const QString& repository,
                           const QString& path,
                           const QByteArray& bytes,
                           const QString& message,
                           const QString& sha,
                           std::function<void(QString newSha, QString error, bool conflict)> done)
{
    HttpRequest request =
        apiRequest("PUT", QStringLiteral("/repos/%1/contents/%2")
                              .arg(encodedPath(repository), encodedPath(path)));
    addHeader(request, "Content-Type", "application/json");
    QJsonObject body;
    body.insert(QStringLiteral("message"), message);
    body.insert(QStringLiteral("content"), QString::fromLatin1(bytes.toBase64()));
    if (!sha.isEmpty()) {
        body.insert(QStringLiteral("sha"), sha);
    }
    request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_transport.send(request, [done = std::move(done)](const HttpReply& reply) {
        if (reply.status == 200 || reply.status == 201) {
            done(objectOf(reply.body)
                     .value(QStringLiteral("content"))
                     .toObject()
                     .value(QStringLiteral("sha"))
                     .toString(),
                 QString(), false);
            return;
        }
        // 409, or 422 with "sha" in the message: someone wrote the file meanwhile.
        const bool conflict =
            reply.status == 409 ||
            (reply.status == 422 && errorOf(reply).contains(QStringLiteral("sha")));
        done(QString(), errorOf(reply), conflict);
    });
}

void GitHubClient::listFolder(const QString& repository,
                              const QString& path,
                              std::function<void(std::vector<Entry>, QString error)> done)
{
    m_transport.send(
        apiRequest("GET", QStringLiteral("/repos/%1/contents/%2")
                              .arg(encodedPath(repository), encodedPath(path))),
        [done = std::move(done)](const HttpReply& reply) {
            if (reply.status == 404) {
                done({}, QString());
                return;
            }
            const QJsonDocument answer = QJsonDocument::fromJson(reply.body);
            if (reply.status != 200 || !answer.isArray()) {
                done({}, errorOf(reply));
                return;
            }
            std::vector<Entry> entries;
            for (const QJsonValue& value : answer.array()) {
                const QJsonObject item = value.toObject();
                if (item.value(QStringLiteral("type")).toString() != QStringLiteral("file")) {
                    continue;
                }
                Entry entry;
                entry.name = item.value(QStringLiteral("name")).toString();
                entry.path = item.value(QStringLiteral("path")).toString();
                entry.sha = item.value(QStringLiteral("sha")).toString();
                entry.size =
                    static_cast<qint64>(item.value(QStringLiteral("size")).toDouble());
                entries.push_back(std::move(entry));
            }
            done(std::move(entries), QString());
        });
}

}  // namespace colorizing::app
