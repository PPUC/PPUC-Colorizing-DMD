#include "team/CertificateRequest.h"

#include <QRegularExpression>

namespace colorizing::app {

CertificateRequest::CertificateRequest(GitHubClient& client, QString repository)
    : m_client(client),
      m_repository(std::move(repository))
{
}

void CertificateRequest::request(const QString& publicKey,
                                 const QString& editorVersion,
                                 std::function<void(int number, QString error)> done)
{
    // As the issue form writes it, so that the workflow reads both alike.
    const QString body =
        QStringLiteral("### Public key\n\n%1\n\nSent by PPUC Colorizing DMD %2.\n")
            .arg(publicKey, editorVersion);
    m_client.createIssue(m_repository, QStringLiteral("Certificate request"), body,
                         std::move(done));
}

void CertificateRequest::check(int number, std::function<void(Answer, QString error)> done)
{
    m_client.issueComments(
        m_repository, number,
        [done = std::move(done)](const QString& state,
                                 const std::vector<GitHubClient::IssueComment>& comments,
                                 const QString& error) {
            Answer answer;
            if (!error.isEmpty()) {
                done(answer, error);
                return;
            }
            for (const GitHubClient::IssueComment& comment : comments) {
                if (!comment.byBot) {
                    continue;
                }
                if (const std::optional<QString> certificate = certificateIn(comment.body)) {
                    answer.state = Answer::State::Issued;
                    answer.certificate = *certificate;
                    done(answer, QString());
                    return;
                }
                if (comment.body.startsWith(QStringLiteral("No public key found"))) {
                    answer.state = Answer::State::Refused;
                    answer.message = tr("the request has no public key");
                }
            }
            if (answer.state == Answer::State::Waiting && state == QStringLiteral("closed")) {
                answer.state = Answer::State::Refused;
                answer.message = tr("the request was closed without a certificate");
            }
            done(answer, QString());
        });
}

std::optional<QString> CertificateRequest::certificateIn(const QString& text)
{
    static const QRegularExpression certificate(
        QStringLiteral("SERUM-CERTIFICATE-1:[A-Za-z0-9+/]+=*"));
    const QRegularExpressionMatch match = certificate.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(0);
}

QString CertificateRequest::issueUrl(int number) const
{
    return QStringLiteral("https://github.com/%1/issues/%2").arg(m_repository).arg(number);
}

}  // namespace colorizing::app
