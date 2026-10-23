// The GitHub client (team/GitHub.h) against a transport that answers as GitHub's REST API
// does: requests, sign-in by device flow, files, folders, repositories.

#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include "team/GitHub.h"

namespace {

using namespace colorizing;
using app::GitHubClient;
using app::HttpReply;
using app::HttpRequest;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: GitHub: " << message << '\n';
        std::exit(1);
    }
}

// Answers each request with the next reply, at once, and keeps the requests.
class FakeTransport final : public app::HttpTransport {
public:
    void send(const HttpRequest& request, std::function<void(HttpReply)> done) override
    {
        requests.push_back(request);
        expect(!replies.empty(), "a reply for " + request.url.toString().toStdString());
        HttpReply reply = replies.front();
        replies.pop_front();
        done(reply);
    }

    void answer(int status, const QByteArray& body) { replies.push_back({status, body, {}}); }

    [[nodiscard]] QByteArray header(std::size_t index, const QByteArray& name) const
    {
        for (const auto& [key, value] : requests[index].headers) {
            if (key == name) {
                return value;
            }
        }
        return {};
    }

    std::vector<HttpRequest> requests;
    std::deque<HttpReply> replies;
};

void testDeviceFlow()
{
    FakeTransport transport;
    GitHubClient client(transport, QStringLiteral("Iv1.client"));
    transport.answer(200, R"({"device_code":"dc","user_code":"WDJB-MJHT",
        "verification_uri":"https://github.com/login/device","expires_in":900,"interval":5})");
    std::optional<GitHubClient::DeviceCode> code;
    client.requestDeviceCode([&](std::optional<GitHubClient::DeviceCode> answer, QString) {
        code = answer;
    });
    expect(transport.requests[0].url.toString() ==
                   QStringLiteral("https://github.com/login/device/code") &&
               transport.requests[0].method == "POST" &&
               transport.requests[0].body == "client_id=Iv1.client&scope=repo",
           "the device code is asked for with the app's ID and the repo scope");
    expect(code && code->userCode == QStringLiteral("WDJB-MJHT") && code->intervalSeconds == 5,
           "and read");

    std::vector<GitHubClient::TokenPoll> polls;
    const auto poll = [&]() {
        client.pollAccessToken(*code, [&](GitHubClient::TokenPoll result, QString) {
            polls.push_back(result);
        });
    };
    transport.answer(200, R"({"error":"authorization_pending"})");
    poll();
    transport.answer(200, R"({"error":"slow_down"})");
    poll();
    transport.answer(200, R"({"access_token":"gho_token","token_type":"bearer"})");
    poll();
    expect(polls == std::vector<GitHubClient::TokenPoll>{GitHubClient::TokenPoll::Pending,
                                                         GitHubClient::TokenPoll::SlowDown,
                                                         GitHubClient::TokenPoll::Granted} &&
               client.token() == QStringLiteral("gho_token"),
           "pending, slower, then the token");
    const QUrlQuery form(QString::fromUtf8(transport.requests[1].body));
    expect(form.queryItemValue(QStringLiteral("grant_type"), QUrl::FullyDecoded) ==
                   QStringLiteral("urn:ietf:params:oauth:grant-type:device_code") &&
               form.queryItemValue(QStringLiteral("device_code")) == QStringLiteral("dc") &&
               form.queryItemValue(QStringLiteral("client_id")) == QStringLiteral("Iv1.client"),
           "the token is asked for with the device code");
    transport.answer(200, R"({"error":"access_denied"})");
    poll();
    expect(polls.back() == GitHubClient::TokenPoll::Denied, "a denial");

    transport.answer(200, R"({"login":"kraken"})");
    QString login;
    client.currentUser([&](QString answer, QString) { login = answer; });
    expect(login == QStringLiteral("kraken") &&
               transport.header(transport.requests.size() - 1, "Authorization") ==
                   "Bearer gho_token" &&
               transport.header(transport.requests.size() - 1, "X-GitHub-Api-Version") ==
                   "2022-11-28",
           "API calls carry the token and the API version");
}

void testFiles()
{
    FakeTransport transport;
    GitHubClient client(transport, QStringLiteral("id"));
    client.setToken(QStringLiteral("t"));
    const QByteArray claims = "{\"claims\": []}\n";
    QJsonObject file;
    // GitHub breaks its base64 into lines.
    QByteArray encoded = claims.toBase64();
    encoded.insert(8, '\n');
    file.insert(QStringLiteral("content"), QString::fromLatin1(encoded));
    file.insert(QStringLiteral("encoding"), QStringLiteral("base64"));
    file.insert(QStringLiteral("sha"), QStringLiteral("abc"));
    transport.answer(200, QJsonDocument(file).toJson());
    std::optional<GitHubClient::File> read;
    client.getFile(QStringLiteral("serum-team/afm colorization"), QStringLiteral("claims.json"),
                   [&](std::optional<GitHubClient::File> answer, QString) { read = answer; });
    expect(transport.requests[0].url.toString(QUrl::FullyEncoded) ==
                   QStringLiteral("https://api.github.com/repos/serum-team/afm%20colorization/"
                                  "contents/claims.json") &&
               read && read->bytes == claims && read->sha == QStringLiteral("abc"),
           "a file is read with its sha, the names encoded");

    transport.answer(404, R"({"message":"Not Found"})");
    bool called = false;
    QString error = QStringLiteral("unset");
    client.getFile(QStringLiteral("o/r"), QStringLiteral("claims.json"),
                   [&](std::optional<GitHubClient::File> answer, QString message) {
                       called = !answer;
                       error = message;
                   });
    expect(called && error.isEmpty(), "a missing file is no error");

    QJsonObject large;
    large.insert(QStringLiteral("encoding"), QStringLiteral("none"));
    large.insert(QStringLiteral("sha"), QStringLiteral("big"));
    transport.answer(200, QJsonDocument(large).toJson());
    transport.answer(200, QByteArray(2000000, 'x'));
    client.getFile(QStringLiteral("o/r"), QStringLiteral("packages/p.cdmdchanges"),
                   [&](std::optional<GitHubClient::File> answer, QString) { read = answer; });
    expect(read && read->bytes.size() == 2000000 && read->sha == QStringLiteral("big") &&
               transport.header(transport.requests.size() - 1, "Accept") ==
                   "application/vnd.github.raw+json",
           "a large file comes raw, in a second request");

    transport.answer(201, R"({"content":{"sha":"new"}})");
    QString sha;
    client.putFile(QStringLiteral("o/r"), QStringLiteral("claims.json"), claims,
                   QStringLiteral("Claims"), QStringLiteral("abc"),
                   [&](QString newSha, QString, bool) { sha = newSha; });
    const QJsonObject body = QJsonDocument::fromJson(transport.requests.back().body).object();
    expect(transport.requests.back().method == "PUT" && sha == QStringLiteral("new") &&
               QByteArray::fromBase64(body.value(QStringLiteral("content")).toString().toLatin1()) ==
                   claims &&
               body.value(QStringLiteral("sha")).toString() == QStringLiteral("abc"),
           "a file is written with the sha it replaces");
    transport.answer(409, R"({"message":"claims.json does not match abc"})");
    bool conflict = false;
    client.putFile(QStringLiteral("o/r"), QStringLiteral("claims.json"), claims,
                   QStringLiteral("Claims"), QStringLiteral("abc"),
                   [&](QString, QString, bool answer) { conflict = answer; });
    expect(conflict, "a file written meanwhile is a conflict");

    transport.answer(200, R"([{"type":"file","name":"a.cdmdchanges","path":"packages/a.cdmdchanges",
        "sha":"1","size":10},{"type":"dir","name":"old","path":"packages/old"}])");
    std::vector<GitHubClient::Entry> entries;
    client.listFolder(QStringLiteral("o/r"), QStringLiteral("packages"),
                      [&](std::vector<GitHubClient::Entry> answer, QString) { entries = answer; });
    expect(entries.size() == 1 && entries[0].name == QStringLiteral("a.cdmdchanges") &&
               entries[0].size == 10,
           "a folder lists its files");
}

void testRepositories()
{
    FakeTransport transport;
    GitHubClient client(transport, QStringLiteral("id"));
    client.setToken(QStringLiteral("t"));
    transport.answer(201, R"({"full_name":"serum-team/afm-colorization"})");
    QString name;
    client.createRepository(QStringLiteral("serum-team"), QStringLiteral("afm-colorization"),
                            QStringLiteral("AFM"), [&](QString full, QString) { name = full; });
    const QJsonObject body = QJsonDocument::fromJson(transport.requests.back().body).object();
    expect(transport.requests.back().url.path() == QStringLiteral("/orgs/serum-team/repos") &&
               body.value(QStringLiteral("private")).toBool() &&
               name == QStringLiteral("serum-team/afm-colorization"),
           "a private repository of the organization");
    transport.answer(201, R"({"full_name":"kraken/afm"})");
    client.createRepository(QString(), QStringLiteral("afm"), QString(),
                            [&](QString full, QString) { name = full; });
    expect(transport.requests.back().url.path() == QStringLiteral("/user/repos") &&
               name == QStringLiteral("kraken/afm"),
           "or of the user");
    transport.answer(204, {});
    QString error = QStringLiteral("unset");
    client.inviteCollaborator(QStringLiteral("kraken/afm"), QStringLiteral("enthusiast"),
                              [&](QString message) { error = message; });
    expect(error.isEmpty() && transport.requests.back().url.path() ==
                                  QStringLiteral("/repos/kraken/afm/collaborators/enthusiast"),
           "a partner is invited");
    transport.answer(403, R"({"message":"Must have admin rights"})");
    client.inviteCollaborator(QStringLiteral("kraken/afm"), QStringLiteral("x"),
                              [&](QString message) { error = message; });
    expect(error.contains(QStringLiteral("admin rights")), "GitHub's message comes along");
    transport.answer(202, R"({"full_name":"enthusiast/afm"})");
    client.forkRepository(QStringLiteral("kraken/afm"),
                          [&](QString full, QString) { name = full; });
    expect(name == QStringLiteral("enthusiast/afm"), "a fork");
    transport.replies.push_back({0, {}, QStringLiteral("Host not found")});
    client.currentUser([&](QString, QString message) { error = message; });
    expect(error == QStringLiteral("Host not found"), "without an answer, the network's error");
}

}  // namespace

void runGitHubWidgetTests()
{
    testDeviceFlow();
    testFiles();
    testRepositories();
}
