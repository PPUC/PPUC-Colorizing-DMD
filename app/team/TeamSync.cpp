#include "team/TeamSync.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QObject>
#include <QSaveFile>
#include <QTimer>

#include <utility>

#include "team/TeamSession.h"

namespace colorizing::app {

namespace {

// The GitHub repository of the input, with a client of its own.
class GitHubTeamRemote final : public TeamRemote {
public:
    GitHubTeamRemote(HttpTransport& transport, const TeamSync::Input& input)
        : m_client(transport, input.clientId),
          m_remote(m_client, input.repository)
    {
        m_client.setToken(input.token);
    }
    void readClaims(std::function<void(std::optional<QByteArray>, QString, QString)> done)
        override
    {
        m_remote.readClaims(std::move(done));
    }
    void writeClaims(const QByteArray& text,
                     const QString& version,
                     std::function<void(QString, bool)> done) override
    {
        m_remote.writeClaims(text, version, std::move(done));
    }
    void listPackages(std::function<void(std::vector<QString>, QString)> done) override
    {
        m_remote.listPackages(std::move(done));
    }
    void readPackage(const QString& name,
                     std::function<void(std::optional<QByteArray>, QString)> done) override
    {
        m_remote.readPackage(name, std::move(done));
    }
    void writePackage(const QString& name,
                      const QByteArray& bytes,
                      std::function<void(QString)> done) override
    {
        m_remote.writePackage(name, bytes, std::move(done));
    }

private:
    GitHubClient m_client;
    GitHubRemote m_remote;
};

}  // namespace

struct TeamSync::Worker {
    Worker(QObject* uiContext,
           std::function<void(Result)> delivered,
           std::chrono::milliseconds interval,
           RemoteFactory factory)
        : uiContext(uiContext),
          delivered(std::move(delivered)),
          factory(std::move(factory))
    {
        if (!this->factory) {
            this->factory = [](const Input& input, HttpTransport& transport) {
                return std::make_unique<GitHubTeamRemote>(transport, input);
            };
        }
        timer.setInterval(static_cast<int>(interval.count()));
        QObject::connect(&timer, &QTimer::timeout, [this]() { sync(); });
        timer.start();
    }

    ~Worker()
    {
        // Answers that come while the members go are not taken any more.
        shuttingDown = true;
        transport.reset();
    }

    void sync()
    {
        if (busy) {
            again = true;
            return;
        }
        if (!input.active) {
            return;
        }
        const QString key = input.repository + QLatin1Char('\n') + input.login +
                            QLatin1Char('\n') + input.token;
        if (!remote || key != remoteKey) {
            session.reset();
            remote = factory(input, *transport);
            session = std::make_unique<TeamSession>(*remote, input.login);
            remoteKey = key;
        }
        busy = true;
        auto result = std::make_shared<Result>();
        result->input = inputNumber;
        const Input asked = input;
        session->syncClaims(
            asked.claimed, asked.reserved, QDateTime::currentMSecsSinceEpoch(),
            [this, result, asked](std::optional<team::Claims> claims, QString error) {
                if (shuttingDown) {
                    return;
                }
                result->claims = std::move(claims);
                result->error = error;
                session->pendingPackages(
                    asked.mergedPackages,
                    [this, result, asked](std::vector<QString> names, QString listError) {
                        if (shuttingDown) {
                            return;
                        }
                        result->pendingPackages = std::move(names);
                        if (result->error.isEmpty()) {
                            result->error = listError;
                        }
                        download(result, asked.cacheFolder, 0);
                    });
            });
    }

    // Downloads the pending packages the cache lacks, one after another, then delivers.
    void download(const std::shared_ptr<Result>& result, const QString& cache, std::size_t next)
    {
        while (next < result->pendingPackages.size() &&
               (cache.isEmpty() ||
                QFileInfo::exists(QDir(cache).filePath(result->pendingPackages[next])))) {
            ++next;
        }
        if (next >= result->pendingPackages.size()) {
            readComments(*result, cache);
            deliver(result);
            return;
        }
        const QString name = result->pendingPackages[next];
        remote->readPackage(name, [this, result, cache, next, name](
                                      std::optional<QByteArray> bytes, QString error) {
            if (shuttingDown) {
                return;
            }
            if (bytes) {
                QDir().mkpath(cache);
                QSaveFile file(QDir(cache).filePath(name));
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(*bytes);
                    (void)file.commit();
                }
            } else if (result->error.isEmpty()) {
                result->error = error;
            }
            download(result, cache, next + 1);
        });
    }

    // The comments of the downloaded packages, each read once.
    void readComments(Result& result, const QString& cache)
    {
        for (const QString& name : result.pendingPackages) {
            auto known = comments.find(name);
            if (known == comments.end()) {
                const QString path = QDir(cache).filePath(name);
                if (cache.isEmpty() || !QFileInfo::exists(path)) {
                    continue;
                }
                const std::optional<team::ChangePackageNote> note =
                    team::readChangePackageNote(path.toStdString());
                known = comments.emplace(name, note ? QString::fromStdString(note->comment)
                                                    : QString())
                            .first;
            }
            if (!known->second.isEmpty()) {
                result.comments.emplace(name, known->second);
            }
        }
    }

    void deliver(const std::shared_ptr<Result>& result)
    {
        busy = false;
        QMetaObject::invokeMethod(
            uiContext, [deliver = delivered, result]() { deliver(*result); },
            Qt::QueuedConnection);
        if (again) {
            again = false;
            sync();
        }
    }

    QObject* uiContext;
    std::function<void(Result)> delivered;
    RemoteFactory factory;
    QTimer timer;
    Input input;
    uint64_t inputNumber = 0;
    bool busy = false;
    bool again = false;
    bool shuttingDown = false;
    QString remoteKey;
    // The comments of the packages read so far, by name (a name is never reused).
    std::map<QString, QString> comments;
    std::unique_ptr<TeamRemote> remote;
    std::unique_ptr<TeamSession> session;
    // Last, so that it goes first: its replies are cancelled while the rest is there.
    std::unique_ptr<NetworkTransport> transport = std::make_unique<NetworkTransport>();
};

TeamSync::TeamSync(QObject* context,
                   std::function<void(Result)> delivered,
                   std::chrono::milliseconds interval,
                   RemoteFactory factory)
{
    m_threadContext = new QObject;
    m_threadContext->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("Team sync"));
    m_thread.start(QThread::LowPriority);
    QMetaObject::invokeMethod(
        m_threadContext,
        [this, context, delivered = std::move(delivered), interval,
         factory = std::move(factory)]() mutable {
            m_worker = new Worker(context, std::move(delivered), interval, std::move(factory));
        },
        Qt::BlockingQueuedConnection);
}

TeamSync::~TeamSync()
{
    QMetaObject::invokeMethod(
        m_threadContext,
        [this]() {
            delete m_worker;
            m_worker = nullptr;
        },
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    delete m_threadContext;
}

uint64_t TeamSync::setInput(Input input)
{
    const uint64_t number = ++m_inputs;
    Worker* worker = m_worker;
    QMetaObject::invokeMethod(
        m_threadContext,
        [worker, input = std::move(input), number]() mutable {
            worker->input = std::move(input);
            worker->inputNumber = number;
            worker->sync();
        },
        Qt::QueuedConnection);
    return number;
}

void TeamSync::syncNow()
{
    Worker* worker = m_worker;
    QMetaObject::invokeMethod(m_threadContext, [worker]() { worker->sync(); },
                              Qt::QueuedConnection);
}

std::optional<QByteArray> TeamSync::cachedPackage(const QString& cacheFolder, const QString& name)
{
    if (cacheFolder.isEmpty()) {
        return std::nullopt;
    }
    QFile file(QDir(cacheFolder).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return file.readAll();
}

}  // namespace colorizing::app
