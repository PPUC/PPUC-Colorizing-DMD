#pragma once

#include <QCoreApplication>
#include <QString>

#include <functional>
#include <optional>

#include "team/GitHub.h"

namespace colorizing::app {

// The repository where authors ask for the certificate of their signing key (Signing.h): an
// issue titled "Certificate request" with the public key, which its workflow answers, after a
// trusted member approved it, with a comment holding the certificate for the account that
// opened the issue.
inline const QString kSigningRepository = QStringLiteral("PPUC/colorization-signing");

class CertificateRequest {
    Q_DECLARE_TR_FUNCTIONS(CertificateRequest)

public:
    explicit CertificateRequest(GitHubClient& client, QString repository = kSigningRepository);

    // Opens the request as the signed-in user; answers the issue's number.
    void request(const QString& publicKey,
                 const QString& editorVersion,
                 std::function<void(int number, QString error)> done);

    struct Answer {
        enum class State {
            // Not approved yet.
            Waiting,
            // certificate holds it.
            Issued,
            // Closed without a certificate, or the workflow found no key; message says why.
            Refused,
        };
        State state = State::Waiting;
        QString certificate;
        QString message;
    };
    // What the request's issue says now.
    void check(int number, std::function<void(Answer, QString error)> done);

    // The certificate in a comment of the workflow, if there is one.
    [[nodiscard]] static std::optional<QString> certificateIn(const QString& text);
    // The issue's address, to open in the browser.
    [[nodiscard]] QString issueUrl(int number) const;

private:
    GitHubClient& m_client;
    QString m_repository;
};

}  // namespace colorizing::app
